#include "Render/GpuIbl.h"
#include "Assets/Image.h"
#include "Core/Log.h"
#include "Math/Color.h"
#include "Render/Renderer.h"
#include "Render/Texture2D.h"

#include <chrono>
#include <cstring>
#include <vector>

namespace Dark
{

    namespace
    {
        bool FailedHr(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return false;
            DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return true;
        }

        struct BakeConstants
        {
            uint32_t faceIndex;
            float    roughness;
            uint32_t sampleCount;
            uint32_t pad;
        };

        void CreateCubeRtv(ID3D12Device* device, ID3D12Resource* resource, uint32_t mip, uint32_t face, D3D12_CPU_DESCRIPTOR_HANDLE dest)
        {
            D3D12_RENDER_TARGET_VIEW_DESC rtv{};
            rtv.Format                         = DXGI_FORMAT_R16G16B16A16_FLOAT;
            rtv.ViewDimension                  = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            rtv.Texture2DArray.MipSlice        = mip;
            rtv.Texture2DArray.FirstArraySlice = face;
            rtv.Texture2DArray.ArraySize       = 1;
            device->CreateRenderTargetView(resource, &rtv, dest);
        }

        void CreateCubeSrv(ID3D12Device* device, ID3D12Resource* resource, uint32_t mips, D3D12_CPU_DESCRIPTOR_HANDLE dest)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format                        = DXGI_FORMAT_R16G16B16A16_FLOAT;
            srv.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURECUBE;
            srv.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.TextureCube.MostDetailedMip   = 0;
            srv.TextureCube.MipLevels         = mips;
            srv.TextureCube.ResourceMinLODClamp = 0.0f;
            device->CreateShaderResourceView(resource, &srv, dest);
        }

        bool CreateCubeResource(ID3D12Device* device, uint32_t size, uint32_t mips, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state, const wchar_t* name,
                                ComPtr<ID3D12Resource>& out)
        {
            out.Reset();
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width            = size;
            desc.Height           = size;
            desc.DepthOrArraySize = 6;
            desc.MipLevels        = static_cast<UINT16>(mips);
            desc.Format           = DXGI_FORMAT_R16G16B16A16_FLOAT;
            desc.SampleDesc       = { 1, 0 };
            desc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags            = flags;

            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_CLEAR_VALUE  clear{};
            D3D12_CLEAR_VALUE* clearPtr = nullptr;
            if (flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
            {
                clear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                clearPtr     = &clear;
            }

            if (FailedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, clearPtr, IID_PPV_ARGS(&out)), "CreateCommittedResource (ibl cube)"))
                return false;
            if (name)
                out->SetName(name);
            return true;
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

        void DrawCubeFaces(ID3D12GraphicsCommandList* cmd, const IblBakePipeline& pipe, ID3D12PipelineState* pso, uint32_t srvSlot, uint32_t mip, uint32_t size,
                           const BakeConstants& base)
        {
            cmd->SetGraphicsRootSignature(pipe.rootSignature());
            cmd->SetPipelineState(pso);
            ID3D12DescriptorHeap* heaps[] = { pipe.srvHeap() };
            cmd->SetDescriptorHeaps(1, heaps);
            cmd->SetGraphicsRootDescriptorTable(IblBakePipeline::kRootSrv, pipe.srvGpu(srvSlot));
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            D3D12_VIEWPORT vp{};
            vp.Width    = static_cast<float>(size);
            vp.Height   = static_cast<float>(size);
            vp.MaxDepth = 1.0f;
            D3D12_RECT sc{ 0, 0, static_cast<LONG>(size), static_cast<LONG>(size) };
            cmd->RSSetViewports(1, &vp);
            cmd->RSSetScissorRects(1, &sc);

            BakeConstants c = base;
            for (uint32_t face = 0; face < 6; ++face)
            {
                c.faceIndex                           = face;
                const D3D12_CPU_DESCRIPTOR_HANDLE rtv = pipe.rtvCpu(mip * 6u + face);
                cmd->SetGraphicsRoot32BitConstants(IblBakePipeline::kRootConstants, 4, &c, 0);
                cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
                cmd->DrawInstanced(3, 1, 0, 0);
            }
        }
    } // namespace

    void GpuIbl::resetGpu()
    {
        m_ready = false;
        m_envCube.Reset();
        m_irradiance.Reset();
        m_prefilter.Reset();
        m_cpuSrvHeap.Reset();
        m_envCpu         = {};
        m_irradianceCpu  = {};
        m_prefilterCpu   = {};
    }

    bool GpuIbl::bake(Renderer* renderer, const Image& equirect, const IblBakeSettings& settings)
    {
        resetGpu();
        if (!renderer || !renderer->device())
        {
            DE_LOG_ERROR(LogCategory::Render, "Ibl: bake failed (null renderer / PSO)");
            return false;
        }
        if (!equirect.valid() || equirect.format() != ImageFormat::RGBA32F || !equirect.pixels())
        {
            DE_LOG_ERROR(LogCategory::Render, "Ibl: failed to load '{}' — using ambient", "equirect");
            return false;
        }
        if (settings.equirectToCubeSize == 0 || settings.irradianceSize == 0 || settings.prefilterSize == 0 || settings.prefilterMips == 0
            || settings.sampleCountIrr == 0 || settings.sampleCountPref == 0)
        {
            DE_LOG_ERROR(LogCategory::Render, "Ibl: bake failed (null renderer / PSO)");
            return false;
        }

        ID3D12Device* device = renderer->device();
        if (!renderer->queue())
        {
            DE_LOG_ERROR(LogCategory::Render, "Ibl: bake failed (null renderer / PSO)");
            return false;
        }
        if (!m_pipeline.create(device))
        {
            DE_LOG_ERROR(LogCategory::Render, "Ibl: bake failed (null renderer / PSO)");
            return false;
        }

        const auto t0 = std::chrono::steady_clock::now();

        if (!CreateCubeResource(device, settings.equirectToCubeSize, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, L"DE.Ibl.EnvCube",
                                m_envCube)
            || !CreateCubeResource(device, settings.irradianceSize, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, L"DE.Ibl.Irradiance",
                                   m_irradiance)
            || !CreateCubeResource(device, settings.prefilterSize, settings.prefilterMips, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                   L"DE.Ibl.Prefilter", m_prefilter))
        {
            resetGpu();
            DE_LOG_ERROR(LogCategory::Render, "Ibl: bake failed (null renderer / PSO)");
            return false;
        }

        Texture2D equirectGpu;
        if (!equirectGpu.createFromImage(*renderer, equirect, Color::TextureUsage::Ibl) || equirectGpu.cpuHandle().ptr == 0)
        {
            resetGpu();
            DE_LOG_ERROR(LogCategory::Render, "Ibl: failed to load '{}' — using ambient", "equirect");
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC cpuHeapDesc{};
        cpuHeapDesc.NumDescriptors = 3;
        cpuHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cpuHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FailedHr(device->CreateDescriptorHeap(&cpuHeapDesc, IID_PPV_ARGS(&m_cpuSrvHeap)), "CreateDescriptorHeap (ibl cube SRV)"))
        {
            resetGpu();
            return false;
        }
        const UINT incr      = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_envCpu             = m_cpuSrvHeap->GetCPUDescriptorHandleForHeapStart();
        m_irradianceCpu      = m_envCpu;
        m_irradianceCpu.ptr += incr;
        m_prefilterCpu       = m_irradianceCpu;
        m_prefilterCpu.ptr += incr;
        CreateCubeSrv(device, m_envCube.Get(), 1, m_envCpu);
        CreateCubeSrv(device, m_irradiance.Get(), 1, m_irradianceCpu);
        CreateCubeSrv(device, m_prefilter.Get(), settings.prefilterMips, m_prefilterCpu);

        // Slot 0 = equirect, slot 1 = 128 cube. Tables resolve at execute, so both must stay live.
        device->CopyDescriptorsSimple(1, m_pipeline.srvCpu(0), equirectGpu.cpuHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        device->CopyDescriptorsSimple(1, m_pipeline.srvCpu(1), m_envCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        for (uint32_t face = 0; face < 6; ++face)
            CreateCubeRtv(device, m_envCube.Get(), 0, face, m_pipeline.rtvCpu(face));

        if (!m_pipeline.resetCommands(device))
        {
            resetGpu();
            DE_LOG_ERROR(LogCategory::Render, "Ibl: bake failed (null renderer / PSO)");
            return false;
        }
        ID3D12GraphicsCommandList* cmd = m_pipeline.commandList();

        BakeConstants equirectConst{};
        equirectConst.sampleCount = 1;
        DrawCubeFaces(cmd, m_pipeline, m_pipeline.psoEquirect(), 0, 0, settings.equirectToCubeSize, equirectConst);
        Transition(cmd, m_envCube.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        for (uint32_t face = 0; face < 6; ++face)
            CreateCubeRtv(device, m_irradiance.Get(), 0, face, m_pipeline.rtvCpu(face));

        BakeConstants irrConst{};
        irrConst.sampleCount = settings.sampleCountIrr;
        DrawCubeFaces(cmd, m_pipeline, m_pipeline.psoIrradiance(), 1, 0, settings.irradianceSize, irrConst);
        Transition(cmd, m_irradiance.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        for (uint32_t mip = 0; mip < settings.prefilterMips; ++mip)
        {
            for (uint32_t face = 0; face < 6; ++face)
                CreateCubeRtv(device, m_prefilter.Get(), mip, face, m_pipeline.rtvCpu(mip * 6u + face));
        }

        const float invMip = settings.prefilterMips > 1u ? 1.0f / static_cast<float>(settings.prefilterMips - 1u) : 0.0f;
        for (uint32_t mip = 0; mip < settings.prefilterMips; ++mip)
        {
            const uint32_t size = (settings.prefilterSize >> mip) > 0 ? (settings.prefilterSize >> mip) : 1u;
            BakeConstants  prefConst{};
            prefConst.roughness   = static_cast<float>(mip) * invMip;
            prefConst.sampleCount = settings.sampleCountPref;
            DrawCubeFaces(cmd, m_pipeline, m_pipeline.psoPrefilter(), 1, mip, size, prefConst);
        }
        Transition(cmd, m_prefilter.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        if (FailedHr(cmd->Close(), "Close ibl bake list"))
        {
            resetGpu();
            return false;
        }
        ID3D12CommandList* lists[] = { cmd };
        renderer->queue()->ExecuteCommandLists(1, lists);
        renderer->waitForGpu();

        m_ready          = true;
        const auto ms    = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        DE_LOG_INFO(LogCategory::Render, "Ibl: baked '{}' ({}x{}) in {} ms", "equirect", equirect.width(), equirect.height(), static_cast<int>(ms));
        return true;
    }

    bool createBlackIblCube(Renderer& renderer, uint32_t size, ComPtr<ID3D12Resource>& outResource, ComPtr<ID3D12DescriptorHeap>& outCpuHeap,
                            D3D12_CPU_DESCRIPTOR_HANDLE& outCpu)
    {
        outResource.Reset();
        outCpuHeap.Reset();
        outCpu = {};
        ID3D12Device* device = renderer.device();
        if (!device || !renderer.queue() || size == 0)
        {
            DE_LOG_ERROR(LogCategory::Render, "createBlackIblCube: null device or size");
            return false;
        }

        if (!CreateCubeResource(device, size, 1, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, L"DE.Ibl.DummyCube", outResource))
            return false;

        D3D12_RESOURCE_DESC desc = outResource->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[6]{};
        UINT  numRows[6]{};
        UINT64 rowSizes[6]{};
        UINT64 uploadBytes = 0;
        device->GetCopyableFootprints(&desc, 0, 6, 0, footprints, numRows, rowSizes, &uploadBytes);

        D3D12_HEAP_PROPERTIES uploadHeap{};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC uploadDesc{};
        uploadDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        uploadDesc.Width            = uploadBytes;
        uploadDesc.Height           = 1;
        uploadDesc.DepthOrArraySize = 1;
        uploadDesc.MipLevels        = 1;
        uploadDesc.SampleDesc       = { 1, 0 };
        uploadDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> upload;
        if (FailedHr(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)),
                     "CreateCommittedResource (ibl dummy upload)"))
        {
            outResource.Reset();
            return false;
        }

        uint8_t* mapped = nullptr;
        if (FailedHr(upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped)), "Map ibl dummy upload"))
        {
            outResource.Reset();
            return false;
        }
        std::memset(mapped, 0, static_cast<size_t>(uploadBytes));
        upload->Unmap(0, nullptr);

        ComPtr<ID3D12CommandAllocator>    alloc;
        ComPtr<ID3D12GraphicsCommandList> list;
        if (FailedHr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)), "CreateCommandAllocator (ibl dummy)")
            || FailedHr(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list)), "CreateCommandList (ibl dummy)"))
        {
            outResource.Reset();
            return false;
        }

        for (UINT face = 0; face < 6; ++face)
        {
            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource        = outResource.Get();
            dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = face;
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource       = upload.Get();
            src.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint = footprints[face];
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        }
        Transition(list.Get(), outResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (FailedHr(list->Close(), "Close ibl dummy list"))
        {
            outResource.Reset();
            return false;
        }
        ID3D12CommandList* lists[] = { list.Get() };
        renderer.queue()->ExecuteCommandLists(1, lists);
        renderer.waitForGpu();

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = 1;
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&outCpuHeap)), "CreateDescriptorHeap (ibl dummy SRV)"))
        {
            outResource.Reset();
            return false;
        }
        outCpu = outCpuHeap->GetCPUDescriptorHandleForHeapStart();
        CreateCubeSrv(device, outResource.Get(), 1, outCpu);
        return true;
    }

} // namespace Dark
