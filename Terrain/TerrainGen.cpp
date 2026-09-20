#include "Terrain/TerrainGen.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Render/TerrainErosionPipeline.h"
#include "Render/Texture2D.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

namespace Dark::Terrain
{
    using Microsoft::WRL::ComPtr;
    using namespace Math;

    namespace
    {
        bool Report(bool (*progress)(float, const char*, void*), void* user, float t, const char* phase)
        {
            if (!progress)
                return true;
            return progress(t, phase, user);
        }

        void Invalidate(HeightMap& map)
        {
            map = HeightMap{};
        }

        // IQ gradient noise (MIT, Shadertoy XdXBRH): value in x, analytic d/dx d/dz.
        void HashGrad(int ix, int iz, uint32_t seed, float& gx, float& gz)
        {
            const float kx = 0.3183099f;
            const float kz = 0.3678794f;
            float       x  = static_cast<float>(ix) + 0.0133f * static_cast<float>(seed & 255u);
            float       z  = static_cast<float>(iz) + 0.0171f * static_cast<float>((seed >> 8) & 255u);
            x              = x * kx + kz;
            z              = z * kz + kx;
            const float t  = x * z * (x + z);
            const float fx = t - floorf(t);
            gx             = -1.0f + 2.0f * (16.0f * kx * fx - floorf(16.0f * kx * fx));
            const float t2 = t * 1.3247179572f;
            const float fz = t2 - floorf(t2);
            gz             = -1.0f + 2.0f * (16.0f * kz * fz - floorf(16.0f * kz * fz));
        }

        void GradientNoise(float px, float pz, uint32_t seed, float& v, float& dx, float& dz)
        {
            const int   ix = static_cast<int>(floorf(px));
            const int   iz = static_cast<int>(floorf(pz));
            const float fx = px - static_cast<float>(ix);
            const float fz = pz - static_cast<float>(iz);
            const float u  = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
            const float vv = fz * fz * fz * (fz * (fz * 6.0f - 15.0f) + 10.0f);
            const float du = 30.0f * fx * fx * (fx * (fx - 2.0f) + 1.0f);
            const float dv = 30.0f * fz * fz * (fz * (fz - 2.0f) + 1.0f);

            float gax, gaz, gbx, gbz, gcx, gcz, gdx, gdz;
            HashGrad(ix, iz, seed, gax, gaz);
            HashGrad(ix + 1, iz, seed, gbx, gbz);
            HashGrad(ix, iz + 1, seed, gcx, gcz);
            HashGrad(ix + 1, iz + 1, seed, gdx, gdz);

            const float va = gax * fx + gaz * fz;
            const float vb = gbx * (fx - 1.0f) + gbz * fz;
            const float vc = gcx * fx + gcz * (fz - 1.0f);
            const float vd = gdx * (fx - 1.0f) + gdz * (fz - 1.0f);

            v  = va + u * (vb - va) + vv * (vc - va) + u * vv * (va - vb - vc + vd);
            dx = gax + u * (gbx - gax) + vv * (gcx - gax) + u * vv * (gax - gbx - gcx + gdx) + du * (vv * (va - vb - vc + vd) + (vb - va));
            dz = gaz + u * (gbz - gaz) + vv * (gcz - gaz) + u * vv * (gaz - gbz - gcz + gdz) + dv * (u * (va - vb - vc + vd) + (vc - va));
        }

        // Downhill cosine ridges (Clay John / Fewes idea: directional noise along slope).
        void DownhillRidges(float px, float pz, float dirX, float dirZ, uint32_t seed, float& h, float& ddx, float& ddz)
        {
            const int   ix = static_cast<int>(floorf(px));
            const int   iz = static_cast<int>(floorf(pz));
            const float fx = px - static_cast<float>(ix);
            const float fz = pz - static_cast<float>(iz);
            const float f  = 6.28318530718f;
            float       hx = 0.0f;
            float       hy = 0.0f;
            float       hz = 0.0f;
            float       wt = 0.0f;
            for (int j = -1; j <= 1; ++j)
            {
                for (int i = -1; i <= 1; ++i)
                {
                    const float ox = static_cast<float>(i);
                    const float oz = static_cast<float>(j);
                    const float hx0 = hash21(ix + i, iz + j, seed);
                    const float hz0 = hash21(ix + i, iz + j, seed ^ 0x9e3779b9u);
                    const float qx  = fx - ox - (hx0 - 0.5f) * 0.5f;
                    const float qz  = fz - oz - (hz0 - 0.5f) * 0.5f;
                    const float d2  = qx * qx + qz * qz;
                    const float w   = expf(-d2 * 2.0f);
                    const float mag = qx * dirX + qz * dirZ;
                    wt += w;
                    hx += cosf(mag * f) * w;
                    const float s = -sinf(mag * f) * w;
                    hy += s * dirX;
                    hz += s * dirZ;
                }
            }
            if (wt < 1.0e-8f)
            {
                h   = 0.0f;
                ddx = 0.0f;
                ddz = 0.0f;
                return;
            }
            h   = hx / wt;
            ddx = hy / wt;
            ddz = hz / wt;
        }

        float SampleEroded(float u, float v, uint32_t seed)
        {
            const float heightTiles = 3.0f;
            const int   heightOct   = 3;
            const float heightAmp   = 0.25f;
            const float heightGain  = 0.1f;
            const float heightLac   = 2.0f;
            const float waterH      = 0.45f;
            const int   eroOct      = 5;
            const float eroTiles    = 4.0f;
            const float eroGain     = 0.5f;
            const float eroLac      = 2.0f;
            const float slopeK      = 3.0f;
            const float branchK     = 3.0f;
            const float eroStr      = 0.04f;

            const float px = u * heightTiles;
            const float pz = v * heightTiles;
            float       n  = 0.0f;
            float       nx = 0.0f;
            float       nz = 0.0f;
            float       nf = 1.0f;
            float       na = heightAmp;
            for (int i = 0; i < heightOct; ++i)
            {
                float gv, gdx, gdz;
                GradientNoise(px * nf, pz * nf, seed, gv, gdx, gdz);
                n += gv * na;
                nx += gdx * na * nf;
                nz += gdz * na * nf;
                na *= heightGain;
                nf *= heightLac;
            }
            n = n * 0.5f + 0.5f;

            float dirX = nz * slopeK;
            float dirZ = -nx * slopeK;

            float a    = 0.5f * SmoothStep(waterH - 0.1f, waterH + 0.2f, n);
            float f    = 1.0f;
            float hSum = 0.0f;
            float hx   = 0.0f;
            float hz   = 0.0f;
            for (int i = 0; i < eroOct; ++i)
            {
                float rh, rdx, rdz;
                DownhillRidges(px * eroTiles * f, pz * eroTiles * f, dirX + hz * branchK, dirZ - hx * branchK, seed + 17u, rh, rdx, rdz);
                hSum += rh * a;
                hx += rdx * a * f;
                hz += rdz * a * f;
                a *= eroGain;
                f *= eroLac;
            }
            return Clamp(n + (hSum - 0.5f) * eroStr, 0.0f, 1.0f);
        }

        void FillErodedRect(float* dst, int dstW, int dstH, int originX, int originZ, int fullW, int fullH, const WorldGenDesc& desc)
        {
            const float invW = 1.0f / static_cast<float>(fullW > 1 ? fullW - 1 : 1);
            const float invH = 1.0f / static_cast<float>(fullH > 1 ? fullH - 1 : 1);
            for (int z = 0; z < dstH; ++z)
            {
                for (int x = 0; x < dstW; ++x)
                {
                    const float u                          = static_cast<float>(originX + x) * invW;
                    const float v                          = static_cast<float>(originZ + z) * invH;
                    dst[static_cast<size_t>(z) * dstW + x] = SampleEroded(u, v, desc.erosion.seed);
                }
            }
        }

        void FlattenSea(float* samples, size_t count, float sea)
        {
            for (size_t i = 0; i < count; ++i)
            {
                if (samples[i] < sea)
                    samples[i] = sea;
            }
        }

        float SampleBilinear(const float* s, int w, int h, float fx, float fz)
        {
            const float maxX = static_cast<float>(w - 1);
            const float maxZ = static_cast<float>(h - 1);
            fx               = Clamp(fx, 0.0f, maxX);
            fz               = Clamp(fz, 0.0f, maxZ);
            const int   x0   = static_cast<int>(floorf(fx));
            const int   z0   = static_cast<int>(floorf(fz));
            const int   x1   = x0 < w - 1 ? x0 + 1 : x0;
            const int   z1   = z0 < h - 1 ? z0 + 1 : z0;
            const float tx   = fx - static_cast<float>(x0);
            const float tz   = fz - static_cast<float>(z0);
            const float h00  = s[static_cast<size_t>(z0) * w + x0];
            const float h10  = s[static_cast<size_t>(z0) * w + x1];
            const float h01  = s[static_cast<size_t>(z1) * w + x0];
            const float h11  = s[static_cast<size_t>(z1) * w + x1];
            return Lerp(Lerp(h00, h10, tx), Lerp(h01, h11, tx), tz);
        }

        void Gradient(const float* s, int w, int h, float fx, float fz, float& gx, float& gz)
        {
            const float maxX = static_cast<float>(w - 1);
            const float maxZ = static_cast<float>(h - 1);
            fx               = Clamp(fx, 0.0f, maxX);
            fz               = Clamp(fz, 0.0f, maxZ);
            const int   x0   = static_cast<int>(floorf(fx));
            const int   z0   = static_cast<int>(floorf(fz));
            const int   x1   = x0 < w - 1 ? x0 + 1 : x0;
            const int   z1   = z0 < h - 1 ? z0 + 1 : z0;
            const float tx   = fx - static_cast<float>(x0);
            const float tz   = fz - static_cast<float>(z0);
            const float h00  = s[static_cast<size_t>(z0) * w + x0];
            const float h10  = s[static_cast<size_t>(z0) * w + x1];
            const float h01  = s[static_cast<size_t>(z1) * w + x0];
            const float h11  = s[static_cast<size_t>(z1) * w + x1];
            gx               = Lerp(h10 - h00, h11 - h01, tz);
            gz               = Lerp(h01 - h00, h11 - h10, tx);
        }

        void AddBilinear(float* s, int w, int h, float fx, float fz, float amount)
        {
            if (fx < 0.0f || fz < 0.0f || fx > static_cast<float>(w - 1) || fz > static_cast<float>(h - 1))
                return;
            const int   x0 = static_cast<int>(floorf(fx));
            const int   z0 = static_cast<int>(floorf(fz));
            const int   x1 = x0 < w - 1 ? x0 + 1 : x0;
            const int   z1 = z0 < h - 1 ? z0 + 1 : z0;
            const float tx = fx - static_cast<float>(x0);
            const float tz = fz - static_cast<float>(z0);
            s[static_cast<size_t>(z0) * w + x0] += amount * (1.0f - tx) * (1.0f - tz);
            s[static_cast<size_t>(z0) * w + x1] += amount * tx * (1.0f - tz);
            s[static_cast<size_t>(z1) * w + x0] += amount * (1.0f - tx) * tz;
            s[static_cast<size_t>(z1) * w + x1] += amount * tx * tz;
        }

        void Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
        {
            D3D12_RESOURCE_BARRIER b{};
            b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource   = resource;
            b.Transition.StateBefore = before;
            b.Transition.StateAfter  = after;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1, &b);
        }

        bool FailedHr(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return false;
            DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return true;
        }

        bool CreateBuffer(ID3D12Device* device, UINT64 bytes, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES state, ComPtr<ID3D12Resource>& out)
        {
            out.Reset();
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = heapType;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width            = bytes;
            desc.Height           = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels        = 1;
            desc.SampleDesc       = { 1, 0 };
            desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            return !FailedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&out)), "CreateCommittedResource (terrain bake buffer)");
        }

        bool WaitFence(ID3D12CommandQueue* queue, ID3D12Fence* fence, UINT64& value, HANDLE event)
        {
            ++value;
            if (FailedHr(queue->Signal(fence, value), "Signal (terrain bake fence)"))
                return false;
            if (fence->GetCompletedValue() < value)
            {
                if (FailedHr(fence->SetEventOnCompletion(value, event), "SetEventOnCompletion (terrain bake)"))
                    return false;
                WaitForSingleObject(event, INFINITE);
            }
            return true;
        }

        bool FlushBake(TerrainErosionPipeline& pipeline, ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Fence* fence, UINT64& value, HANDLE event)
        {
            ID3D12GraphicsCommandList* cmd = pipeline.commandList();
            if (!cmd)
                return false;
            if (FailedHr(cmd->Close(), "Close (terrain bake list)"))
                return false;
            ID3D12CommandList* lists[] = { cmd };
            queue->ExecuteCommandLists(1, lists);
            if (!WaitFence(queue, fence, value, event))
                return false;
            return pipeline.resetCommands(device);
        }

        ErosionGpuConstants MakeGpuConstants(uint32_t w, uint32_t h, float cellSize, const ErosionParams& p)
        {
            ErosionGpuConstants c{};
            c.width       = w;
            c.height      = h;
            c.cellSize    = cellSize;
            c.talusTan    = p.talusTan;
            c.thermalRate = p.thermalRate;
            c.evaporate   = p.evaporate;
            c.capacity    = p.capacity;
            c.erode       = p.erode;
            c.deposit     = p.deposit;
            c.gravity     = p.gravity;
            c.seaLevelRaw = p.seaLevelRaw;
            c.pipeDt      = 0.2f;
            c.rain        = p.evaporate;
            return c;
        }

        bool GpuErode(HeightMap& map, const ErosionParams& params, TerrainErosionPipeline& pipeline, ID3D12Device* device, ID3D12CommandQueue* queue, bool (*progress)(float, const char*, void*),
                      void* user, bool* cancelled)
        {
            if (cancelled)
                *cancelled = false;
            if (!map.valid() || !pipeline.isValid() || !device || !queue)
                return false;
            const uint32_t w       = map.width();
            const uint32_t h       = map.height();
            float*         samples = map.mutableSamples();

            Texture2D heightA;
            Texture2D heightB;
            Texture2D water;
            Texture2D flux;
            Texture2D sediment;
            if (!heightA.createUavR32Float(device, w, h) || !heightB.createUavR32Float(device, w, h) || !water.createUavR32Float(device, w, h) || !flux.createUavRgba32Float(device, w, h) ||
                !sediment.createUavR32Float(device, w, h))
                return false;

            const D3D12_RESOURCE_DESC          texDesc = heightA.resource()->GetDesc();
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
            UINT                               rows    = 0;
            UINT64                             rowSize = 0;
            UINT64                             total   = 0;
            device->GetCopyableFootprints(&texDesc, 0, 1, 0, &fp, &rows, &rowSize, &total);

            ComPtr<ID3D12Resource> upload;
            ComPtr<ID3D12Resource> readback;
            if (!CreateBuffer(device, total, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, upload) ||
                !CreateBuffer(device, total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, readback))
                return false;

            uint8_t* mapped = nullptr;
            if (FailedHr(upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped)), "Map (terrain bake upload)"))
                return false;
            for (UINT y = 0; y < h; ++y)
            {
                std::memcpy(mapped + fp.Footprint.RowPitch * y, samples + static_cast<size_t>(y) * w, static_cast<size_t>(w) * sizeof(float));
            }
            upload->Unmap(0, nullptr);

            ComPtr<ID3D12Fence> fence;
            if (FailedHr(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence (terrain bake)"))
                return false;
            HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!event)
                return false;
            UINT64 fenceValue = 0;

            if (!pipeline.resetCommands(device))
            {
                CloseHandle(event);
                return false;
            }
            ID3D12GraphicsCommandList* cmd = pipeline.commandList();

            Transition(cmd, heightA.resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_TEXTURE_COPY_LOCATION dstLoc{};
            dstLoc.pResource        = heightA.resource();
            dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dstLoc.SubresourceIndex = 0;
            D3D12_TEXTURE_COPY_LOCATION srcLoc{};
            srcLoc.pResource       = upload.Get();
            srcLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            srcLoc.PlacedFootprint = fp;
            cmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

            const int thermalIters = params.thermalIterations < 0 ? 0 : params.thermalIterations;
            const int pipeIters    = params.hydraulicIterations < 0 ? 0 : params.hydraulicIterations;
            if (thermalIters > 0)
                Transition(cmd, heightA.resource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            else
                Transition(cmd, heightA.resource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            // Clear water / flux / sediment via UAV; flush so later table copies cannot change this clear.
            ID3D12DescriptorHeap* heaps[] = { pipeline.heap() };
            cmd->SetDescriptorHeaps(1, heaps);
            device->CopyDescriptorsSimple(1, pipeline.cpuHandle(1), water.cpuHandleUav(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            device->CopyDescriptorsSimple(1, pipeline.cpuHandle(2), flux.cpuHandleUav(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            device->CopyDescriptorsSimple(1, pipeline.cpuHandle(3), sediment.cpuHandleUav(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            const float z4[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            cmd->ClearUnorderedAccessViewFloat(pipeline.gpuHandle(1), water.cpuHandleUav(), water.resource(), z4, 0, nullptr);
            cmd->ClearUnorderedAccessViewFloat(pipeline.gpuHandle(2), flux.cpuHandleUav(), flux.resource(), z4, 0, nullptr);
            cmd->ClearUnorderedAccessViewFloat(pipeline.gpuHandle(3), sediment.cpuHandleUav(), sediment.resource(), z4, 0, nullptr);

            if (!FlushBake(pipeline, device, queue, fence.Get(), fenceValue, event))
            {
                CloseHandle(event);
                return false;
            }

            const ErosionGpuConstants baseConst = MakeGpuConstants(w, h, map.cellSize(), params);
            Texture2D*                srcTex    = &heightA;
            Texture2D*                dstTex    = &heightB;
            D3D12_RESOURCE_STATES     srcState  = thermalIters > 0 ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            D3D12_RESOURCE_STATES     dstState  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            uint32_t                  batchSlot = 0;

            auto flushBatch = [&]() -> bool
            {
                if (!FlushBake(pipeline, device, queue, fence.Get(), fenceValue, event))
                    return false;
                batchSlot = 0;
                return true;
            };

            cmd = pipeline.commandList();
            for (int iter = 0; iter < thermalIters; ++iter)
            {
                if (srcState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
                {
                    Transition(cmd, srcTex->resource(), srcState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    srcState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                }
                if (dstState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
                {
                    Transition(cmd, dstTex->resource(), dstState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    dstState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                }
                if (!pipeline.dispatchThermal(cmd, device, *srcTex, *dstTex, baseConst, batchSlot))
                {
                    CloseHandle(event);
                    return false;
                }
                ++batchSlot;
                const bool last = (iter + 1 == thermalIters);
                if (!last)
                {
                    Transition(cmd, dstTex->resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    Transition(cmd, srcTex->resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    Texture2D* tmp = srcTex;
                    srcTex         = dstTex;
                    dstTex         = tmp;
                    srcState       = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                    dstState       = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                }
                else
                {
                    srcTex   = dstTex;
                    srcState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                }

                if (batchSlot >= TerrainErosionPipeline::kBatchSlots)
                {
                    if (!flushBatch())
                    {
                        CloseHandle(event);
                        return false;
                    }
                    cmd           = pipeline.commandList();
                    const float t = 0.15f + 0.35f * (static_cast<float>(iter + 1) / static_cast<float>(thermalIters));
                    if (!Report(progress, user, t, "thermal"))
                    {
                        if (cancelled)
                            *cancelled = true;
                        CloseHandle(event);
                        return false;
                    }
                }
            }

            if (batchSlot > 0 && !flushBatch())
            {
                CloseHandle(event);
                return false;
            }
            cmd = pipeline.commandList();

            if (srcState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
            {
                Transition(cmd, srcTex->resource(), srcState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                srcState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            }

            for (int iter = 0; iter < pipeIters; ++iter)
            {
                if (!pipeline.dispatchPipe(cmd, device, *srcTex, water, flux, sediment, baseConst, batchSlot))
                {
                    CloseHandle(event);
                    return false;
                }
                ++batchSlot;
                if (batchSlot >= TerrainErosionPipeline::kBatchSlots)
                {
                    if (!flushBatch())
                    {
                        CloseHandle(event);
                        return false;
                    }
                    cmd           = pipeline.commandList();
                    const float t = 0.5f + 0.4f * (static_cast<float>(iter + 1) / static_cast<float>(pipeIters > 0 ? pipeIters : 1));
                    if (!Report(progress, user, t, "hydraulic"))
                    {
                        if (cancelled)
                            *cancelled = true;
                        CloseHandle(event);
                        return false;
                    }
                }
            }

            Transition(cmd, srcTex->resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION rbDst{};
            rbDst.pResource       = readback.Get();
            rbDst.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            rbDst.PlacedFootprint = fp;
            D3D12_TEXTURE_COPY_LOCATION rbSrc{};
            rbSrc.pResource        = srcTex->resource();
            rbSrc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            rbSrc.SubresourceIndex = 0;
            cmd->CopyTextureRegion(&rbDst, 0, 0, 0, &rbSrc, nullptr);

            if (!FlushBake(pipeline, device, queue, fence.Get(), fenceValue, event))
            {
                CloseHandle(event);
                return false;
            }

            uint8_t* rbMapped = nullptr;
            if (FailedHr(readback->Map(0, nullptr, reinterpret_cast<void**>(&rbMapped)), "Map (terrain bake readback)"))
            {
                CloseHandle(event);
                return false;
            }
            for (UINT y = 0; y < h; ++y)
                std::memcpy(samples + static_cast<size_t>(y) * w, rbMapped + fp.Footprint.RowPitch * y, static_cast<size_t>(w) * sizeof(float));
            readback->Unmap(0, nullptr);
            CloseHandle(event);
            FlattenSea(samples, static_cast<size_t>(w) * h, params.seaLevelRaw);
            return true;
        }

        bool WorkingSize(const WorldGenDesc& desc, uint32_t& outW, uint32_t& outH)
        {
            if (desc.tilesX < 1 || desc.tilesZ < 1 || desc.tilesX > static_cast<uint32_t>(kMaxWorldTiles) || desc.tilesZ > static_cast<uint32_t>(kMaxWorldTiles) || desc.tileCells == 0 ||
                desc.cellSize <= 0.0f)
                return false;
            const uint64_t w = static_cast<uint64_t>(desc.tilesX) * desc.tileCells + 1ull;
            const uint64_t h = static_cast<uint64_t>(desc.tilesZ) * desc.tileCells + 1ull;
            if (w > static_cast<uint64_t>(kMaxWorkingSize) || h > static_cast<uint64_t>(kMaxWorkingSize))
                return false;
            outW = static_cast<uint32_t>(w);
            outH = static_cast<uint32_t>(h);
            return true;
        }
    } // namespace

    float hash21(int x, int z, uint32_t seed)
    {
        uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u + seed * 2246822519u;
        h          = (h ^ (h >> 13)) * 1274126177u;
        return static_cast<float>(h & 0x00FFFFFFu) * (1.0f / 16777216.0f);
    }

    bool applyThermalJacobi(HeightMap& map, const ErosionParams& params)
    {
        if (!map.valid())
            return false;
        const int iters = params.thermalIterations;
        if (iters <= 0)
            return true;

        const int          w    = static_cast<int>(map.width());
        const int          h    = static_cast<int>(map.height());
        const float        cell = map.cellSize();
        float*             out  = map.mutableSamples();
        std::vector<float> a(out, out + static_cast<size_t>(w) * h);
        std::vector<float> b   = a;
        float*             src = a.data();
        float*             dst = b.data();

        const int ox[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
        const int oz[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };

        for (int iter = 0; iter < iters; ++iter)
        {
            std::memcpy(dst, src, static_cast<size_t>(w) * h * sizeof(float));
            for (int z = 1; z < h - 1; ++z)
            {
                for (int x = 1; x < w - 1; ++x)
                {
                    const float hv  = src[static_cast<size_t>(z) * w + x];
                    float       acc = hv;
                    for (int n = 0; n < 8; ++n)
                    {
                        const float nh    = src[static_cast<size_t>(z + oz[n]) * w + (x + ox[n])];
                        const float dist  = sqrtf(static_cast<float>(ox[n] * ox[n] + oz[n] * oz[n])) * cell;
                        const float dh    = hv - nh;
                        const float talus = params.talusTan * dist;
                        if (dh > talus)
                            acc -= params.thermalRate * (dh - talus) / 8.0f;
                    }
                    dst[static_cast<size_t>(z) * w + x] = acc;
                }
            }
            float* tmp = src;
            src        = dst;
            dst        = tmp;
        }
        std::memcpy(out, src, static_cast<size_t>(w) * h * sizeof(float));
        return true;
    }

    bool applyHydraulicDroplets(HeightMap& map, const ErosionParams& params)
    {
        if (!map.valid())
            return false;
        if (params.hydraulicMaxSteps <= 0)
            return true;

        const int w         = static_cast<int>(map.width());
        const int h         = static_cast<int>(map.height());
        float*    s         = map.mutableSamples();
        const int nDroplets = params.hydraulicDroplets > 0 ? params.hydraulicDroplets : (w * h / 8);
        if (nDroplets <= 0)
            return true;

        const float maxX = static_cast<float>(w - 1);
        const float maxZ = static_cast<float>(h - 1);
        const float cell = map.cellSize();

        for (int i = 0; i < nDroplets; ++i)
        {
            float fx       = hash21(i, 0, params.seed) * maxX;
            float fz       = hash21(i, 1, params.seed) * maxZ;
            float dirX     = 0.0f;
            float dirZ     = 0.0f;
            float vel      = 0.0f;
            float water    = 1.0f;
            float sediment = 0.0f;

            for (int step = 1; step <= params.hydraulicMaxSteps; ++step)
            {
                if (water < 1.0e-4f)
                    break;
                const float hCur = SampleBilinear(s, w, h, fx, fz);
                if (hCur < params.seaLevelRaw)
                    break;

                float gx = 0.0f;
                float gz = 0.0f;
                Gradient(s, w, h, fx, fz, gx, gz);
                dirX      = dirX * params.hydraulicInertia - gx * (1.0f - params.hydraulicInertia);
                dirZ      = dirZ * params.hydraulicInertia - gz * (1.0f - params.hydraulicInertia);
                float len = sqrtf(dirX * dirX + dirZ * dirZ);
                if (len < 1.0e-5f)
                {
                    dirX = hash21(i, step, params.seed) * 2.0f - 1.0f;
                    dirZ = hash21(i, step + 7919, params.seed) * 2.0f - 1.0f;
                    len  = sqrtf(dirX * dirX + dirZ * dirZ);
                }
                if (len > 1.0e-5f)
                {
                    dirX /= len;
                    dirZ /= len;
                }

                const float newX = fx + dirX;
                const float newZ = fz + dirZ;
                if (newX < 0.0f || newZ < 0.0f || newX > maxX || newZ > maxZ)
                    break;

                const float cap = Max(vel, 0.01f) * water * params.capacity;
                if (sediment < cap)
                {
                    float amt = params.erode * (cap - sediment);
                    if (hCur - amt < params.seaLevelRaw)
                        amt = Max(hCur - params.seaLevelRaw, 0.0f);
                    AddBilinear(s, w, h, fx, fz, -amt);
                    sediment += amt;
                }
                else
                {
                    const float amt = params.deposit * (sediment - cap);
                    AddBilinear(s, w, h, fx, fz, amt);
                    sediment -= amt;
                }

                fx = newX;
                fz = newZ;
                water *= (1.0f - params.evaporate);
                const float slopeMag = sqrtf(gx * gx + gz * gz);
                vel                  = (vel + params.gravity * cell * slopeMag) * (1.0f - params.evaporate);
            }
        }
        return true;
    }

    bool generateWorld(const WorldGenDesc& desc, HeightMap& outFull, SplatMap& outSplat, bool (*progress)(float t, const char* phase, void* user), void* user, const WorldGenGpu* gpu)
    {
        Invalidate(outFull);
        outSplat = SplatMap{};

        uint32_t w = 0;
        uint32_t h = 0;
        if (!WorkingSize(desc, w, h))
        {
            DE_LOG_ERROR(LogCategory::Render, "TerrainGen: world {}x{} tiles / {} cells exceeds cap", desc.tilesX, desc.tilesZ, desc.tileCells);
            return false;
        }
        if (!Report(progress, user, 0.0f, "noise"))
            return false;

        const auto t0 = std::chrono::steady_clock::now();
        DE_LOG_INFO(LogCategory::Render, "TerrainGen: {}x{} samples, downhill-gully filter seed {}", w, h, desc.erosion.seed);

        HeightMap working;
        if (!working.createWorking(w, h, desc.cellSize, desc.heightScale))
            return false;
        working.setOrigin(desc.origin);
        FillErodedRect(working.mutableSamples(), static_cast<int>(w), static_cast<int>(h), 0, 0, static_cast<int>(w), static_cast<int>(h), desc);
        (void)gpu;
        float* samples = working.mutableSamples();
        const float scale = desc.heightScale > 0.0f ? desc.heightScale : 1.0f;
        const size_t n    = static_cast<size_t>(w) * h;
        for (size_t i = 0; i < n; ++i)
            samples[i] *= scale;
        const float seaM = desc.erosion.seaLevelRaw * scale;
        FlattenSea(samples, n, seaM);

        if (!Report(progress, user, 0.7f, "gullies"))
            return false;

        if (!Report(progress, user, 0.9f, "splat"))
            return false;
        if (!outSplat.generateFromHeight(working))
            return false;

        outFull          = std::move(working);
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        DE_LOG_INFO(LogCategory::Render, "TerrainGen: done in {:.1f}s", sec);
        if (!Report(progress, user, 1.0f, "done"))
        {
            Invalidate(outFull);
            outSplat = SplatMap{};
            return false;
        }
        return true;
    }

    bool regenerateRect(HeightMap& map, const WorldGenDesc& desc, int x0, int z0, int x1, int z1, const WorldGenGpu* gpu)
    {
        if (!map.valid())
            return false;
        const int w = static_cast<int>(map.width());
        const int h = static_cast<int>(map.height());
        if (x0 > x1)
        {
            const int t = x0;
            x0          = x1;
            x1          = t;
        }
        if (z0 > z1)
        {
            const int t = z0;
            z0          = z1;
            z1          = t;
        }
        if (x1 < 0 || z1 < 0 || x0 >= w || z0 >= h)
            return false;
        x0 = Max(x0, 0);
        z0 = Max(z0, 0);
        x1 = Min(x1, w - 1);
        z1 = Min(z1, h - 1);

        const bool gpuOk = gpu && gpu->pipeline && gpu->pipeline->isValid() && gpu->device && gpu->queue;
        int        pad   = desc.erosion.thermalIterations;
        if (pad < 0)
            pad = 0;
        if (gpuOk)
        {
            int hp = desc.erosion.hydraulicIterations;
            if (hp < 0)
                hp = 0;
            pad += hp;
        }

        const int ex0 = Max(x0 - pad, 0);
        const int ez0 = Max(z0 - pad, 0);
        const int ex1 = Min(x1 + pad, w - 1);
        const int ez1 = Min(z1 + pad, h - 1);
        const int tw  = ex1 - ex0 + 1;
        const int th  = ez1 - ez0 + 1;

        HeightMap tile;
        if (!tile.createWorking(static_cast<uint32_t>(tw), static_cast<uint32_t>(th), map.cellSize(), map.heightScale()))
            return false;
        FillErodedRect(tile.mutableSamples(), tw, th, ex0, ez0, w, h, desc);
        (void)gpuOk;
        float* ts = tile.mutableSamples();
        const float scale = map.heightScale() > 0.0f ? map.heightScale() : 1.0f;
        const size_t tn   = static_cast<size_t>(tw) * static_cast<size_t>(th);
        for (size_t i = 0; i < tn; ++i)
            ts[i] *= scale;
        FlattenSea(ts, tn, desc.erosion.seaLevelRaw * scale);

        float*       dst = map.mutableSamples();
        const float* src = tile.samples();
        for (int z = z0; z <= z1; ++z)
        {
            const int lz = z - ez0;
            for (int x = x0; x <= x1; ++x)
            {
                const int lx                        = x - ex0;
                dst[static_cast<size_t>(z) * w + x] = src[static_cast<size_t>(lz) * tw + lx];
            }
        }
        return true;
    }

} // namespace Dark::Terrain
