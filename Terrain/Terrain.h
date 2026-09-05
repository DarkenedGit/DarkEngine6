#pragma once

#include "Math/AABox3f.h"
#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"
#include "Render/Mesh.h"
#include "Render/DebugRenderState.h"
#include "Terrain/HeightMap.h"
#include "Terrain/SplatMap.h"
#include "Terrain/TerrainLod.h"

#include <cstdint>
#include <vector>

namespace Dark
{

class Renderer;
class TerrainPipeline;
class TerrainMaterial;
class Camera3D;
class ShadowSystem;

namespace Sky
{
class Environment;
}

namespace Math
{
class Frustum3f;
}

namespace Terrain
{

struct TerrainDesc
{
    HeightMap heightMap;
    int       chunkCells = 16;
    float     lodDistances[kMaxLodLevels]{ 48.0f, 96.0f, 192.0f, 384.0f, 768.0f, 1536.0f, 3072.0f, 6144.0f };
    int       lodDistanceCount = 5;
};

struct TerrainChunk
{
    int              ix = 0;
    int              iz = 0;
    int              lod = 0;
    EdgeMask         edges{};
    int              builtLod = -1;
    uint8_t          builtMask = 0xFF;
    MeshData         cpu;
    Mesh             gpu;
    Math::Aabb3f     bounds;
};

// Chunked geomipmap terrain. CPU LOD / welding is independent of D3D;
// call createGpu / uploadDirty / draw after the height field is ready.
class TerrainWorld
{
public:
    bool create(TerrainDesc desc);

    void updateLod(const Math::Vector3f& cameraPos);
    void rebuildDirtyCpuMeshes();
    bool needsRebuild() const;
    bool createGpu(Renderer& renderer);
    bool uploadDirty(Renderer& renderer);

    void draw(
        ID3D12GraphicsCommandList* cmd,
        const TerrainPipeline& pipeline,
        const TerrainMaterial& material,
        const Camera3D& camera,
        const Math::Frustum3f* frustum = nullptr,
        const Sky::Environment* env = nullptr,
        const ShadowSystem* shadows = nullptr,
        const DebugRenderState* debug = nullptr) const;

    void drawGBuffer(
        ID3D12GraphicsCommandList* cmd,
        const TerrainPipeline& pipeline,
        const TerrainMaterial& material,
        const Camera3D& camera,
        const Math::Frustum3f* frustum = nullptr,
        const DebugRenderState* debug = nullptr,
        const Math::Matrix4f* prevViewProj = nullptr) const;

    // Depth-only casters. Caller binds ShadowPipeline and sets light WVP.
    // Pass the cascade clip frustum so chunks outside this slice are skipped
    // (otherwise every cascade is a sun's-eye view of the whole terrain).
    void drawDepth(ID3D12GraphicsCommandList* cmd, const Math::Frustum3f* casterFrustum = nullptr) const;

    float heightAtWorld(float x, float z) const;
    bool tryHeightAtWorld(float x, float z, float& outY) const;
    bool containsXZ(float x, float z) const;
    Collision::RayHit3D raycast(const Math::Ray3f& ray, float maxDistance = Math::Infinity) const;
    Math::Vector3f normalAtWorld(float x, float z) const;
    Math::Aabb3f bounds() const { return m_bounds; }

    const HeightMap& heightMap() const { return m_heightMap; }
    HeightMap&       heightMap()       { return m_heightMap; }

    int chunksX() const { return m_chunksX; }
    int chunksZ() const { return m_chunksZ; }
    int chunkCells() const { return m_chunkCells; }
    int maxLod() const { return m_maxLod; }
    int chunkCount() const { return static_cast<int>(m_chunks.size()); }
    const TerrainChunk* chunk(int ix, int iz) const;

    uint32_t lastDrawCalls() const { return m_lastDrawCalls; }
    uint32_t lastTriangles() const { return m_lastTriangles; }

private:
    TerrainChunk* chunkAt(int ix, int iz);

    HeightMap m_heightMap;
    int       m_chunkCells = 16;
    int       m_chunksX    = 0;
    int       m_chunksZ    = 0;
    int       m_maxLod     = 0;
    float     m_lodDistances[kMaxLodLevels]{};
    int       m_lodDistanceCount = 0;
    Math::Aabb3f m_bounds;

    std::vector<TerrainChunk> m_chunks;
    std::vector<int>          m_lods;

    mutable uint32_t m_lastDrawCalls = 0;
    mutable uint32_t m_lastTriangles = 0;
};

} // namespace Terrain
} // namespace Dark
