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
    int c = requested > 0 ? requested : 64;
    if (c > tileCells)
        c = tileCells;
    while (c > 1 && ((tileCells % c) != 0 || !isPowerOfTwo(c)))
        --c;
    if (!isPowerOfTwo(c))
        c = 1;
    return c;
}

} // namespace

TerrainGrid::~TerrainGrid()
{
    reset();
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
    slot.splat.generateFromHeight(slot.world.heightMap());
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
    slot.heap      = PackedSrvHeap{};
    slot.heapPacked = false;
}

void TerrainGrid::evictFineTile(int tx, int tz)
{
    if (tx < 0 || tz < 0 || tx >= static_cast<int>(kMaxWorldTiles) || tz >= static_cast<int>(kMaxWorldTiles))
        return;
    TileSlot& slot = m_slots[tz][tx];
    unregisterTileHeap(slot);
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
                    virtualLods[static_cast<size_t>((oz + cz) * vx + (ox + cx))] =
                        lodFromDistance(dist, m_lodDistances, m_lodDistanceCount, world.maxLod());
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
            if (world.needsRebuild())
                world.rebuildDirtyCpuMeshes();
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

    renderer.waitForGpu();
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

    if (!m_loggedStreamingIn && residentCount() > 0)
    {
        DE_LOG_INFO(LogCategory::Render, "TerrainGrid: streaming in");
        m_loggedStreamingIn = true;
    }

    // Lod on the resident virtual grid before any GPU create. Tiles do not call updateLod.
    updateLod(cameraPos);

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

} // namespace Terrain
} // namespace Dark
