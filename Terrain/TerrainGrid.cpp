#include "Terrain/TerrainGrid.h"
#include "Core/Log.h"
#include "Render/Camera3D.h"
#include "Render/ShadowCascades.h"

#include <cmath>
#include <utility>

namespace Dark
{
namespace Terrain
{

using namespace Math;

namespace
{

bool pathHasDotDot(const std::filesystem::path& path)
{
    for (const std::filesystem::path& part : path)
    {
        if (part == "..")
            return true;
    }
    return false;
}

int clampRing(int ring)
{
    if (ring < 1)
        ring = 1;
    if (ring > kResidentRingMax)
        ring = kResidentRingMax;
    if ((ring & 1) == 0)
        --ring;
    if (ring < 1)
        ring = 1;
    return ring;
}

} // namespace

void TerrainGrid::reset()
{
    m_valid         = false;
    m_tilesX        = 0;
    m_tilesZ        = 0;
    m_tileCells     = kTileCells;
    m_residentRing  = kResidentRingDefault;
    m_cellSize      = 1.0f;
    m_tileWorld     = 0.0f;
    m_worldMaxX     = 0.0f;
    m_worldMaxZ     = 0.0f;
    m_origin        = Vector3f(0.0f, 0.0f, 0.0f);
    m_tileDir.clear();
    m_coarse        = HeightMap{};
    for (uint32_t z = 0; z < kMaxWorldTiles; ++z)
    {
        for (uint32_t x = 0; x < kMaxWorldTiles; ++x)
            m_slots[z][x] = TileSlot{};
    }
}

bool TerrainGrid::create(const TerrainGridDesc& desc)
{
    reset();

    if (desc.tilesX < 1u || desc.tilesX > kMaxWorldTiles || desc.tilesZ < 1u || desc.tilesZ > kMaxWorldTiles)
    {
        DE_LOG_ERROR(LogCategory::Render, "TerrainGrid: tiles {}x{} not in [1, {}]", desc.tilesX, desc.tilesZ, kMaxWorldTiles);
        return false;
    }
    if (desc.cellSize <= 0.0f)
    {
        DE_LOG_ERROR(LogCategory::Render, "TerrainGrid: cellSize must be > 0");
        return false;
    }
    const int tileCells = static_cast<int>(desc.tileCells);
    if (tileCells <= 0 || (tileCells & (tileCells - 1)) != 0 || tileCells + 1 > static_cast<int>(kMaxHeightMapSize))
    {
        DE_LOG_ERROR(LogCategory::Render, "TerrainGrid: tileCells {} invalid", desc.tileCells);
        return false;
    }
    if (pathHasDotDot(desc.tileDir))
    {
        DE_LOG_ERROR(LogCategory::Render, "TerrainGrid: tileDir '{}' rejected", desc.tileDir.string());
        return false;
    }
    if (desc.coarseFile.empty() || pathHasDotDot(desc.coarseFile))
    {
        DE_LOG_ERROR(LogCategory::Render, "TerrainGrid: missing coarse '{}' — create failed", desc.coarseFile.string());
        return false;
    }

    HeightMap coarse;
    if (!coarse.loadBinary(desc.coarseFile))
    {
        DE_LOG_ERROR(LogCategory::Render, "TerrainGrid: missing coarse '{}' — create failed", desc.coarseFile.string());
        return false;
    }

    m_tilesX       = desc.tilesX;
    m_tilesZ       = desc.tilesZ;
    m_tileCells    = tileCells;
    m_residentRing = clampRing(desc.residentRing);
    m_cellSize     = desc.cellSize;
    m_tileWorld    = static_cast<float>(m_tileCells) * m_cellSize;
    m_origin       = desc.origin;
    m_worldMaxX    = m_origin.x + static_cast<float>(m_tilesX) * m_tileWorld;
    m_worldMaxZ    = m_origin.z + static_cast<float>(m_tilesZ) * m_tileWorld;
    m_tileDir      = desc.tileDir;
    m_coarse       = std::move(coarse);
    m_valid        = true;
    return true;
}

bool TerrainGrid::worldToTile(float x, float z, int& tx, int& tz) const
{
    if (!m_valid || m_tileWorld <= 0.0f)
        return false;
    tx = static_cast<int>(floorf((x - m_origin.x) / m_tileWorld));
    tz = static_cast<int>(floorf((z - m_origin.z) / m_tileWorld));
    if (tx < 0)
        tx = 0;
    if (tz < 0)
        tz = 0;
    const int maxX = static_cast<int>(m_tilesX) - 1;
    const int maxZ = static_cast<int>(m_tilesZ) - 1;
    if (tx > maxX)
        tx = maxX;
    if (tz > maxZ)
        tz = maxZ;
    return true;
}

bool TerrainGrid::inRing(int tx, int tz, int cx, int cz) const
{
    const int radius = m_residentRing / 2;
    const int dx     = tx > cx ? tx - cx : cx - tx;
    const int dz     = tz > cz ? tz - cz : cz - tz;
    return (dx > dz ? dx : dz) <= radius;
}

bool TerrainGrid::loadFineTile(int tx, int tz)
{
    TileSlot& slot = m_slots[tz][tx];
    if (slot.resident)
        return true;
    if (m_tileDir.empty())
    {
        if (!slot.missingLogged)
        {
            DE_LOG_ERROR(LogCategory::Render, "TerrainGrid: missing tile ({},{}) — coarse fallback", tx, tz);
            slot.missingLogged = true;
        }
        return false;
    }

    const std::filesystem::path path = tileHeightPath(m_tileDir, tx, tz);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec)
    {
        if (!slot.missingLogged)
        {
            DE_LOG_ERROR(LogCategory::Render, "TerrainGrid: missing tile ({},{}) — coarse fallback", tx, tz);
            slot.missingLogged = true;
        }
        return false;
    }

    HeightMap hm;
    if (!hm.loadBinary(path))
    {
        if (!slot.missingLogged)
        {
            DE_LOG_ERROR(LogCategory::Render, "TerrainGrid: missing tile ({},{}) — coarse fallback", tx, tz);
            slot.missingLogged = true;
        }
        return false;
    }

    const int samples = m_tileCells + 1;
    if (static_cast<int>(hm.width()) != samples || static_cast<int>(hm.height()) != samples)
    {
        if (!slot.missingLogged)
        {
            DE_LOG_ERROR(LogCategory::Render, "TerrainGrid: missing tile ({},{}) — coarse fallback", tx, tz);
            slot.missingLogged = true;
        }
        return false;
    }

    slot.height   = std::move(hm);
    slot.resident = true;
    return true;
}

void TerrainGrid::evictFineTile(int tx, int tz)
{
    TileSlot& slot = m_slots[tz][tx];
    if (!slot.resident)
        return;
    slot.height   = HeightMap{};
    slot.resident = false;
}

void TerrainGrid::updateStreaming(const Vector3f& cameraPos, Renderer* renderer)
{
    (void)renderer; // GPU apply is PR5; null renderer is the CPU/test path
    if (!m_valid)
        return;

    int cx = 0;
    int cz = 0;
    worldToTile(cameraPos.x, cameraPos.z, cx, cz);

    int loaded  = 0;
    int evicted = 0;
    for (int tz = 0; tz < static_cast<int>(m_tilesZ); ++tz)
    {
        for (int tx = 0; tx < static_cast<int>(m_tilesX); ++tx)
        {
            const bool want = inRing(tx, tz, cx, cz);
            TileSlot&  slot = m_slots[tz][tx];
            if (want)
            {
                if (!slot.resident && loadFineTile(tx, tz))
                    ++loaded;
            }
            else if (slot.resident)
            {
                evictFineTile(tx, tz);
                ++evicted;
            }
        }
    }

    if (loaded > 0 || evicted > 0)
    {
        DE_LOG_INFO(
            LogCategory::Render,
            "TerrainGrid: resident {} tiles, stream queue {}",
            residentCount(),
            0);
    }
}

bool TerrainGrid::containsXZ(float x, float z) const
{
    if (!m_valid)
        return false;
    return x >= m_origin.x && x <= m_worldMaxX && z >= m_origin.z && z <= m_worldMaxZ;
}

const HeightMap* TerrainGrid::heightSourceAt(float x, float z) const
{
    int tx = 0;
    int tz = 0;
    if (!worldToTile(x, z, tx, tz))
        return &m_coarse;
    const TileSlot& slot = m_slots[tz][tx];
    if (slot.resident && slot.height.valid())
        return &slot.height;
    return &m_coarse;
}

float TerrainGrid::heightAtWorld(float x, float z) const
{
    if (!m_valid)
        return 0.0f;
    float y = 0.0f;
    if (tryHeightAtWorld(x, z, y))
        return y;
    return m_coarse.heightAtWorld(x, z);
}

bool TerrainGrid::tryHeightAtWorld(float x, float z, float& outY) const
{
    if (!containsXZ(x, z))
        return false;
    outY = heightSourceAt(x, z)->heightAtWorld(x, z);
    return true;
}

Collision::RayHit3D TerrainGrid::raycast(const Ray3f& ray, float maxDistance) const
{
    Collision::RayHit3D best{};
    if (!m_valid)
        return best;

    const Collision::RayHit3D coarseHit = m_coarse.raycast(ray, maxDistance);
    if (coarseHit.hit)
    {
        int tx = 0;
        int tz = 0;
        worldToTile(coarseHit.point.x, coarseHit.point.z, tx, tz);
        if (!isResident(tx, tz))
            best = coarseHit;
    }

    for (int tz = 0; tz < static_cast<int>(m_tilesZ); ++tz)
    {
        for (int tx = 0; tx < static_cast<int>(m_tilesX); ++tx)
        {
            const TileSlot& slot = m_slots[tz][tx];
            if (!slot.resident || !slot.height.valid())
                continue;
            const Collision::RayHit3D hit = slot.height.raycast(ray, maxDistance);
            if (hit.hit && (!best.hit || hit.t < best.t))
                best = hit;
        }
    }
    return best;
}

Vector3f TerrainGrid::normalAtWorld(float x, float z) const
{
    if (!m_valid)
        return Vector3f(0.0f, 1.0f, 0.0f);
    if (!containsXZ(x, z))
        return m_coarse.normalAtWorld(x, z);
    return heightSourceAt(x, z)->normalAtWorld(x, z);
}

AABox3f TerrainGrid::bounds() const
{
    if (!m_valid)
        return AABox3f::Empty();
    AABox3f box = m_coarse.bounds();
    if (!box.IsValid())
    {
        box.Min = Vector3f(m_origin.x, m_origin.y, m_origin.z);
        box.Max = Vector3f(m_worldMaxX, m_origin.y, m_worldMaxZ);
    }
    box.Min.x = m_origin.x;
    box.Min.z = m_origin.z;
    box.Max.x = m_worldMaxX;
    box.Max.z = m_worldMaxZ;
    return box;
}

AABox3f TerrainGrid::residentBounds() const
{
    AABox3f box = AABox3f::Empty();
    if (!m_valid)
        return box;
    for (int tz = 0; tz < static_cast<int>(m_tilesZ); ++tz)
    {
        for (int tx = 0; tx < static_cast<int>(m_tilesX); ++tx)
        {
            const TileSlot& slot = m_slots[tz][tx];
            if (!slot.resident || !slot.height.valid())
                continue;
            box.ExpandToInclude(slot.height.bounds());
        }
    }
    return box;
}

AABox3f TerrainGrid::shadowBounds(const Camera3D& camera) const
{
    if (!m_valid)
        return AABox3f::Empty();

    const ShadowSettings settings{};
    const float          half = settings.maxDistance + settings.casterMargin;
    const Vector3f       p    = camera.GetPosition();

    AABox3f yBox = m_coarse.bounds();
    float   yMin = yBox.IsValid() ? yBox.Min.y : m_origin.y;
    float   yMax = yBox.IsValid() ? yBox.Max.y : m_origin.y;
    yMin -= settings.casterMargin;
    yMax += settings.casterMargin;

    return AABox3f(
        Vector3f(p.x - half, yMin, p.z - half),
        Vector3f(p.x + half, yMax, p.z + half));
}

int TerrainGrid::residentCount() const
{
    int n = 0;
    if (!m_valid)
        return 0;
    for (int tz = 0; tz < static_cast<int>(m_tilesZ); ++tz)
    {
        for (int tx = 0; tx < static_cast<int>(m_tilesX); ++tx)
        {
            if (m_slots[tz][tx].resident)
                ++n;
        }
    }
    return n;
}

bool TerrainGrid::isResident(int tileX, int tileZ) const
{
    if (!m_valid || tileX < 0 || tileZ < 0)
        return false;
    if (tileX >= static_cast<int>(m_tilesX) || tileZ >= static_cast<int>(m_tilesZ))
        return false;
    return m_slots[tileZ][tileX].resident;
}

const HeightMap* TerrainGrid::residentHeight(int tileX, int tileZ) const
{
    if (!isResident(tileX, tileZ))
        return nullptr;
    const HeightMap& hm = m_slots[tileZ][tileX].height;
    return hm.valid() ? &hm : nullptr;
}

} // namespace Terrain
} // namespace Dark
