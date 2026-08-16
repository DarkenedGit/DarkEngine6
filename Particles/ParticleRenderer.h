#pragma once

#include "Particles/ParticleEmitter.h"
#include "Render/ParticlePipeline.h"
#include "Render/Texture2D.h"
#include "Render/Camera3D.h"
#include "Math/Matrix4f.h"

#include <cstdint>
#include <d3d12.h>
#include <vector>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;

    // Builds camera-facing quads from CPU particles and draws with ParticlePipeline.
    class ParticleRenderer
    {
    public:
        ParticleRenderer() = default;

        bool create(Renderer& renderer);
        void destroy(Renderer& renderer);

        // Upload + draw all alive particles in emitter.
        void draw(ID3D12GraphicsCommandList* cmd, const Camera3D& camera, const ParticleEmitter& emitter, bool additive);

        Texture2D& sprite()
        {
            return m_sprite;
        }

    private:
        bool ensureUploadCapacity(Renderer& renderer, uint32_t quadCount);

        ParticlePipeline m_pipeAdditive;
        ParticlePipeline m_pipeAlpha;
        Texture2D        m_sprite;
        Texture2D        m_streak;

        Microsoft::WRL::ComPtr<ID3D12Resource> m_uploadVB;
        uint32_t                               m_uploadCapacityQuads = 0;

        std::vector<ParticleVertex> m_cpuVerts;
        Renderer*                   m_renderer = nullptr;
    };

} // namespace Dark
