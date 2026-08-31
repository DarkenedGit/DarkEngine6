#pragma once

#include "Particles/ParticleTypes.h"
#include "Render/ParticlePipeline.h"
#include "Render/Texture2D.h"
#include "Render/Camera3D.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{
    class Renderer;
    namespace Terrain
    {
        class HeightMap;
    }

    // Height-draped blood decal verts. Returns vertex count (triangle list).
    uint32_t buildBloodSplatVerts(
        ParticleVertex* out,
        uint32_t        outCap,
        float           centerX,
        float           centerZ,
        float           radius,
        float           yaw,
        float           yBias,
        float (*heightAt)(void*, float, float),
        void* user);

    // 10 projected ground splatters; oldest slot is reused.
    class BloodSplatPool
    {
    public:
        static constexpr int      kCapacity     = 10;
        static constexpr int      kGrid         = 6;
        static constexpr uint32_t kVertsPerSplat = (kGrid - 1) * (kGrid - 1) * 6u;

        bool create(Renderer& renderer);
        void destroy(Renderer& renderer);

        void spawn(float worldX, float worldZ, const Terrain::HeightMap& heightMap);
        void draw(ID3D12GraphicsCommandList* cmd, const Camera3D& camera) const;

        int  activeCount() const { return m_active; }
        int  nextIndex() const { return m_next; }
        bool isValid() const { return m_uploadVB != nullptr && m_pipe.isValid() && m_tex.valid(); }

    private:
        ParticlePipeline m_pipe;
        Texture2D        m_tex;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_uploadVB;
        Renderer*        m_renderer = nullptr;
        int              m_next     = 0;
        int              m_active   = 0;
        uint32_t         m_seed     = 1;
    };

} // namespace Dark
