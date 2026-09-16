#pragma once

#include "Particles/ParticleEmitter.h"
#include "Assets/Material.h"
#include "Render/ParticlePipeline.h"
#include "Render/Camera3D.h"
#include "Math/Matrix4f.h"

#include <cstdint>
#include <d3d12.h>
#include <vector>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;

    // Vertex offset into a 2-frame upload ring. Each draw must use a unique range
    // so later Map/memcpy cannot clobber an in-flight DrawInstanced.
    inline uint32_t particleUploadVertOffset(uint32_t frameSlot, uint32_t quadsPerFrame, uint32_t usedQuads)
    {
        return (frameSlot * quadsPerFrame + usedQuads) * 6u;
    }

    // Builds camera-facing quads from CPU particles and draws with ParticlePipeline.
    class ParticleRenderer
    {
    public:
        static constexpr uint32_t kFrameCount = 2;

        ParticleRenderer() = default;

        bool create(Renderer& renderer, class AssetManager& assets);
        void destroy(Renderer& renderer);

        // Pin the bump allocator to this Renderer::frameIndex() slot. Safe to call
        // every draw; only the first call of a frame resets the write cursor.
        void beginFrame(uint32_t frameIndex);

        // Upload + draw all alive particles. material albedo is the sprite; null uses the
        // interned soft-circle / soft-streak default for the emitter's render mode.
        void draw(ID3D12GraphicsCommandList* cmd, const Camera3D& camera, const ParticleEmitter& emitter, bool additive,
                  const Material* material = nullptr);

    private:
        bool recreateUpload(Renderer& renderer, uint32_t quadsPerFrame);
        void unmapUpload();

        ParticlePipeline m_pipeAdditive;
        ParticlePipeline m_pipeAlpha;
        AssetRef<Material> m_billboardMat;
        AssetRef<Material> m_ribbonMat;

        Microsoft::WRL::ComPtr<ID3D12Resource> m_uploadVB;
        uint8_t*                               m_mapped              = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS              m_gpu                 = 0;
        uint32_t                               m_capacityQuads       = 0;
        uint32_t                               m_pendingQuads        = 0;
        uint32_t                               m_usedQuads           = 0;
        uint32_t                               m_frameSlot           = 0;
        uint32_t                               m_boundFrame          = ~0u;

        std::vector<ParticleVertex> m_cpuVerts;
        Renderer*                   m_renderer = nullptr;
    };

} // namespace Dark
