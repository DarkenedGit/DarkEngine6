#include "Particles/BloodSplatPool.h"

#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Math/Matrix4f.h"
#include "Render/Renderer.h"
#include "Terrain/HeightMap.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace Dark
{
    using Math::Vector3f;

    namespace
    {
        float hash01(uint32_t n)
        {
            n ^= n >> 16;
            n *= 0x7feb352du;
            n ^= n >> 15;
            n *= 0x846ca68bu;
            n ^= n >> 16;
            return static_cast<float>(n) * (1.0f / 4294967295.0f);
        }

        bool createSplatTexture(Renderer& renderer, Texture2D& out)
        {
            constexpr uint32_t kSize = 64;
            std::vector<uint8_t> px(static_cast<size_t>(kSize) * kSize * 4u);
            const float cx = (static_cast<float>(kSize) - 1.0f) * 0.5f;
            for (uint32_t y = 0; y < kSize; ++y)
            {
                for (uint32_t x = 0; x < kSize; ++x)
                {
                    const float u = (static_cast<float>(x) - cx) / cx;
                    const float v = (static_cast<float>(y) - cx) / cx;
                    const float r = std::sqrt(u * u + v * v);
                    float blobs = 0.0f;
                    const float drops[5][3] = {
                        { 0.00f, 0.00f, 1.05f },
                        { 0.28f, -0.18f, 0.55f },
                        { -0.32f, 0.22f, 0.50f },
                        { 0.18f, 0.34f, 0.42f },
                        { -0.22f, -0.30f, 0.40f },
                    };
                    for (int i = 0; i < 5; ++i)
                    {
                        const float dx = u - drops[i][0];
                        const float dy = v - drops[i][1];
                        const float d  = std::sqrt(dx * dx + dy * dy) / drops[i][2];
                        float       a  = 1.0f - d;
                        if (a < 0.0f)
                            a = 0.0f;
                        a = a * a * (3.0f - 2.0f * a);
                        blobs = Math::Max(blobs, a);
                    }
                    const float edge = Math::Clamp(1.0f - r, 0.0f, 1.0f);
                    float       a    = blobs * edge;
                    a                = a * a * (3.0f - 2.0f * a);
                    const size_t i   = (static_cast<size_t>(y) * kSize + x) * 4u;
                    px[i + 0]        = 150;
                    px[i + 1]        = 6;
                    px[i + 2]        = 10;
                    px[i + 3]        = static_cast<uint8_t>(a * 255.0f + 0.5f);
                }
            }
            return out.createFromRGBA(renderer, px.data(), kSize, kSize, kSize * 4u);
        }
    } // namespace

    uint32_t buildBloodSplatVerts(
        ParticleVertex* out,
        uint32_t        outCap,
        float           centerX,
        float           centerZ,
        float           radius,
        float           yaw,
        float           yBias,
        float (*heightAt)(void*, float, float),
        void* user)
    {
        if (!out || outCap < BloodSplatPool::kVertsPerSplat)
            return 0;
        if (radius < 0.1f)
            radius = 0.1f;

        const int   n    = BloodSplatPool::kGrid;
        const float c    = std::cos(yaw);
        const float s    = std::sin(yaw);
        Vector3f    grid[BloodSplatPool::kGrid][BloodSplatPool::kGrid];
        Vector3f    uv[BloodSplatPool::kGrid][BloodSplatPool::kGrid];

        for (int iz = 0; iz < n; ++iz)
        {
            const float v  = static_cast<float>(iz) / static_cast<float>(n - 1);
            const float lz = (v - 0.5f) * 2.0f * radius;
            for (int ix = 0; ix < n; ++ix)
            {
                const float u  = static_cast<float>(ix) / static_cast<float>(n - 1);
                const float lx = (u - 0.5f) * 2.0f * radius;
                const float wx = centerX + lx * c - lz * s;
                const float wz = centerZ + lx * s + lz * c;
                float       wy = 0.0f;
                if (heightAt)
                    wy = heightAt(user, wx, wz);
                Vector3f nrm{ 0.0f, 1.0f, 0.0f };
                if (heightAt && yBias != 0.0f)
                {
                    const float e  = 0.25f;
                    const float hL = heightAt(user, wx - e, wz);
                    const float hR = heightAt(user, wx + e, wz);
                    const float hD = heightAt(user, wx, wz - e);
                    const float hU = heightAt(user, wx, wz + e);
                    nrm            = Vector3f{ hL - hR, 2.0f * e, hD - hU };
                    const float nsq = nrm.MagnitudeSqrd();
                    if (nsq > 1.0e-10f)
                        nrm *= (1.0f / std::sqrt(nsq));
                    else
                        nrm = Vector3f{ 0.0f, 1.0f, 0.0f };
                }
                grid[iz][ix] = Vector3f{ wx + nrm.x * yBias, wy + nrm.y * yBias, wz + nrm.z * yBias };
                uv[iz][ix]   = Vector3f{ u, v, 0.0f };
            }
        }

        uint32_t count = 0;
        auto     push  = [&](int ix, int iz) {
            ParticleVertex& v = out[count++];
            const Vector3f& p = grid[iz][ix];
            v.px              = p.x;
            v.py              = p.y;
            v.pz              = p.z;
            v.u               = uv[iz][ix].x;
            v.v               = uv[iz][ix].y;
            v.r               = 1.0f;
            v.g               = 1.0f;
            v.b               = 1.0f;
            v.a               = 1.0f;
        };

        for (int iz = 0; iz < n - 1; ++iz)
        {
            for (int ix = 0; ix < n - 1; ++ix)
            {
                push(ix, iz);
                push(ix + 1, iz);
                push(ix + 1, iz + 1);
                push(ix, iz);
                push(ix + 1, iz + 1);
                push(ix, iz + 1);
            }
        }
        return count;
    }

    bool BloodSplatPool::create(Renderer& renderer)
    {
        m_renderer = &renderer;
        m_next     = 0;
        m_active   = 0;
        m_seed     = 17;
        if (!m_pipe.create(renderer.device(), false, -4, -3.0f))
        {
            DE_LOG_ERROR(LogCategory::Render, "BloodSplatPool: pipeline failed");
            return false;
        }
        if (!createSplatTexture(renderer, m_tex))
        {
            DE_LOG_ERROR(LogCategory::Render, "BloodSplatPool: texture failed");
            return false;
        }

        const uint64_t bytes = static_cast<uint64_t>(kCapacity) * kVertsPerSplat * sizeof(ParticleVertex);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width            = bytes;
        desc.Height           = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.SampleDesc       = { 1, 0 };
        desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(renderer.device()->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_uploadVB))))
        {
            DE_LOG_ERROR(LogCategory::Render, "BloodSplatPool: upload VB failed");
            return false;
        }
        DE_LOG_INFO(LogCategory::Render, "BloodSplatPool: {} slots, {} verts each", kCapacity, kVertsPerSplat);
        return true;
    }

    void BloodSplatPool::destroy(Renderer& renderer)
    {
        renderer.waitForGpu();
        m_uploadVB.Reset();
        m_renderer = nullptr;
        m_active   = 0;
        m_next     = 0;
    }

    void BloodSplatPool::spawn(float worldX, float worldZ, const Terrain::HeightMap& heightMap)
    {
        if (!m_uploadVB)
            return;

        m_seed = m_seed * 1664525u + 1013904223u;
        const float yaw    = hash01(m_seed) * 6.2831853f;
        const float radius = 1.6f + hash01(m_seed ^ 0x9e3779b9u) * 0.9f;

        ParticleVertex verts[kVertsPerSplat];
        struct Ctx
        {
            const Terrain::HeightMap* hm;
        } ctx{ &heightMap };
        auto heightAt = [](void* user, float x, float z) -> float {
            return static_cast<Ctx*>(user)->hm->heightAtWorld(x, z);
        };
        const uint32_t n = buildBloodSplatVerts(verts, kVertsPerSplat, worldX, worldZ, radius, yaw, 0.16f, heightAt, &ctx);
        if (n == 0)
            return;

        const int      slot  = m_next;
        const uint64_t bytes = static_cast<uint64_t>(n) * sizeof(ParticleVertex);
        const uint64_t off   = static_cast<uint64_t>(slot) * kVertsPerSplat * sizeof(ParticleVertex);
        uint8_t*       mapped = nullptr;
        D3D12_RANGE    read{ 0, 0 };
        if (FAILED(m_uploadVB->Map(0, &read, reinterpret_cast<void**>(&mapped))))
            return;
        std::memcpy(mapped + off, verts, bytes);
        D3D12_RANGE written{ static_cast<SIZE_T>(off), static_cast<SIZE_T>(off + bytes) };
        m_uploadVB->Unmap(0, &written);

        m_next = (m_next + 1) % kCapacity;
        if (m_active < kCapacity)
            ++m_active;
    }

    void BloodSplatPool::draw(ID3D12GraphicsCommandList* cmd, const Camera3D& camera) const
    {
        if (!cmd || !m_uploadVB || m_active <= 0 || !m_pipe.isValid())
            return;

        m_pipe.bind(cmd);
        ParticleFrameConstants fc{};
        const Math::Matrix4f vp = camera.GetViewProj();
        std::memcpy(fc.viewProj, vp.m_afEntry, sizeof(float) * 16);
        m_pipe.setConstants(cmd, fc);
        m_tex.bind(cmd, ParticlePipeline::kRootSrv);

        D3D12_VERTEX_BUFFER_VIEW vbv{};
        vbv.BufferLocation = m_uploadVB->GetGPUVirtualAddress();
        vbv.StrideInBytes  = sizeof(ParticleVertex);
        vbv.SizeInBytes    = static_cast<UINT>(kCapacity * kVertsPerSplat * sizeof(ParticleVertex));
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->IASetVertexBuffers(0, 1, &vbv);
        cmd->DrawInstanced(static_cast<UINT>(m_active) * kVertsPerSplat, 1, 0, 0);
    }

} // namespace Dark
