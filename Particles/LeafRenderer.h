#pragma once

#include "Assets/Material.h"
#include "Particles/ParticleTypes.h"
#include "Render/Frustum3f.h"
#include "Render/ParticlePipeline.h"
#include "Terrain/LeafField.h"
#include "Terrain/TerrainGrid.h"

#include <cstdint>
#include <d3d12.h>
#include <vector>
#include <wrl/client.h>

namespace Dark
{
    enum class LeafCardPhase : uint8_t
    {
        None = 0,
        Airborne,
        Rest,
        Fade
    };

    struct LeafCardInput
    {
        LeafCardPhase phase  = LeafCardPhase::None;
        float         x      = 0.0f;
        float         y      = 0.0f;
        float         z      = 0.0f;
        float         nx     = 0.0f;
        float         ny     = 1.0f;
        float         nz     = 0.0f;
        float         fade01 = 0.0f;
    };

    // One shared upload, two frames, sized to the leaf pool. Not the blood/scene ring.
    inline constexpr int kLeafQuadCapacity = Terrain::LeafField::kPoolSize;

    // Writes 6 vertices. Airborne cards face the camera. Rested and fading cards lie on the normal.
    // Returns false for an empty phase.
    bool buildLeafCard(const LeafCardInput& leaf, const Math::Vector3f& cameraRight, const Math::Vector3f& cameraUp, ParticleVertex outVerts[6]);

    int buildLeafCards(const LeafCardInput* leaves, int count, const Math::Vector3f& cameraRight, const Math::Vector3f& cameraUp,
                       ParticleVertex* outVerts, int outVertCapacity);

    class Renderer;
    class AssetManager;
    class Camera3D;

    // Trees the foliage draw can see. A non-null host list is that list, kept only on resident tiles.
    // A null list walks resident tiles in the gather disk. Crown height is the tree top above the seat.
    int gatherLeafCrowns(const Terrain::TerrainGrid& grid, const std::vector<Terrain::FoliageRecord>* hostRecords,
                         const Math::Vector3f& camera, const Frustum3f* frustum, float treeTopY, Terrain::LeafField::Crown* out, int cap);

    // Own alpha upload sized to the leaf pool. Init failure logs and leaves draw as a no-op.
    class LeafRenderer
    {
    public:
        static constexpr uint32_t kFrameCount = 2;

        LeafRenderer() = default;

        bool create(Renderer& renderer, AssetManager& assets);
        void destroy(Renderer& renderer);
        bool ready() const { return m_ready; }

        void draw(ID3D12GraphicsCommandList* cmd, const Camera3D& camera, const Terrain::LeafField& field);

    private:
        bool createUpload(Renderer& renderer);

        ParticlePipeline                   m_pipe;
        AssetRef<Material>                 m_sprite;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_upload;
        uint8_t*                           m_mapped   = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS          m_gpu      = 0;
        Renderer*                          m_renderer = nullptr;
        bool                               m_ready = false;
    };

} // namespace Dark
