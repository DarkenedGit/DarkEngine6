#pragma once

#include "Collision/HitResult.h"
#include "Math/AABox3f.h"
#include "Math/MathDefines.h"
#include "Math/Ray3f.h"
#include "Math/Vector3f.h"
#include "Terrain/HeightMap.h"
#include "Terrain/TerrainTileFile.h"

#include <cstdint>
#include <filesystem>

namespace Dark
{

class Renderer;
class Camera3D;

namespace Terrain
{

struct TerrainGridDesc
{
    uint32_t              tilesX       = 4;
    uint32_t              tilesZ       = 4;
    uint32_t              tileCells    = static_cast<uint32_t>(kTileCells);
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
    bool create(const TerrainGridDesc& desc);

    // CPU residency. renderer may be null (tests / no GPU). GPU upload is a later PR.
    void updateStreaming(const Math::Vector3f& cameraPos, Renderer* renderer);

    float heightAtWorld(float x, float z) const;
    bool  tryHeightAtWorld(float x, float z, float& outY) const;
    bool  containsXZ(float x, float z) const;
    Collision::RayHit3D raycast(const Math::Ray3f& ray, float maxDistance = Math::Infinity) const;
    Math::Vector3f      normalAtWorld(float x, float z) const;

    Math::AABox3f bounds() const;         // full authored AABB
    Math::AABox3f residentBounds() const; // union of loaded fine tiles — NOT CSM
    Math::AABox3f shadowBounds(const Camera3D& camera) const;

    const HeightMap& coarse() const { return m_coarse; }
    HeightMap*       editableWorking() { return nullptr; }

    bool valid() const { return m_valid; }
    int  residentCount() const;
    bool isResident(int tileX, int tileZ) const;
    const HeightMap* residentHeight(int tileX, int tileZ) const;

    uint32_t tilesX() const { return m_tilesX; }
    uint32_t tilesZ() const { return m_tilesZ; }
    int      tileCells() const { return m_tileCells; }
    int      residentRing() const { return m_residentRing; }

private:
    struct TileSlot
    {
        HeightMap height;
        bool      resident      = false;
        bool      missingLogged = false;
    };

    void reset();
    bool worldToTile(float x, float z, int& tx, int& tz) const;
    bool inRing(int tx, int tz, int cx, int cz) const;
    bool loadFineTile(int tx, int tz);
    void evictFineTile(int tx, int tz);
    const HeightMap* heightSourceAt(float x, float z) const;

    bool           m_valid = false;
    uint32_t       m_tilesX = 0;
    uint32_t       m_tilesZ = 0;
    int            m_tileCells    = kTileCells;
    int            m_residentRing = kResidentRingDefault;
    float          m_cellSize     = 1.0f;
    float          m_tileWorld    = 0.0f;
    float          m_worldMaxX    = 0.0f;
    float          m_worldMaxZ    = 0.0f;
    Math::Vector3f m_origin{ 0.0f, 0.0f, 0.0f };
    std::filesystem::path m_tileDir;

    HeightMap m_coarse;
    TileSlot  m_slots[kMaxWorldTiles][kMaxWorldTiles];
};

} // namespace Terrain
} // namespace Dark
