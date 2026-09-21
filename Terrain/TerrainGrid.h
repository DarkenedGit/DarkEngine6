#pragma once

#include "Collision/HitResult.h"
#include "Math/AABox3f.h"
#include "Math/MathDefines.h"
#include "Math/Ray3f.h"
#include "Math/Vector3f.h"
#include "Render/PackedSrvHeap.h"
#include "Render/Texture2D.h"
#include "Terrain/SplatMap.h"
#include "Terrain/Terrain.h"
#include "Terrain/TerrainTileFile.h"

#include <cstdint>
#include <filesystem>

namespace Dark
{

class Renderer;
class Camera3D;
class TerrainPipeline;
class TerrainMaterial;
class ShadowSystem;
class Frustum3f;
class GpuResourceCache;

namespace Sky
{
class Environment;
}

namespace Terrain
{

struct TerrainGridDesc
{
    uint32_t              tilesX       = 4;
    uint32_t              tilesZ       = 4;
    uint32_t              tileCells    = static_cast<uint32_t>(kTileCells);
    int                   chunkCells   = 64; // streamed default; clamped to tileCells
    float                 cellSize     = 1.0f;
    float                 heightScale  = 80.0f;
    Math::Vector3f        origin{ -1024.0f, 0.0f, -1024.0f };
    std::filesystem::path coarseFile;
    std::filesystem::path tileDir;
    int                   residentRing = kResidentRingDefault;
};

// Streamed tile world. Coarse HF is always resident; fine tiles load in a camera-centered ring.
// Queries use fine if resident, else coarse — never 0 for in-world XZ because a tile is unloaded.
class TerrainGrid
{
public:
    TerrainGrid() = default;
    ~TerrainGrid();

    TerrainGrid(const TerrainGrid&)            = delete;
    TerrainGrid& operator=(const TerrainGrid&) = delete;

    bool create(const TerrainGridDesc& desc);
    // In-memory coarse (Editor generate / 129 FBM). coarseFile may be empty.
    bool createFromCoarse(const TerrainGridDesc& desc, HeightMap&& coarse);
    // Legacy / tests / 129 FBM: one tile, tileCells = width-1 (power of two).
    bool createFromHeightMap(HeightMap&& map, int chunkCells = 16);

    void clear();

    // Editor CPU source of truth. Runtime leaves these null.
    bool setWorking(HeightMap&& height, SplatMap&& splat);
    bool setWorkingSplat(SplatMap&& splat);
    HeightMap*       editableWorking();
    const HeightMap* editableWorking() const;
    SplatMap*        editableWorkingSplat();
    const SplatMap*  editableWorkingSplat() const;
    bool boxFilterCoarseFromWorking();
    // Copy working samples/splat into overlapping resident tiles; box-filter coarse.
    void applyWorkingRect(int x0, int z0, int x1, int z1, bool heights, bool splat);
    // Load every fine tile into working (Editor hitch OK). Runtime must not call this.
    bool assembleWorking();

    // Player 3×3 pin (odd ring). Combined with the camera stream ring.
    void pinWorldXZ(float x, float z, int ring = 3);
    void clearPin();

    // CPU residency + optional GPU apply. renderer may be null (tests).
    // material is the 14-slot template (layers 0–11); each resident tile packs its own heap.
    void updateStreaming(const Math::Vector3f& cameraPos, Renderer* renderer);
    void updateStreaming(const Math::Vector3f& cameraPos, Renderer* renderer, const TerrainMaterial* material);

    void setLodDistances(const float* distances, int count);
    void updateLod(const Math::Vector3f& cameraPos);

    float heightAtWorld(float x, float z) const;
    bool  tryHeightAtWorld(float x, float z, float& outY) const;
    bool  containsXZ(float x, float z) const;
    Collision::RayHit3D raycast(const Math::Ray3f& ray, float maxDistance = Math::Infinity) const;
    Math::Vector3f      normalAtWorld(float x, float z) const;

    Math::AABox3f bounds() const;         // full authored AABB
    Math::AABox3f residentBounds() const; // union of loaded fine tiles — NOT CSM
    Math::AABox3f shadowBounds(const Camera3D& camera) const;

    const HeightMap& coarse() const { return m_coarse; }

    // Coarse R32F for fog/water setHeightSrv. Never a per-tile height SRV.
    const Texture2D& heightTexture() const { return m_heightTexture; }
    bool uploadCoarseHeightTexture(Renderer& renderer);

    void draw(
        ID3D12GraphicsCommandList* cmd,
        const TerrainPipeline& pipeline,
        const TerrainMaterial& material,
        const Camera3D& camera,
        const Frustum3f* frustum = nullptr,
        const Sky::Environment* env = nullptr,
        const ShadowSystem* shadows = nullptr,
        const DebugRenderState* debug = nullptr) const;

    void drawGBuffer(
        ID3D12GraphicsCommandList* cmd,
        const TerrainPipeline& pipeline,
        const TerrainMaterial& material,
        const Camera3D& camera,
        const Frustum3f* frustum = nullptr,
        const DebugRenderState* debug = nullptr,
        const Math::Matrix4f* prevViewProj = nullptr) const;

    void drawDepth(ID3D12GraphicsCommandList* cmd, const Frustum3f* casterFrustum = nullptr) const;

    bool valid() const { return m_valid; }
    int  residentCount() const;
    bool isResident(int tileX, int tileZ) const;
    const HeightMap* residentHeight(int tileX, int tileZ) const;
    const TerrainWorld* residentWorld(int tileX, int tileZ) const;
    const PackedSrvHeap* residentPackedHeap(int tileX, int tileZ) const;

    // Pack that tile's 14-slot table and copySplat into slot 12. Device may be null (CPU tests).
    bool packTileSplat(
        int tileX,
        int tileZ,
        D3D12_CPU_DESCRIPTOR_HANDLE splatCpu,
        Renderer* renderer = nullptr,
        const TerrainMaterial* material = nullptr);

    uint32_t tilesX() const { return m_tilesX; }
    uint32_t tilesZ() const { return m_tilesZ; }
    int      tileCells() const { return m_tileCells; }
    int      chunkCells() const { return m_chunkCells; }
    int      residentRing() const { return m_residentRing; }
    float    cellSize() const { return m_cellSize; }
    float    heightScale() const { return m_coarse.valid() ? m_coarse.heightScale() : 1.0f; }
    const Math::Vector3f& origin() const { return m_origin; }
    uint32_t lastDrawCalls() const;
    uint32_t lastTriangles() const;

    // Recopy layers 0–11 from the template onto every resident tile heap (Editor layer edits).
    void rebindResidentHeaps(Renderer& renderer, const TerrainMaterial& material);

private:
    struct TileSlot
    {
        TerrainWorld     world;
        PackedSrvHeap    heap;
        Texture2D        splatTexture;
        SplatMap         splat;
        GpuResourceCache* cache = nullptr;
        bool             resident           = false;
        bool             missingLogged      = false;
        bool             firstGpuApplyDone  = false;
        bool             heapPacked         = false;
    };

    void reset();
    bool configure(const TerrainGridDesc& desc, HeightMap&& coarse);
    bool worldToTile(float x, float z, int& tx, int& tz) const;
    bool inRing(int tx, int tz, int cx, int cz) const;
    bool inPinRing(int tx, int tz) const;
    bool loadFineTile(int tx, int tz);
    bool sliceWorkingTile(int tx, int tz);
    bool copyWorkingTileHeight(int tx, int tz, HeightMap& out) const;
    bool copyWorkingTileSplat(int tx, int tz, SplatMap& out) const;
    void evictFineTile(int tx, int tz);
    void unregisterTileHeap(TileSlot& slot);
    const HeightMap* heightSourceAt(float x, float z) const;
    bool packTileHeapGpu(Renderer& renderer, TileSlot& slot, const TerrainMaterial& material);
    void applyGpu(Renderer& renderer, const TerrainMaterial* material);
    void boxFilterCoarseFrom(const HeightMap& src);

    bool           m_valid = false;
    uint32_t       m_tilesX = 0;
    uint32_t       m_tilesZ = 0;
    int            m_tileCells    = kTileCells;
    int            m_chunkCells   = 64;
    int            m_residentRing = kResidentRingDefault;
    float          m_cellSize     = 1.0f;
    float          m_tileWorld    = 0.0f;
    float          m_worldMaxX    = 0.0f;
    float          m_worldMaxZ    = 0.0f;
    Math::Vector3f m_origin{ 0.0f, 0.0f, 0.0f };
    std::filesystem::path m_tileDir;
    float          m_lodDistances[kMaxLodLevels]{ 48.0f, 96.0f, 192.0f, 384.0f, 768.0f, 1536.0f, 3072.0f, 6144.0f };
    int            m_lodDistanceCount = 5;
    bool           m_loggedStreamingIn = false;

    HeightMap m_coarse;
    Texture2D m_heightTexture;
    HeightMap m_working;
    SplatMap  m_workingSplat;
    bool      m_pinActive = false;
    int       m_pinTx     = 0;
    int       m_pinTz     = 0;
    int       m_pinRing   = 3;
    TileSlot  m_slots[kMaxWorldTiles][kMaxWorldTiles];
    GpuMeshRetire m_gpuRetire;
};

} // namespace Terrain
} // namespace Dark
