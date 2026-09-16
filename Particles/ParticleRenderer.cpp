#include "Particles/ParticleRenderer.h"
#include "Particles/ParticleRibbon.h"
#include "Render/Renderer.h"
#include "Math/MathHelper.h"
#include "Core/Log.h"

#include <cstring>

namespace Dark
{
    using namespace Math;

    namespace
    {

        bool FailedHr(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return false;
            DE_LOG_ERROR("{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return true;
        }

        void lerpColor(const float a[4], const float b[4], float t, float out[4])
        {
            out[0] = a[0] + (b[0] - a[0]) * t;
            out[1] = a[1] + (b[1] - a[1]) * t;
            out[2] = a[2] + (b[2] - a[2]) * t;
            out[3] = a[3] + (b[3] - a[3]) * t;
        }

    } // namespace

    bool ParticleRenderer::create(Renderer& renderer)
    {
        m_renderer = &renderer;
        const DXGI_FORMAT colorFormat = renderer.sceneColorFormat();
        if (!m_pipeAdditive.create(renderer.device(), true, 0, 0.0f, colorFormat) || !m_pipeAlpha.create(renderer.device(), false, 0, 0.0f, colorFormat))
        {
            DE_LOG_ERROR("ParticleRenderer: pipeline create failed");
            return false;
        }
        if (!m_sprite.createSoftCircle(renderer, 64))
        {
            DE_LOG_ERROR("ParticleRenderer: soft sprite failed");
            return false;
        }
        if (!m_streak.createSoftStreak(renderer, 64))
        {
            DE_LOG_ERROR("ParticleRenderer: soft streak failed");
            return false;
        }
        if (!recreateUpload(renderer, 1024))
            return false;
        DE_LOG_INFO("ParticleRenderer: ready");
        return true;
    }

    void ParticleRenderer::unmapUpload()
    {
        if (m_uploadVB && m_mapped)
            m_uploadVB->Unmap(0, nullptr);
        m_mapped = nullptr;
        m_gpu    = 0;
    }

    void ParticleRenderer::destroy(Renderer& renderer)
    {
        renderer.waitForGpu();
        unmapUpload();
        m_uploadVB.Reset();
        m_capacityQuads = 0;
        m_pendingQuads  = 0;
        m_usedQuads     = 0;
        m_boundFrame    = ~0u;
        m_renderer      = nullptr;
    }

    bool ParticleRenderer::recreateUpload(Renderer& renderer, uint32_t quadsPerFrame)
    {
        if (quadsPerFrame < 256)
            quadsPerFrame = 256;

        renderer.waitForGpu();
        unmapUpload();
        m_uploadVB.Reset();

        const uint32_t verts = quadsPerFrame * 6u * kFrameCount;
        const uint64_t bytes = static_cast<uint64_t>(verts) * sizeof(ParticleVertex);

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

        if (FailedHr(renderer.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_uploadVB)), "Particle upload VB"))
        {
            m_capacityQuads = 0;
            return false;
        }

        D3D12_RANGE readRange{ 0, 0 };
        void* mapped = nullptr;
        if (FailedHr(m_uploadVB->Map(0, &readRange, &mapped), "Map particle VB"))
        {
            m_uploadVB.Reset();
            m_capacityQuads = 0;
            return false;
        }

        m_mapped        = static_cast<uint8_t*>(mapped);
        m_gpu           = m_uploadVB->GetGPUVirtualAddress();
        m_capacityQuads = quadsPerFrame;
        m_pendingQuads  = quadsPerFrame;
        m_usedQuads     = 0;
        return true;
    }

    void ParticleRenderer::beginFrame(uint32_t frameIndex)
    {
        if (m_boundFrame == frameIndex && m_uploadVB)
            return;
        if (m_renderer && m_pendingQuads > m_capacityQuads)
            recreateUpload(*m_renderer, m_pendingQuads);
        m_boundFrame = frameIndex;
        m_frameSlot  = frameIndex % kFrameCount;
        m_usedQuads  = 0;
    }

    void ParticleRenderer::draw(ID3D12GraphicsCommandList* cmd, const Camera3D& camera, const ParticleEmitter& emitter, bool additive)
    {
        if (!cmd || emitter.aliveCount() == 0)
            return;
        if (m_renderer)
            beginFrame(m_renderer->frameIndex());
        if (!m_uploadVB || !m_mapped)
            return;

        const Vector3f camRight = camera.GetRight();
        const Vector3f camUp    = camera.GetUp();
        const bool     ribbon   = emitter.desc().renderMode == ParticleEmitterDesc::RenderMode::Ribbon;

        m_cpuVerts.clear();
        m_cpuVerts.reserve(static_cast<size_t>(emitter.aliveCount()) * 6u);

        if (ribbon)
        {
            const uint32_t ribbons = clampRibbonCount(emitter.desc().ribbonCount);
            std::vector<RibbonNode> nodes;
            for (uint32_t r = 0; r < ribbons; ++r)
            {
                collectRibbonNodes(emitter.particles(), r, nodes);
                appendCameraFacingRibbon(
                    nodes.data(),
                    static_cast<uint32_t>(nodes.size()),
                    camera.GetPosition(),
                    emitter.desc().ribbonUvScale,
                    m_cpuVerts);
            }
        }
        else
        {
            for (const Particle& p : emitter.particles())
            {
                if (!p.alive)
                    continue;

                const float t    = 1.0f - (p.life / p.maxLife); // 0 birth → 1 death
                const float size = p.size0 + (p.size1 - p.size0) * t;
                float       col[4];
                lerpColor(p.color0, p.color1, t, col);

                const float hx = size * 0.5f;
                const float hy = size * 0.5f;

                const Vector3f c = p.position;
                const Vector3f r = camRight * hx;
                const Vector3f u = camUp * hy;

                const Vector3f bl(c.x - r.x - u.x, c.y - r.y - u.y, c.z - r.z - u.z);
                const Vector3f br(c.x + r.x - u.x, c.y + r.y - u.y, c.z + r.z - u.z);
                const Vector3f tr(c.x + r.x + u.x, c.y + r.y + u.y, c.z + r.z + u.z);
                const Vector3f tl(c.x - r.x + u.x, c.y - r.y + u.y, c.z - r.z + u.z);

                auto push = [&](const Vector3f& pos, float uu, float vv)
                {
                    ParticleVertex v{};
                    v.px = pos.x;
                    v.py = pos.y;
                    v.pz = pos.z;
                    v.u  = uu;
                    v.v  = vv;
                    v.r  = col[0];
                    v.g  = col[1];
                    v.b  = col[2];
                    v.a  = col[3];
                    m_cpuVerts.push_back(v);
                };

                push(bl, 0, 1);
                push(br, 1, 1);
                push(tr, 1, 0);
                push(bl, 0, 1);
                push(tr, 1, 0);
                push(tl, 0, 0);
            }
        }

        if (m_cpuVerts.empty())
            return;

        const uint32_t quads = static_cast<uint32_t>(m_cpuVerts.size() / 6u);
        if (quads == 0)
            return;
        if (m_usedQuads + quads > m_capacityQuads)
        {
            m_pendingQuads = Math::Max(m_pendingQuads, m_usedQuads + quads);
            if (m_usedQuads == 0 && m_renderer && recreateUpload(*m_renderer, m_pendingQuads))
            {
                m_frameSlot = m_boundFrame % kFrameCount;
            }
            else
            {
                DE_LOG_WARN("ParticleRenderer: upload ring full ({}+{} > {}), skipping emitter", m_usedQuads, quads, m_capacityQuads);
                return;
            }
        }

        const uint32_t vertOffset = particleUploadVertOffset(m_frameSlot, m_capacityQuads, m_usedQuads);
        std::memcpy(m_mapped + static_cast<size_t>(vertOffset) * sizeof(ParticleVertex), m_cpuVerts.data(), m_cpuVerts.size() * sizeof(ParticleVertex));

        ParticlePipeline& pipe = additive ? m_pipeAdditive : m_pipeAlpha;
        pipe.bind(cmd);

        ParticleFrameConstants fc{};
        const Matrix4f         vp = camera.GetViewProj();
        std::memcpy(fc.viewProj, vp.m_afEntry, sizeof(float) * 16);
        pipe.setConstants(cmd, fc);

        Texture2D& sprite = ribbon ? m_streak : m_sprite;
        sprite.bind(cmd, ParticlePipeline::kRootSrv);

        D3D12_VERTEX_BUFFER_VIEW vbv{};
        vbv.BufferLocation = m_gpu + static_cast<UINT64>(vertOffset) * sizeof(ParticleVertex);
        vbv.StrideInBytes  = sizeof(ParticleVertex);
        vbv.SizeInBytes    = static_cast<UINT>(m_cpuVerts.size() * sizeof(ParticleVertex));

        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->IASetVertexBuffers(0, 1, &vbv);
        cmd->DrawInstanced(static_cast<UINT>(m_cpuVerts.size()), 1, 0, 0);
        m_usedQuads += quads;
    }

} // namespace Dark
