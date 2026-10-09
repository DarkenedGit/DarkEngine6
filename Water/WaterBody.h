#pragma once

#include "Assets/MeshData.h"
#include "Math/AABox3f.h"
#include "Math/Vector2f.h"
#include "Math/Vector3f.h"
#include "Render/Mesh.h"
#include "Terrain/TerrainLod.h"
#include "Water/WaterWaves.h"

#include <cstdint>
#include <vector>

namespace Dark
{

    class Renderer;
    class WaterPipeline;
    class Camera3D;
    class Frustum3f;
    class ShadowSystem;
    struct SsrSettings;
    struct DebugRenderState;

    namespace Sky
    {
    class Environment;
    }

    namespace Terrain
    {
    class HeightMap;
    }

    // One finite water surface. Position Y is the rest height. extentX/extentZ are the full footprint in meters.
    struct WaterBodyDesc
    {
        Math::Vector3f center{ 0.0f, 0.0f, 0.0f };
        float          extentX = 48.0f;
        float          extentZ = 48.0f;
    };

    // XZ rectangle WaterBody::build keeps. Extents under 4 m become 4 m. False when either extent is over 1024 m.
    bool placedWaterFootprint(float centerX, float centerZ, float extentX, float extentZ, float& minX, float& maxX, float& minZ, float& maxZ);

    // One wet 64 m square, or the single chunk of a body smaller than 64 m on both axes.
    struct WaterBodyChunk
    {
        int               ix        = 0;
        int               iz        = 0;
        int               lod       = 0;
        int               builtLod  = -1;
        uint8_t           builtMask = 0xFF;
        Terrain::EdgeMask edges{};
        MeshData          cpu;
        Mesh              gpu;
        Math::AABox3f     bounds;
    };

    class WaterBody
    {
    public:
        bool build(const Terrain::HeightMap* heightMap, const WaterBodyDesc& desc, const WaterParams& params);
        bool matches(const WaterBodyDesc& desc) const;
        bool bakedTerrain() const { return m_bakedTerrain; }

        void setWaveScales(float amplitudeScale, float speedScale);
        // Copy the sheet look, then put this rectangle's Y back. Does not rebuild the mesh.
        void applySharedParams(const WaterParams& shared);
        WaterParams&       params() { return m_params; }
        const WaterParams& params() const { return m_params; }

        void updateLod(const Math::Vector3f& cameraPos);
        void rebuildDirtyCpuMeshes();
        bool needsRebuild() const;
        bool upload(Renderer& renderer);
        void retireGpu(GpuMeshRetire& retire);
        bool gpuValid() const;

        bool draw(
            ID3D12GraphicsCommandList* cmd,
            WaterPipeline& pipeline,
            const Camera3D& camera,
            const Frustum3f* frustum,
            const DebugRenderState* debug,
            float time,
            uint32_t frameIndex,
            uint32_t drawIndex,
            ID3D12DescriptorHeap* heightHeap,
            D3D12_GPU_DESCRIPTOR_HANDLE heightGpu,
            const ShadowSystem* shadows,
            D3D12_CPU_DESCRIPTOR_HANDLE sceneColorCpu,
            D3D12_CPU_DESCRIPTOR_HANDLE depthCpu,
            const SsrSettings* ssrSettings,
            const Terrain::HeightMap* heightMap,
            const Sky::Environment* env) const;

        int vertexCount() const;
        // LOD 0 of the single chunk. False when the body is more than one chunk.
        bool vertex(int index, Math::Vector3f& outPos, Math::Vector2f& outUv) const;
        bool containsXZ(float x, float z) const;
        const Math::AABox3f& bounds() const { return m_bounds; }

        int chunkCount() const { return static_cast<int>(m_chunks.size()); }
        int chunksX() const { return m_chunksX; }
        int chunksZ() const { return m_chunksZ; }
        const WaterBodyChunk* chunk(int index) const;
        const WaterBodyChunk* chunkAt(int ix, int iz) const;

    private:
        bool buildOneChunk(int ix, int iz);
        bool rebuildOne(WaterBodyChunk& chunk);
        void refreshBounds();

        WaterBodyDesc               m_desc{};
        WaterParams                 m_params{};
        std::vector<WaterBodyChunk> m_chunks;
        std::vector<int>            m_slot;
        int                         m_chunksX    = 0;
        int                         m_chunksZ    = 0;
        int                         m_cells      = 32;
        int                         m_maxLod     = 0;
        float                       m_originX    = 0.0f;
        float                       m_originZ    = 0.0f;
        float                       m_cellMeters = 2.0f;
        float                       m_lodDistances[4]{ 48.0f, 96.0f, 192.0f, 384.0f };
        Math::AABox3f               m_bounds{};
        const Terrain::HeightMap*   m_height       = nullptr;
        bool                        m_bakedTerrain = false;
        bool                        m_small        = false;
        GpuMeshRetire               m_retire;
    };

} // namespace Dark
