#include "Terrain/TerrainGrid.h"
#include "Assets/Image.h"
#include "Core/Log.h"
#include "Render/Camera3D.h"
#include "Render/Frustum3f.h"
#include "Render/GpuResourceCache.h"
#include "Render/Renderer.h"
#include "Render/ShadowCascades.h"
#include "Render/TerrainPipeline.h"
#include "Sky/Environment.h"
#include "Terrain/TerrainMaterial.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace Dark
{
namespace Terrain
{

using namespace Math;

namespace
{

constexpr float kSteadyStateGpuMs = 4.0f;

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

int pickChunkCells(int requested, int tileCells)
{
    // One patch per tile. Interior 64-cell chunks drew a shared edge twice
    // (z-fight) and welded LOD grooves on a 64 m grid.
    (void)requested;
    int c = tileCells;
    if (c < 1)
        c = 1;
    if (!isPowerOfTwo(c))
    {
        while (c > 1 && !isPowerOfTwo(c))
            --c;
    }
    return c;
}

} // namespace

TerrainGrid::~TerrainGrid()
{
    reset();
}

void TerrainGrid::transferRetireToRenderer(Renderer* renderer)
{
    if (!renderer)
        return;
    for (auto& item : m_gpuRetire.m_items)
    {
        Mesh& mesh = item.mesh;
        if (mesh.valid())
        {
            if (mesh.m_vb)
                renderer->deferRelease(mesh.m_vb.Get());
            if (mesh.m_ib)
                renderer->deferRelease(mesh.m_ib.Get());
        }
    }
    m_gpuRetire.clear();
    for (auto& item : m_heapRetire.m_items)
    {
        if (item.heap)
            renderer->deferRelease(item.heap.Get());
    }
    m_heapRetire.clear();
    for (auto& item : m_textureRetire.m_items)
    {
        Texture2D& tex = item.texture;
        if (tex.valid())
        {
            if (tex.m_resource)
                renderer->deferRelease(tex.m_resource.Get());
            if (tex.m_cpuSrvHeap)
                renderer->deferRelease(tex.m_cpuSrvHeap.Get());
            if (tex.m_srvHeap)
                renderer->deferRelease(tex.m_srvHeap.Get());
        }
    }
    m_textureRetire.clear();
}

void TerrainGrid::reset()
{
    for (uint32_t z = 0; z < kMaxWorldTiles; ++z)
    {
        for (uint32_t x = 0; x < kMaxWorldTiles; ++x)
            evictFineTile(static_cast<int>(x), static_cast<int>(z));
    }
    m_valid              = false;
    m_tilesX             = 0;
    m_tilesZ             = 0;
    m_tileCells          = kTileCells;
    m_chunkCells         = 64;
    m_residentRing       = kResidentRingDefault;
    m_cellSize           = 1.0f;
    m_tileWorld          = 0.0f;
    m_worldMaxX          = 0.0f;
    m_worldMaxZ          = 0.0f;
    m_origin             = Vector3f(0.0f, 0.0f, 0.0f);
    m_tileDir.clear();
    m_lodDistanceCount   = 5;
    m_loggedStreamingIn  = false;
    m_coarse             = HeightMap{};
    m_heightTexture      = Texture2D{};
    m_working            = HeightMap{};
    m_workingSplat       = SplatMap{};
    m_pinActive          = false;
    m_pinTx              = 0;
    m_pinTz              = 0;
    m_pinRing            = 3;
}

void TerrainGrid::clear()
{
    reset();
}

bool TerrainGrid::configure(const TerrainGridDesc& desc, HeightMap&& coarse)
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
    if (!coarse.valid())
    {
        DE_LOG_ERROR(LogCategory::Render, "TerrainGrid: missing coarse '{}' — create failed", desc.coarseFile.string());
        return false;
    }

    m_tilesX       = desc.tilesX;
    m_tilesZ       = desc.tilesZ;
    m_tileCells    = tileCells;
    m_chunkCells   = pickChunkCells(desc.chunkCells, tileCells);
    m_residentRing = clampRing(desc.residentRing);
    m_cellSize     = desc.cellSize;
    m_tileWorld    = static_cast<float>(m_tileCells) * m_cellSize;
    m_origin       = desc.origin;
    m_worldMaxX    = m_origin.x + static_cast<float>(m_tilesX) * m_tileWorld;
    m_worldMaxZ    = m_origin.z + static_cast<float>(m_tilesZ) * m_tileWorld;
    m_tileDir      = desc.tileDir;
    m_coarse       = std::move(coarse);
    m_valid        = true;
    {
        const float unit = m_cellSize * static_cast<float>(m_chunkCells);
        m_lodDistanceCount = 5;
        for (int i = 0; i < 5; ++i)
            m_lodDistances[i] = unit * static_cast<float>(2 << i);
    }
    return true;
}

bool TerrainGrid::create(const TerrainGridDesc& desc)
{
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
    return configure(desc, std::move(coarse));
}

bool TerrainGrid::createFromCoarse(const TerrainGridDesc& desc, HeightMap&& coarse)
{
    return configure(desc, std::move(coarse));
}

bool TerrainGrid::createFromHeightMap(HeightMap&& map, int chunkCells)
{
    if (!map.valid() || map.width() != map.height() || map.width() < 2u)
    {
        DE_LOG_ERROR(LogCategory::Render, "TerrainGrid: createFromHeightMap needs a square HF");
        return false;
    }
    const int tileCells = static_cast<int>(map.width()) - 1;
    if (tileCells <= 0 || (tileCells & (tileCells - 1)) != 0 || tileCells + 1 > static_cast<int>(kMaxHeightMapSize))
    {
        DE_LOG_ERROR(LogCategory::Render, "TerrainGrid: createFromHeightMap size {} invalid", map.width());
        return false;
    }

    TerrainGridDesc desc{};
    desc.tilesX       = 1;
    desc.tilesZ       = 1;
    desc.tileCells    = static_cast<uint32_t>(tileCells);
    desc.chunkCells   = chunkCells;
    desc.cellSize     = map.cellSize();
    desc.heightScale  = map.heightScale();
    desc.origin       = map.origin();
    desc.residentRing = kResidentRingDefault;

    HeightMap coarse = map;
    if (!configure(desc, std::move(coarse)))
        return false;
    m_working = std::move(map);
    return true;
}

void TerrainGrid::setLodDistances(const float* distances, int count)
{
    if (!distances || count < 1)
        return;
    m_lodDistanceCount = count;
    if (m_lodDistanceCount > kMaxLodLevels)
        m_lodDistanceCount = kMaxLodLevels;
    std::memcpy(m_lodDistances, distances, sizeof(float) * static_cast<size_t>(m_lodDistanceCount));
}

HeightMap* TerrainGrid::editableWorking()
{
    return m_working.valid() ? &m_working : nullptr;
}

const HeightMap* TerrainGrid::editableWorking() const
{
    return m_working.valid() ? &m_working : nullptr;
}

SplatMap* TerrainGrid::editableWorkingSplat()
{
    return m_workingSplat.valid() ? &m_workingSplat : nullptr;
}

const SplatMap* TerrainGrid::editableWorkingSplat() const
{
    return m_workingSplat.valid() ? &m_workingSplat : nullptr;
}

bool TerrainGrid::setWorking(HeightMap&& height, SplatMap&& splat)
{
    if (!m_valid || !height.valid())
        return false;
    m_working      = std::move(height);
    m_workingSplat = std::move(splat);
    for (uint32_t z = 0; z < m_tilesZ; ++z)
    {
        for (uint32_t x = 0; x < m_tilesX; ++x)
        {
            if (m_slots[z][x].resident)
                m_slots[z][x].world.setNormalHeightMap(&m_working);
        }
    }
    return true;
}

bool TerrainGrid::setWorkingSplat(SplatMap&& splat)
{
    if (!m_valid || !splat.valid())
        return false;
    m_workingSplat = std::move(splat);
    return true;
}

void TerrainGrid::pinWorldXZ(float x, float z, int ring)
{
    int tx = 0;
    int tz = 0;
    if (!worldToTile(x, z, tx, tz))
    {
        m_pinActive = false;
        return;
    }
    m_pinActive = true;
    m_pinTx     = tx;
    m_pinTz     = tz;
    m_pinRing   = clampRing(ring);
}

void TerrainGrid::clearPin()
{
    m_pinActive = false;
}

bool TerrainGrid::inPinRing(int tx, int tz) const
{
    if (!m_pinActive)
        return false;
    const int radius = m_pinRing / 2;
    const int dx     = tx > m_pinTx ? tx - m_pinTx : m_pinTx - tx;
    const int dz     = tz > m_pinTz ? tz - m_pinTz : m_pinTz - tz;
    return (dx > dz ? dx : dz) <= radius;
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

bool TerrainGrid::copyWorkingTileHeight(int tx, int tz, HeightMap& out) const
{
    if (!m_working.valid())
        return false;
    const int samples = m_tileCells + 1;
    const int srcX    = tx * m_tileCells;
    const int srcZ    = tz * m_tileCells;
    if (srcX < 0 || srcZ < 0)
        return false;
    if (srcX + m_tileCells >= static_cast<int>(m_working.width()) || srcZ + m_tileCells >= static_cast<int>(m_working.height()))
        return false;
    if (!out.create(static_cast<uint32_t>(samples), static_cast<uint32_t>(samples), m_working.cellSize(), m_working.heightScale()))
        return false;
    out.setOrigin(Vector3f(
        m_working.origin().x + static_cast<float>(srcX) * m_working.cellSize(),
        m_working.origin().y,
        m_working.origin().z + static_cast<float>(srcZ) * m_working.cellSize()));
    for (int z = 0; z < samples; ++z)
    {
        for (int x = 0; x < samples; ++x)
            out.setHeight(x, z, m_working.height(srcX + x, srcZ + z));
    }
    return true;
}

bool TerrainGrid::copyWorkingTileSplat(int tx, int tz, SplatMap& out) const
{
    if (!m_workingSplat.valid())
        return false;
    const int samples = m_tileCells + 1;
    const int srcX    = tx * m_tileCells;
    const int srcZ    = tz * m_tileCells;
    if (srcX + m_tileCells >= static_cast<int>(m_workingSplat.width()) || srcZ + m_tileCells >= static_cast<int>(m_workingSplat.height()))
        return false;
    if (!out.create(static_cast<uint32_t>(samples), static_cast<uint32_t>(samples)))
        return false;
    for (int z = 0; z < samples; ++z)
    {
        for (int x = 0; x < samples; ++x)
        {
            uint8_t c[4]{};
            m_workingSplat.getTexel(srcX + x, srcZ + z, c);
            out.setTexel(x, z, c[0], c[1], c[2], c[3]);
        }
    }
    return true;
}

bool TerrainGrid::sliceWorkingTile(int tx, int tz)
{
    HeightMap hm;
    if (!copyWorkingTileHeight(tx, tz, hm))
        return false;

    TerrainDesc desc;
    desc.heightMap        = std::move(hm);
    desc.chunkCells       = m_chunkCells;
    desc.lodDistanceCount = m_lodDistanceCount;
    std::memcpy(desc.lodDistances, m_lodDistances, sizeof(float) * kMaxLodLevels);
    TileSlot& slot = m_slots[tz][tx];
    unregisterTileHeap(slot);
    if (!slot.world.create(std::move(desc)))
        return false;
    slot.world.setUploadHeightTexture(false);
    slot.world.setNormalHeightMap(m_working.valid() ? &m_working : nullptr);
    if (!copyWorkingTileSplat(tx, tz, slot.splat))
    {
        // Tile-local generateFromHeight uses clamped edge normals and paints a
        // rock rim on every 512 m border. Prefer the full working map.
        if (m_working.valid() && !m_workingSplat.valid())
            m_workingSplat.generateFromHeight(m_working);
        if (!copyWorkingTileSplat(tx, tz, slot.splat))
            slot.splat.generateFromHeight(slot.world.heightMap());
    }
    slot.resident          = true;
    slot.firstGpuApplyDone = false;
    slot.heapPacked        = false;
    slot.missingLogged     = false;
    return true;
}

void TerrainGrid::boxFilterCoarseFrom(const HeightMap& src)
{
    if (!src.valid())
        return;
    const uint32_t fineW = src.width();
    const uint32_t fineH = src.height();
    uint32_t coarseW = fineW;
    uint32_t coarseH = fineH;
    if (coarseW > kMaxHeightMapSize)
        coarseW = kMaxHeightMapSize;
    if (coarseH > kMaxHeightMapSize)
        coarseH = kMaxHeightMapSize;

    const float worldX = src.cellSize() * static_cast<float>(fineW - 1u);
    const float worldZ = src.cellSize() * static_cast<float>(fineH - 1u);
    const float coarseCellX = (coarseW > 1u) ? (worldX / static_cast<float>(coarseW - 1u)) : src.cellSize();
    const float coarseCellZ = (coarseH > 1u) ? (worldZ / static_cast<float>(coarseH - 1u)) : src.cellSize();
    const float coarseCell  = coarseCellX > 0.0f ? coarseCellX : src.cellSize();
    (void)coarseCellZ;

    HeightMap coarse;
    if (!coarse.create(coarseW, coarseH, coarseCell, src.heightScale()))
        return;
    coarse.setOrigin(src.origin());

    if (coarseW == fineW && coarseH == fineH)
    {
        for (uint32_t z = 0; z < coarseH; ++z)
        {
            for (uint32_t x = 0; x < coarseW; ++x)
                coarse.setHeight(static_cast<int>(x), static_cast<int>(z), src.height(static_cast<int>(x), static_cast<int>(z)));
        }
        m_coarse = std::move(coarse);
        return;
    }

    for (uint32_t cz = 0; cz < coarseH; ++cz)
    {
        const int z0 = static_cast<int>((static_cast<uint64_t>(cz) * (fineH - 1u)) / (coarseH - 1u));
        int       z1 = static_cast<int>((static_cast<uint64_t>(cz + 1u) * (fineH - 1u)) / (coarseH - 1u));
        if (z1 <= z0)
            z1 = z0 + 1;
        if (z1 > static_cast<int>(fineH))
            z1 = static_cast<int>(fineH);
        for (uint32_t cx = 0; cx < coarseW; ++cx)
        {
            const int x0 = static_cast<int>((static_cast<uint64_t>(cx) * (fineW - 1u)) / (coarseW - 1u));
            int       x1 = static_cast<int>((static_cast<uint64_t>(cx + 1u) * (fineW - 1u)) / (coarseW - 1u));
            if (x1 <= x0)
                x1 = x0 + 1;
            if (x1 > static_cast<int>(fineW))
                x1 = static_cast<int>(fineW);
            double sum = 0.0;
            int    n   = 0;
            for (int z = z0; z < z1; ++z)
            {
                for (int x = x0; x < x1; ++x)
                {
                    sum += static_cast<double>(src.height(x, z));
                    ++n;
                }
            }
            coarse.setHeight(static_cast<int>(cx), static_cast<int>(cz), n > 0 ? static_cast<float>(sum / static_cast<double>(n)) : 0.0f);
        }
    }
    m_coarse = std::move(coarse);
}

bool TerrainGrid::boxFilterCoarseFromWorking()
{
    if (!m_working.valid())
        return false;
    boxFilterCoarseFrom(m_working);
    return m_coarse.valid();
}

void TerrainGrid::applyWorkingRect(int x0, int z0, int x1, int z1, bool heights, bool splat)
{
    if (!m_valid || !m_working.valid())
        return;
    if (x0 > x1)
    {
        const int t = x0;
        x0          = x1;
        x1          = t;
    }
    if (z0 > z1)
    {
        const int t = z0;
        z0          = z1;
        z1          = t;
    }
    if (x0 < 0)
        x0 = 0;
    if (z0 < 0)
        z0 = 0;
    const int maxS = m_tileCells * static_cast<int>(m_tilesX);
    const int maxT = m_tileCells * static_cast<int>(m_tilesZ);
    if (x1 > maxS)
        x1 = maxS;
    if (z1 > maxT)
        z1 = maxT;
    if (x1 < x0 || z1 < z0)
        return;

    int tx0 = x0 / m_tileCells;
    int tz0 = z0 / m_tileCells;
    int tx1 = x1 / m_tileCells;
    int tz1 = z1 / m_tileCells;
    if (x0 % m_tileCells == 0 && tx0 > 0)
        --tx0;
    if (z0 % m_tileCells == 0 && tz0 > 0)
        --tz0;
    if (tx0 < 0)
        tx0 = 0;
    if (tz0 < 0)
        tz0 = 0;
    if (tx1 >= static_cast<int>(m_tilesX))
        tx1 = static_cast<int>(m_tilesX) - 1;
    if (tz1 >= static_cast<int>(m_tilesZ))
        tz1 = static_cast<int>(m_tilesZ) - 1;

    for (int tz = tz0; tz <= tz1; ++tz)
    {
        for (int tx = tx0; tx <= tx1; ++tx)
        {
            TileSlot& slot = m_slots[tz][tx];
            if (!slot.resident)
                continue;
            if (heights)
            {
                HeightMap hm;
                if (copyWorkingTileHeight(tx, tz, hm))
                {
                    const int samples = m_tileCells + 1;
                    HeightMap& dst = slot.world.heightMap();
                    if (dst.valid() && static_cast<int>(dst.width()) == samples)
                    {
                        for (int z = 0; z < samples; ++z)
                        {
                            for (int x = 0; x < samples; ++x)
                                dst.setHeight(x, z, hm.height(x, z));
                        }
                        const int lx0 = x0 - tx * m_tileCells;
                        const int lx1 = x1 - tx * m_tileCells;
                        const int lz0 = z0 - tz * m_tileCells;
                        const int lz1 = z1 - tz * m_tileCells;
                        slot.world.markHeightDirtyRect(lx0, lz0, lx1, lz1);
                    }
                }
            }
            if (splat)
            {
                if (!copyWorkingTileSplat(tx, tz, slot.splat))
                    slot.splat.generateFromHeight(slot.world.heightMap());
                slot.heapPacked = false;
            }
        }
    }
    if (heights)
        boxFilterCoarseFrom(m_working);
}

bool TerrainGrid::assembleWorking()
{
    if (!m_valid)
        return false;
    const uint32_t samples = static_cast<uint32_t>(m_tilesX) * static_cast<uint32_t>(m_tileCells) + 1u;
    if (!m_working.createWorking(samples, samples, m_cellSize, heightScale()))
        return false;
    m_working.setOrigin(m_origin);
    if (!m_workingSplat.createWorking(samples, samples))
        return false;

    const int tileSamples = m_tileCells + 1;
    for (int tz = 0; tz < static_cast<int>(m_tilesZ); ++tz)
    {
        for (int tx = 0; tx < static_cast<int>(m_tilesX); ++tx)
        {
            HeightMap tileHm;
            SplatMap  tileSplat;
            bool      gotHeight = false;
            if (m_slots[tz][tx].resident && m_slots[tz][tx].world.heightMap().valid())
            {
                tileHm    = m_slots[tz][tx].world.heightMap();
                tileSplat = m_slots[tz][tx].splat;
                gotHeight = tileHm.valid();
            }
            if (!gotHeight)
            {
                if (m_tileDir.empty())
                    return false;
                if (!tileHm.loadBinary(tileHeightPath(m_tileDir, tx, tz)))
                    return false;
                Image splatImg;
                const std::filesystem::path splatPath = tileSplatPath(m_tileDir, tx, tz);
                std::error_code ec;
                if (std::filesystem::exists(splatPath, ec) && !ec && splatImg.createFromFile(splatPath) && splatImg.valid()
                    && splatImg.width() == tileHm.width() && splatImg.height() == tileHm.height())
                    tileSplat.createFromRGBA(splatImg.width(), splatImg.height(), splatImg.pixels());
                else
                    tileSplat.generateFromHeight(tileHm);
            }
            if (static_cast<int>(tileHm.width()) != tileSamples || static_cast<int>(tileHm.height()) != tileSamples)
                return false;
            const int dstX = tx * m_tileCells;
            const int dstZ = tz * m_tileCells;
            for (int z = 0; z < tileSamples; ++z)
            {
                for (int x = 0; x < tileSamples; ++x)
                {
                    m_working.setHeight(dstX + x, dstZ + z, tileHm.height(x, z));
                    if (tileSplat.valid())
                    {
                        uint8_t c[4]{};
                        tileSplat.getTexel(x, z, c);
                        m_workingSplat.setTexel(dstX + x, dstZ + z, c[0], c[1], c[2], c[3]);
                    }
                }
            }
        }
    }
    return true;
}

bool TerrainGrid::loadFineTile(int tx, int tz)
{
    TileSlot& slot = m_slots[tz][tx];
    if (slot.resident)
        return true;
    if (m_working.valid())
        return sliceWorkingTile(tx, tz);
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

    TerrainDesc desc;
    desc.heightMap        = std::move(hm);
    desc.chunkCells       = m_chunkCells;
    desc.lodDistanceCount = m_lodDistanceCount;
    std::memcpy(desc.lodDistances, m_lodDistances, sizeof(float) * kMaxLodLevels);
    if (!slot.world.create(std::move(desc)))
    {
        if (!slot.missingLogged)
        {
            DE_LOG_ERROR(LogCategory::Render, "TerrainGrid: missing tile ({},{}) — coarse fallback", tx, tz);
            slot.missingLogged = true;
        }
        return false;
    }
    slot.world.setUploadHeightTexture(false);
    {
        Image splatImg;
        const std::filesystem::path splatPath = tileSplatPath(m_tileDir, tx, tz);
        ec.clear();
        if (std::filesystem::exists(splatPath, ec) && !ec && splatImg.createFromFile(splatPath) && splatImg.valid()
            && splatImg.width() == slot.world.heightMap().width() && splatImg.height() == slot.world.heightMap().height()
            && slot.splat.createFromRGBA(splatImg.width(), splatImg.height(), splatImg.pixels()))
        {
        }
        else if (!copyWorkingTileSplat(tx, tz, slot.splat))
        {
            if (m_working.valid() && !m_workingSplat.valid())
                m_workingSplat.generateFromHeight(m_working);
            if (!copyWorkingTileSplat(tx, tz, slot.splat))
                slot.splat.generateFromHeight(slot.world.heightMap());
        }
    }
    slot.resident          = true;
    slot.firstGpuApplyDone = false;
    slot.heapPacked        = false;
    return true;
}

void TerrainGrid::unregisterTileHeap(TileSlot& slot)
{
    if (slot.cache)
    {
        slot.cache->unregisterPackedHeap(&slot.heap);
        slot.cache = nullptr;
    }
    if (slot.heap.heap)
        m_heapRetire.push(std::move(slot.heap.heap));
    slot.heap      = PackedSrvHeap{};
    slot.heapPacked = false;
}

void TerrainGrid::evictFineTile(int tx, int tz)
{
    if (tx < 0 || tz < 0 || tx >= static_cast<int>(kMaxWorldTiles) || tz >= static_cast<int>(kMaxWorldTiles))
        return;
    TileSlot& slot = m_slots[tz][tx];
    unregisterTileHeap(slot);
    slot.world.giveGpuMeshes(m_gpuRetire);
    if (slot.splatTexture.valid())
        m_textureRetire.push(std::move(slot.splatTexture));
    slot.world             = TerrainWorld{};
    slot.splatTexture      = Texture2D{};
    slot.splat             = SplatMap{};
    slot.resident          = false;
    slot.firstGpuApplyDone = false;
}

void TerrainGrid::updateLod(const Vector3f& cameraPos)
{
    if (!m_valid)
        return;

    int minTx = static_cast<int>(m_tilesX);
    int maxTx = -1;
    int minTz = static_cast<int>(m_tilesZ);
    int maxTz = -1;
    for (int tz = 0; tz < static_cast<int>(m_tilesZ); ++tz)
    {
        for (int tx = 0; tx < static_cast<int>(m_tilesX); ++tx)
        {
            if (!m_slots[tz][tx].resident)
                continue;
            if (tx < minTx)
                minTx = tx;
            if (tx > maxTx)
                maxTx = tx;
            if (tz < minTz)
                minTz = tz;
            if (tz > maxTz)
                maxTz = tz;
        }
    }
    if (maxTx < minTx || maxTz < minTz)
        return;

    const int chunksPerTileX = m_tileCells / m_chunkCells;
    const int chunksPerTileZ = chunksPerTileX;
    const int vx             = (maxTx - minTx + 1) * chunksPerTileX;
    const int vz             = (maxTz - minTz + 1) * chunksPerTileZ;
    if (vx <= 0 || vz <= 0)
        return;

    std::vector<int> virtualLods(static_cast<size_t>(vx) * static_cast<size_t>(vz), -1);
    for (int tz = minTz; tz <= maxTz; ++tz)
    {
        for (int tx = minTx; tx <= maxTx; ++tx)
        {
            TileSlot& slot = m_slots[tz][tx];
            if (!slot.resident)
                continue;
            const TerrainWorld& world = slot.world;
            const int ox = (tx - minTx) * chunksPerTileX;
            const int oz = (tz - minTz) * chunksPerTileZ;
            for (int cz = 0; cz < world.chunksZ(); ++cz)
            {
                for (int cx = 0; cx < world.chunksX(); ++cx)
                {
                    const TerrainChunk* c = world.chunk(cx, cz);
                    if (!c)
                        continue;
                    const Vector3f center = c->bounds.Center();
                    const float dx = center.x - cameraPos.x;
                    const float dy = center.y - cameraPos.y;
                    const float dz = center.z - cameraPos.z;
                    const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
                    const int current = (c->builtLod < 0) ? -1 : c->lod;
                    virtualLods[static_cast<size_t>((oz + cz) * vx + (ox + cx))] =
                        lodFromDistance(dist, m_lodDistances, m_lodDistanceCount, world.maxLod(), current);
                }
            }
        }
    }

    int maxIter = vx + vz;
    if (maxIter < 32)
        maxIter = 32;
    restrictNeighborLods(virtualLods.data(), vx, vz, maxIter);

    std::vector<int>      tileLods;
    std::vector<EdgeMask> tileEdges;
    for (int tz = minTz; tz <= maxTz; ++tz)
    {
        for (int tx = minTx; tx <= maxTx; ++tx)
        {
            TileSlot& slot = m_slots[tz][tx];
            if (!slot.resident)
                continue;
            TerrainWorld& world = slot.world;
            const int     n     = world.chunksX() * world.chunksZ();
            tileLods.assign(static_cast<size_t>(n), 0);
            tileEdges.assign(static_cast<size_t>(n), EdgeMask{});
            const int ox = (tx - minTx) * chunksPerTileX;
            const int oz = (tz - minTz) * chunksPerTileZ;
            for (int cz = 0; cz < world.chunksZ(); ++cz)
            {
                for (int cx = 0; cx < world.chunksX(); ++cx)
                {
                    const int gx = ox + cx;
                    const int gz = oz + cz;
                    const int li = cz * world.chunksX() + cx;
                    tileLods[static_cast<size_t>(li)]  = virtualLods[static_cast<size_t>(gz * vx + gx)];
                    tileEdges[static_cast<size_t>(li)] = neighborCoarserMask(virtualLods.data(), vx, vz, gx, gz);
                }
            }
            world.applyExternalLods(tileLods.data(), tileEdges.data());
        }
    }
}

bool TerrainGrid::packTileSplat(
    int tileX,
    int tileZ,
    D3D12_CPU_DESCRIPTOR_HANDLE splatCpu,
    Renderer* renderer,
    const TerrainMaterial* material)
{
    if (!isResident(tileX, tileZ))
        return false;
    TileSlot& slot = m_slots[tileZ][tileX];
    unregisterTileHeap(slot);

    ID3D12Device*     device = renderer ? renderer->device() : nullptr;
    GpuResourceCache* cache  = renderer ? &renderer->gpuResources() : nullptr;

    slot.heap.shadowSlot = TerrainMaterial::kShadowSlot;
    slot.heap.splatSlot  = TerrainMaterial::kSplatSlot;
    slot.heap.srvCount   = TerrainMaterial::kSrvCount;

    if (material && material->isValid())
    {
        if (!material->packTileHeap(device, slot.heap, splatCpu))
            return false;
    }
    else
        copySplat(device, slot.heap, splatCpu);

    if (cache)
    {
        cache->registerPackedHeap(&slot.heap);
        slot.cache = cache;
    }
    slot.heapPacked = true;
    return true;
}

bool TerrainGrid::packTileHeapGpu(Renderer& renderer, TileSlot& slot, const TerrainMaterial& material)
{
    if (!slot.splat.valid())
        slot.splat.generateFromHeight(slot.world.heightMap());

    if (slot.splatTexture.valid())
        m_textureRetire.push(std::move(slot.splatTexture));

    Image splatImg;
    if (!splatImg.createFromRGBA(slot.splat.rgba(), slot.splat.width(), slot.splat.height(), slot.splat.width() * 4u)
        || !slot.splatTexture.createFromImage(renderer, splatImg, Dark::Color::TextureUsage::Data))
    {
        DE_LOG_ERROR(LogCategory::Render, "TerrainGrid: tile splat upload failed");
        return false;
    }

    unregisterTileHeap(slot);
    if (!material.packTileHeap(renderer.device(), slot.heap, slot.splatTexture.cpuHandle()))
        return false;
    renderer.gpuResources().registerPackedHeap(&slot.heap);
    slot.cache      = &renderer.gpuResources();
    slot.heapPacked = true;
    return true;
}

bool TerrainGrid::uploadCoarseHeightTexture(Renderer& renderer)
{
    if (!m_coarse.valid())
        return false;

    const uint32_t w = m_coarse.width();
    const uint32_t h = m_coarse.height();
    std::vector<float> samples(static_cast<size_t>(w) * h);
    for (uint32_t z = 0; z < h; ++z)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            samples[static_cast<size_t>(z) * w + x] =
                m_coarse.heightAtWorld(m_coarse.worldX(static_cast<int>(x)), m_coarse.worldZ(static_cast<int>(z)));
        }
    }
    if (!m_heightTexture.createFromR32Float(renderer, samples.data(), w, h, w * static_cast<uint32_t>(sizeof(float))))
    {
        DE_LOG_ERROR(LogCategory::Render, "TerrainGrid: coarse height texture upload failed");
        return false;
    }
    return true;
}

void TerrainGrid::applyGpu(Renderer& renderer, const TerrainMaterial* material)
{
    if (!m_heightTexture.valid())
        uploadCoarseHeightTexture(renderer);

    if (material && material->isValid())
    {
        for (int tz = 0; tz < static_cast<int>(m_tilesZ); ++tz)
        {
            for (int tx = 0; tx < static_cast<int>(m_tilesX); ++tx)
            {
                TileSlot& slot = m_slots[tz][tx];
                if (!slot.resident || slot.heapPacked)
                    continue;
                packTileHeapGpu(renderer, slot, *material);
            }
        }
    }

    TileSlot* target     = nullptr;
    bool      firstApply = false;
    for (int tz = 0; tz < static_cast<int>(m_tilesZ); ++tz)
    {
        for (int tx = 0; tx < static_cast<int>(m_tilesX); ++tx)
        {
            TileSlot& slot = m_slots[tz][tx];
            if (!slot.resident || slot.world.pendingGpuUploads() <= 0)
                continue;
            if (!slot.firstGpuApplyDone)
            {
                target     = &slot;
                firstApply = true;
                break;
            }
            if (!target)
                target = &slot;
        }
        if (firstApply)
            break;
    }
    if (!target)
        return;

    const auto t0 = std::chrono::steady_clock::now();
    if (firstApply)
    {
        target->world.uploadDirty(renderer, -1);
        target->firstGpuApplyDone = true;
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ms > kSteadyStateGpuMs)
            DE_LOG_INFO(LogCategory::Render, "TerrainGrid: first tile GPU {:.1f} ms", ms);
        return;
    }

    while (target->world.pendingGpuUploads() > 0)
    {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ms >= kSteadyStateGpuMs)
            break;
        target->world.uploadDirty(renderer, 1);
    }
}

void TerrainGrid::updateStreaming(const Vector3f& cameraPos, Renderer* renderer)
{
    updateStreaming(cameraPos, renderer, nullptr);
}

void TerrainGrid::updateStreaming(const Vector3f& cameraPos, Renderer* renderer, const TerrainMaterial* material)
{
    if (!m_valid)
        return;

    m_gpuRetire.tick();
    m_heapRetire.tick();
    m_textureRetire.tick();
    for (int tz = 0; tz < static_cast<int>(m_tilesZ); ++tz)
    {
        for (int tx = 0; tx < static_cast<int>(m_tilesX); ++tx)
        {
            if (m_slots[tz][tx].resident)
                m_slots[tz][tx].world.tickGpuRetire();
        }
    }

    int cx = 0;
    int cz = 0;
    worldToTile(cameraPos.x, cameraPos.z, cx, cz);

    int  loaded    = 0;
    int  evictN    = 0;
    int  evictTx[kMaxWorldTiles * kMaxWorldTiles];
    int  evictTz[kMaxWorldTiles * kMaxWorldTiles];
    int  bestLoadTx = -1;
    int  bestLoadTz = -1;
    float bestLoadD = 1.0e30f;
    const int maxLoad = renderer ? 1 : (kMaxWorldTiles * kMaxWorldTiles);
    for (int tz = 0; tz < static_cast<int>(m_tilesZ); ++tz)
    {
        for (int tx = 0; tx < static_cast<int>(m_tilesX); ++tx)
        {
            const bool want = inRing(tx, tz, cx, cz) || inPinRing(tx, tz);
            TileSlot&  slot = m_slots[tz][tx];
            if (want)
            {
                if (!slot.resident)
                {
                    if (!renderer)
                    {
                        if (loadFineTile(tx, tz))
                            ++loaded;
                    }
                    else
                    {
                        const float tcx = m_origin.x + (static_cast<float>(tx) + 0.5f) * m_tileWorld;
                        const float tcz = m_origin.z + (static_cast<float>(tz) + 0.5f) * m_tileWorld;
                        const float dx  = tcx - cameraPos.x;
                        const float dz  = tcz - cameraPos.z;
                        const float d2  = dx * dx + dz * dz;
                        if (d2 < bestLoadD)
                        {
                            bestLoadD  = d2;
                            bestLoadTx = tx;
                            bestLoadTz = tz;
                        }
                    }
                }
            }
            else if (slot.resident)
            {
                evictTx[evictN] = tx;
                evictTz[evictN] = tz;
                ++evictN;
            }
        }
    }
    if (bestLoadTx >= 0 && loaded < maxLoad && loadFineTile(bestLoadTx, bestLoadTz))
        ++loaded;

    for (int i = 0; i < evictN; ++i)
        evictFineTile(evictTx[i], evictTz[i]);

    if (loaded > 0 || evictN > 0)
    {
        DE_LOG_INFO(
            LogCategory::Render,
            "TerrainGrid: resident {} tiles, stream queue {}",
            residentCount(),
            0);
    }

    if (!m_loggedStreamingIn && residentCount() > 0)
    {
        DE_LOG_INFO(LogCategory::Render, "TerrainGrid: streaming in");
        m_loggedStreamingIn = true;
    }

    // Lod on the resident virtual grid before any GPU create. Tiles do not call updateLod.
    updateLod(cameraPos);

    bool rebuild = false;
    for (int tz = 0; tz < static_cast<int>(m_tilesZ); ++tz)
    {
        for (int tx = 0; tx < static_cast<int>(m_tilesX); ++tx)
        {
            if (m_slots[tz][tx].resident && m_slots[tz][tx].world.needsRebuild())
            {
                rebuild = true;
                break;
            }
        }
        if (rebuild)
            break;
    }
    if (rebuild)
    {
        int rebuilt = 0;
        const int maxRebuild = renderer ? 1 : (kMaxWorldTiles * kMaxWorldTiles);
        for (int tz = 0; tz < static_cast<int>(m_tilesZ); ++tz)
        {
            for (int tx = 0; tx < static_cast<int>(m_tilesX); ++tx)
            {
                TileSlot& slot = m_slots[tz][tx];
                if (slot.resident && slot.world.needsRebuild())
                {
                    slot.world.rebuildDirtyCpuMeshes();
                    ++rebuilt;
                    if (rebuilt >= maxRebuild)
                        break;
                }
            }
            if (rebuilt >= maxRebuild)
                break;
        }
    }

    if (renderer)
        applyGpu(*renderer, material);
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
    if (slot.resident && slot.world.heightMap().valid())
        return &slot.world.heightMap();
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
            if (!slot.resident || !slot.world.heightMap().valid())
                continue;
            const Collision::RayHit3D hit = slot.world.heightMap().raycast(ray, maxDistance);
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
            if (!slot.resident || !slot.world.heightMap().valid())
                continue;
            box.ExpandToInclude(slot.world.heightMap().bounds());
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

void TerrainGrid::draw(
    ID3D12GraphicsCommandList* cmd,
    const TerrainPipeline& pipeline,
    const TerrainMaterial& material,
    const Camera3D& camera,
    const Frustum3f* frustum,
    const Sky::Environment* env,
    const ShadowSystem* shadows,
    const DebugRenderState* debug) const
{
    if (!m_valid || !cmd)
        return;
    for (int tz = 0; tz < static_cast<int>(m_tilesZ); ++tz)
    {
        for (int tx = 0; tx < static_cast<int>(m_tilesX); ++tx)
        {
            const TileSlot& slot = m_slots[tz][tx];
            if (!slot.resident || !slot.heapPacked)
                continue;
            slot.world.draw(cmd, pipeline, material, camera, frustum, env, shadows, debug, &slot.heap);
        }
    }
}

void TerrainGrid::drawGBuffer(
    ID3D12GraphicsCommandList* cmd,
    const TerrainPipeline& pipeline,
    const TerrainMaterial& material,
    const Camera3D& camera,
    const Frustum3f* frustum,
    const DebugRenderState* debug,
    const Matrix4f* prevViewProj) const
{
    if (!m_valid || !cmd)
        return;
    for (int tz = 0; tz < static_cast<int>(m_tilesZ); ++tz)
    {
        for (int tx = 0; tx < static_cast<int>(m_tilesX); ++tx)
        {
            const TileSlot& slot = m_slots[tz][tx];
            if (!slot.resident || !slot.heapPacked)
                continue;
            slot.world.drawGBuffer(cmd, pipeline, material, camera, frustum, debug, prevViewProj, &slot.heap);
        }
    }
}

void TerrainGrid::drawDepth(ID3D12GraphicsCommandList* cmd, const Frustum3f* casterFrustum) const
{
    if (!m_valid || !cmd)
        return;
    for (int tz = 0; tz < static_cast<int>(m_tilesZ); ++tz)
    {
        for (int tx = 0; tx < static_cast<int>(m_tilesX); ++tx)
        {
            const TileSlot& slot = m_slots[tz][tx];
            if (!slot.resident)
                continue;
            slot.world.drawDepth(cmd, casterFrustum);
        }
    }
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
    const TerrainWorld* world = residentWorld(tileX, tileZ);
    if (!world)
        return nullptr;
    return world->heightMap().valid() ? &world->heightMap() : nullptr;
}

const TerrainWorld* TerrainGrid::residentWorld(int tileX, int tileZ) const
{
    if (!isResident(tileX, tileZ))
        return nullptr;
    return &m_slots[tileZ][tileX].world;
}

const PackedSrvHeap* TerrainGrid::residentPackedHeap(int tileX, int tileZ) const
{
    if (!isResident(tileX, tileZ))
        return nullptr;
    return &m_slots[tileZ][tileX].heap;
}

uint32_t TerrainGrid::lastDrawCalls() const
{
    uint32_t n = 0;
    if (!m_valid)
        return 0;
    for (int tz = 0; tz < static_cast<int>(m_tilesZ); ++tz)
    {
        for (int tx = 0; tx < static_cast<int>(m_tilesX); ++tx)
        {
            if (m_slots[tz][tx].resident)
                n += m_slots[tz][tx].world.lastDrawCalls();
        }
    }
    return n;
}

uint32_t TerrainGrid::lastTriangles() const
{
    uint32_t n = 0;
    if (!m_valid)
        return 0;
    for (int tz = 0; tz < static_cast<int>(m_tilesZ); ++tz)
    {
        for (int tx = 0; tx < static_cast<int>(m_tilesX); ++tx)
        {
            if (m_slots[tz][tx].resident)
                n += m_slots[tz][tx].world.lastTriangles();
        }
    }
    return n;
}

void TerrainGrid::rebindResidentHeaps(Renderer& renderer, const TerrainMaterial& material)
{
    if (!m_valid || !material.isValid())
        return;
    for (int tz = 0; tz < static_cast<int>(m_tilesZ); ++tz)
    {
        for (int tx = 0; tx < static_cast<int>(m_tilesX); ++tx)
        {
            TileSlot& slot = m_slots[tz][tx];
            if (!slot.resident)
                continue;
            packTileHeapGpu(renderer, slot, material);
        }
    }
}

} // namespace Terrain
} // namespace Dark
