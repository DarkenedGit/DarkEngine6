#pragma once

#include "Geometry/Mesh.h"
#include "Math/AABox3f.h"
#include "Math/MathDefines.h"
#include "Math/Vector3f.h"
#include "Terrain/TerrainLod.h"
#include "Water/WaterWaves.h"

#include <cstdint>
#include <vector>

namespace Dark
{

class Renderer;
class WaterPipeline;
class Camera3D;

namespace Sky
{
class Environment;
}

namespace Math
{
class Frustum3f;
class Ray3f;
}

namespace Terrain
{
class HeightMap;
}

namespace Water
{

struct WaterDesc
{
    float waterLevel = 0.0f;
    int   chunkCells = 16;
    float lodDistances[Terrain::kMaxLodLevels]{ 40.0f, 80.0f, 160.0f, 320.0f, 640.0f, 1280.0f, 2560.0f, 5120.0f };
    int   lodDistanceCount = 5;
    WaterParams params{};
};

struct WaterChunk
{
    int                ix = 0;
    int                iz = 0;
    bool               wet = false;
    int                lod = 0;
    Terrain::EdgeMask  edges{};
    int                builtLod = -1;
    uint8_t            builtMask = 0xFF;
    Geometry::MeshData cpu;
    Geometry::Mesh     gpu;
    Math::Aabb3f       bounds;
};

// Chunked water surface at a world water level. Patches exist only where the
// height map dips below the water (valleys). LOD matches terrain welding.
class WaterWorld
{
public:
    bool create(const Terrain::HeightMap& heightMap, WaterDesc desc);

    void setParams(const WaterParams& params) { m_params = params; }
    const WaterParams& params() const { return m_params; }
    WaterParams&       params()       { return m_params; }

    void tick(float dt);
    float time() const { return m_time; }
    void setTime(float t) { m_time = t; }

    void updateLod(const Math::Vector3f& cameraPos);
    void rebuildDirtyCpuMeshes();
    bool needsRebuild() const;
    bool createGpu(Renderer& renderer);
    bool uploadDirty(Renderer& renderer);

    void draw(
        ID3D12GraphicsCommandList* cmd,
        const WaterPipeline& pipeline,
        const Camera3D& camera,
        const Math::Frustum3f* frustum = nullptr,
        const Sky::Environment* env = nullptr) const;

    float heightAtWorld(float x, float z) const;
    bool  tryHeightAtWorld(float x, float z, float& outY) const;

    int  chunksX() const { return m_chunksX; }
    int  chunksZ() const { return m_chunksZ; }
    int  chunkCells() const { return m_chunkCells; }
    int  wetChunkCount() const;
    const WaterChunk* chunk(int ix, int iz) const;

    uint32_t lastDrawCalls() const { return m_lastDrawCalls; }
    uint32_t lastTriangles() const { return m_lastTriangles; }

private:
    bool chunkIsWet(const Terrain::HeightMap& hm, int ix, int iz) const;
    bool buildChunkMesh(WaterChunk& c);

    const Terrain::HeightMap* m_heightMap = nullptr;
    WaterParams m_params;
    int         m_chunkCells = 16;
    int         m_chunksX    = 0;
    int         m_chunksZ    = 0;
    int         m_maxLod     = 0;
    float       m_lodDistances[Terrain::kMaxLodLevels]{};
    int         m_lodDistanceCount = 0;
    float       m_time = 0.0f;

    std::vector<WaterChunk> m_chunks;
    std::vector<int>        m_lods;

    mutable uint32_t m_lastDrawCalls = 0;
    mutable uint32_t m_lastTriangles = 0;
};

} // namespace Water
} // namespace Dark
