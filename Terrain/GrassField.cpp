#include "Terrain/GrassField.h"

#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Math/Vector3f.h"
#include "Terrain/GrassBend.h"
#include "Terrain/GrassWind.h"
#include "Terrain/TerrainGen.h"
#include "Terrain/TerrainGrid.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Dark::Terrain
{
    namespace
    {
        constexpr int      kGrid            = 64;
        constexpr int      kCandidates      = kGrid * kGrid;
        constexpr int      kBaseKeep[4]     = { 32, 16, 8, 5 };
        constexpr uint32_t kRegionSlots[4]  = { kGrassLod0Slots, kGrassLod1Slots, kGrassLod2Slots, kGrassLod3Slots };
        constexpr float    kGrassWeightMin  = 0.35f;
        constexpr float    kRockWeightMax   = 0.55f;
        constexpr float    kSnowWeightMax   = 0.45f;
        constexpr float    kNormalYMin      = 0.80f;
        constexpr float    kRootLiftMetres  = 0.015f;
        constexpr int      kDemoteBudget    = 8;
        constexpr int      kGenerateBudget  = 4;
        constexpr int      kGenerateWarm    = 8;
        constexpr float    kWarmRadius      = 48.0f;

        double sq(float metres)
        {
            const double m = static_cast<double>(metres);
            return m * m;
        }

        double dist2(float x0, float z0, float x1, float z1)
        {
            const double dx = static_cast<double>(x0) - static_cast<double>(x1);
            const double dz = static_cast<double>(z0) - static_cast<double>(z1);
            return dx * dx + dz * dz;
        }

        float tileOrigin(int tile)
        {
            return static_cast<float>(tile) * kGrassTileMetres;
        }

        float tileCenterCoord(int tile)
        {
            return tileOrigin(tile) + kGrassTileMetres * 0.5f;
        }

        uint64_t tileKey(int tileX, int tileZ)
        {
            return (static_cast<uint64_t>(static_cast<uint32_t>(tileX)) << 32) | static_cast<uint32_t>(tileZ);
        }

        bool bitSet(const uint64_t* bits, int i)
        {
            return ((bits[i >> 6] >> (i & 63)) & 1ull) != 0;
        }

        void setBit(uint64_t* bits, int i)
        {
            bits[i >> 6] |= 1ull << (i & 63);
        }

        int keepThreshold(int lod, float densityScale)
        {
            const double scaled = static_cast<double>(kBaseKeep[lod]) * static_cast<double>(densityScale);
            int          k      = static_cast<int>(std::lround(scaled));
            if (k < 0)
                k = 0;
            if (k > 32)
                k = 32;
            return k;
        }

        // 5 and 9 are odd, so each residue 0..31 covers 128 cells and every row and column
        // holds the same count. The step sizes keep a threshold's runs short on both axes and both diagonals.
        int keepRank(int i)
        {
            const int ix = i & (kGrid - 1);
            const int iz = i / kGrid;
            return (ix * 5 + iz * 9) & 31;
        }

        int countKept(const uint64_t* bits, int k)
        {
            int n = 0;
            for (int i = 0; i < kCandidates; ++i)
            {
                if (keepRank(i) >= k)
                    continue;
                if (bitSet(bits, i))
                    ++n;
            }
            return n;
        }

        // Worst-case blades a threshold writes. A resident tile's kept blades are exact when k does not grow.
        uint32_t slotsFor(int k, int oldK, uint32_t oldCount, const uint64_t* oldBits)
        {
            if (!oldBits)
                return static_cast<uint32_t>(k) * 128u;
            if (k <= oldK)
                return static_cast<uint32_t>(countKept(oldBits, k));
            return oldCount + static_cast<uint32_t>(k - oldK) * 128u;
        }

        int fitKeep(int lod, float densityScale, uint32_t room, int oldK, uint32_t oldCount, const uint64_t* oldBits)
        {
            const int scaled = keepThreshold(lod, densityScale);
            const int floorK = kBaseKeep[lod];
            if (slotsFor(scaled, oldK, oldCount, oldBits) <= room)
                return scaled;
            // Above scale 1 the region may refuse the scaled predicate. Do not go under the scale-1 base.
            if (scaled > floorK)
            {
                for (int k = scaled - 1; k >= floorK; --k)
                {
                    if (slotsFor(k, oldK, oldCount, oldBits) <= room)
                        return k;
                }
            }
            return -1;
        }

        int nominalLod(double d2)
        {
            if (d2 <= sq(kGrassLod0NominalMetres))
                return 0;
            if (d2 <= sq(kGrassLod1NominalMetres))
                return 1;
            if (d2 <= sq(kGrassLod2NominalMetres))
                return 2;
            if (d2 <= sq(kGrassCreateRadiusMetres))
                return 3;
            return -1;
        }

        int promoteTo(int lod, double d2)
        {
            if (lod == 1 && d2 < sq(kGrassPromoteTo0Metres))
                return 0;
            if (lod == 2 && d2 < sq(kGrassPromoteTo1Metres))
                return 1;
            if (lod == 3 && d2 < sq(kGrassPromoteTo2Metres))
                return 2;
            return -1;
        }

        int demoteTo(int lod, double d2)
        {
            if (lod == 0 && d2 > sq(kGrassDemoteFrom0Metres))
                return 1;
            if (lod == 1 && d2 > sq(kGrassDemoteFrom1Metres))
                return 2;
            if (lod == 2 && d2 > sq(kGrassDemoteFrom2Metres))
                return 3;
            return -1;
        }

        uint32_t makeTileSeed(int tileX, int tileZ, uint32_t seed)
        {
            const float h = hash21(tileX, tileZ, seed ^ 0x6A11C3u);
            return static_cast<uint32_t>(h * 16777216.0f);
        }

        void terrainTileOf(const TerrainGrid& grid, float x, float z, int& tx, int& tz)
        {
            tx             = 0;
            tz             = 0;
            const float span = grid.cellSize() * static_cast<float>(grid.tileCells());
            if (span <= 0.0f)
                return;
            tx = static_cast<int>(std::floor((static_cast<double>(x) - static_cast<double>(grid.origin().x)) / static_cast<double>(span)));
            tz = static_cast<int>(std::floor((static_cast<double>(z) - static_cast<double>(grid.origin().z)) / static_cast<double>(span)));
            if (tx < 0)
                tx = 0;
            if (tz < 0)
                tz = 0;
            const int maxX = static_cast<int>(grid.tilesX()) - 1;
            const int maxZ = static_cast<int>(grid.tilesZ()) - 1;
            if (tx > maxX)
                tx = maxX;
            if (tz > maxZ)
                tz = maxZ;
        }

        struct Job
        {
            double dist2 = 0.0;
            int    tileX = 0;
            int    tileZ = 0;
            int    kind  = 0; // 0 missing, 1 promote, 2 rebuild
            int    lod   = 0;
            size_t index = 0;
        };

        bool closerJob(const Job& a, const Job& b)
        {
            if (a.dist2 < b.dist2)
                return true;
            if (b.dist2 < a.dist2)
                return false;
            if (a.tileX != b.tileX)
                return a.tileX < b.tileX;
            return a.tileZ < b.tileZ;
        }

        struct Demote
        {
            double dist2 = 0.0;
            size_t index = 0;
        };

        bool closerDemote(const Demote& a, const Demote& b)
        {
            return a.dist2 < b.dist2;
        }
    } // namespace

    void GrassField::tileIndex(float worldX, float worldZ, int& tileX, int& tileZ)
    {
        tileX = static_cast<int>(std::floor(static_cast<double>(worldX) / static_cast<double>(kGrassTileMetres)));
        tileZ = static_cast<int>(std::floor(static_cast<double>(worldZ) / static_cast<double>(kGrassTileMetres)));
    }

    void GrassField::setParams(const GrassParams& params)
    {
        GrassParams clamped = params;
        clampGrassParams(clamped);
        const bool regen = clamped.densityScale != m_params.densityScale || clamped.seed != m_params.seed;
        m_params         = clamped;
        if (!regen)
            return;
        for (Tile& tile : m_tiles)
            tile.dirty = true;
    }

    void GrassField::reset()
    {
        m_tiles.clear();
        m_lookup.clear();
        for (Region& region : m_region)
            region.blades.clear();
        if (!m_wind.empty())
        {
            std::fill(m_wind.begin(), m_wind.end(), GrassTileWind{});
            m_freeSlots.clear();
            for (uint32_t slot = kGrassWindSlots; slot-- > 0;)
                m_freeSlots.push_back(slot);
        }
        m_loggedNoSplat = false;
        for (int lod = 0; lod < kGrassLodCount; ++lod)
            m_refused[lod] = false;
        clearInteraction();
    }

    void GrassField::clearInteraction()
    {
        for (int i = 0; i < kGrassInteractorSlots; ++i)
        {
            m_interactor[i]     = {};
            m_prevInteractor[i] = {};
        }
        for (int i = 0; i < kGrassFootprintSlots; ++i)
        {
            m_footprint[i]     = {};
            m_prevFootprint[i] = {};
            m_footAge[i]       = 0.0f;
        }
        m_velX        = 0.0f;
        m_velZ        = 0.0f;
        m_prevVelX    = 0.0f;
        m_prevVelZ    = 0.0f;
        m_dropX       = 0.0f;
        m_dropZ       = 0.0f;
        m_hasDrop     = false;
        m_hasClock    = false;
        m_lastPlaySec = 0.0;
    }

    void GrassField::decayFootprints(float dt)
    {
        if (!(dt > 0.0f))
            return;
        for (int i = 0; i < kGrassFootprintSlots; ++i)
        {
            if (!(m_footprint[i].strength > 0.0f))
                continue;
            m_footAge[i] += dt;
            if (m_footAge[i] >= kGrassFootprintLifeSec)
            {
                m_footprint[i] = {};
                m_footAge[i]   = 0.0f;
                continue;
            }
            m_footprint[i].strength = 1.0f - m_footAge[i] / kGrassFootprintLifeSec;
        }
    }

    void GrassField::placeFootprint(float x, float z)
    {
        int   slot      = -1;
        int   oldest    = 0;
        float oldestAge = -1.0f;
        for (int i = 0; i < kGrassFootprintSlots; ++i)
        {
            if (!(m_footprint[i].strength > 0.0f))
            {
                slot = i;
                break;
            }
            if (m_footAge[i] > oldestAge)
            {
                oldestAge = m_footAge[i];
                oldest    = i;
            }
        }
        if (slot < 0)
            slot = oldest;
        m_footprint[slot].x        = x;
        m_footprint[slot].z        = z;
        m_footprint[slot].strength = 1.0f;
        m_footprint[slot].radius   = kGrassShoveRadiusMetres;
        m_footAge[slot]            = 0.0f;
    }

    void GrassField::updateInteraction(const GrassPlayerSample& player, double playTimeSec)
    {
        for (int i = 0; i < kGrassInteractorSlots; ++i)
            m_prevInteractor[i] = m_interactor[i];
        for (int i = 0; i < kGrassFootprintSlots; ++i)
            m_prevFootprint[i] = m_footprint[i];
        m_prevVelX = m_velX;
        m_prevVelZ = m_velZ;

        float dt = 0.0f;
        if (m_hasClock)
        {
            const double step = playTimeSec - m_lastPlaySec;
            if (step > 0.0)
                dt = static_cast<float>(step);
        }
        m_hasClock    = true;
        m_lastPlaySec = playTimeSec;
        decayFootprints(dt);

        m_velX = player.velX;
        m_velZ = player.velZ;
        for (int i = 0; i < kGrassInteractorSlots; ++i)
            m_interactor[i] = {};
        if (!player.shoving)
        {
            m_hasDrop = false;
            return;
        }

        m_interactor[0].x        = player.x;
        m_interactor[0].z        = player.z;
        m_interactor[0].strength = 1.0f;
        m_interactor[0].radius   = kGrassShoveRadiusMetres;

        if (!m_hasDrop)
        {
            m_hasDrop = true;
            m_dropX   = player.x;
            m_dropZ   = player.z;
            return;
        }

        const float dx = player.x - m_dropX;
        const float dz = player.z - m_dropZ;
        if (dx * dx + dz * dz > kGrassFootprintStepMetres * kGrassFootprintStepMetres)
        {
            placeFootprint(player.x, player.z);
            m_dropX = player.x;
            m_dropZ = player.z;
        }
    }

    void GrassField::writeFrameInteraction(GrassFrameConstants& frame) const
    {
        frame.pushMetres = kGrassPushMetres;
        const uint32_t bits = (static_cast<uint32_t>(grassPackYawBits(m_prevVelX, m_prevVelZ)) << 16) | static_cast<uint32_t>(grassPackYawBits(m_velX, m_velZ));
        float packed = 0.0f;
        std::memcpy(&packed, &bits, sizeof(packed));
        frame.pad0 = packed;
        for (int i = 0; i < kGrassInteractorSlots; ++i)
        {
            frame.interactor[i]     = m_interactor[i];
            frame.prevInteractor[i] = m_prevInteractor[i];
        }
        for (int i = 0; i < kGrassFootprintSlots; ++i)
        {
            frame.footprint[i]     = m_footprint[i];
            frame.prevFootprint[i] = m_prevFootprint[i];
        }
    }

    GrassField::BladeSpan GrassField::blades(int lod) const
    {
        BladeSpan span;
        if (lod < 0 || lod >= kGrassLodCount)
            return span;
        span.count = static_cast<uint32_t>(m_region[lod].blades.size());
        span.data  = span.count > 0 ? m_region[lod].blades.data() : nullptr;
        return span;
    }

    GrassField::BladeSpan GrassField::tileBlades(int tileX, int tileZ) const
    {
        BladeSpan   span;
        const Tile* tile = findTile(tileX, tileZ);
        if (!tile || tile->count == 0)
            return span;
        span.data  = m_region[tile->lod].blades.data() + tile->begin;
        span.count = tile->count;
        return span;
    }

    int GrassField::residentLod(int tileX, int tileZ) const
    {
        const Tile* tile = findTile(tileX, tileZ);
        return tile ? tile->lod : -1;
    }

    void GrassField::ensureWind()
    {
        if (!m_wind.empty())
            return;
        m_wind.assign(kGrassWindSlots, GrassTileWind{});
        m_freeSlots.clear();
        m_freeSlots.reserve(kGrassWindSlots);
        for (uint32_t slot = kGrassWindSlots; slot-- > 0;)
            m_freeSlots.push_back(slot);
    }

    uint32_t GrassField::allocWindSlot()
    {
        ensureWind();
        if (m_freeSlots.empty())
            return kGrassWindSlots;
        const uint32_t slot = m_freeSlots.back();
        m_freeSlots.pop_back();
        return slot;
    }

    void GrassField::freeWindSlot(uint32_t slot)
    {
        if (slot >= kGrassWindSlots || m_wind.empty())
            return;
        m_wind[slot] = GrassTileWind{};
        m_freeSlots.push_back(slot);
    }

    void GrassField::warnFull(int lod)
    {
        if (lod < 0 || lod >= kGrassLodCount || m_refused[lod])
            return;
        m_refused[lod] = true;
        DE_LOG_WARN(LogCategory::Render, "GrassField: LOD {} region full, skipped a tile", lod);
    }

    GrassField::Tile* GrassField::findTile(int tileX, int tileZ)
    {
        const auto it = m_lookup.find(tileKey(tileX, tileZ));
        if (it == m_lookup.end())
            return nullptr;
        return &m_tiles[it->second];
    }

    const GrassField::Tile* GrassField::findTile(int tileX, int tileZ) const
    {
        const auto it = m_lookup.find(tileKey(tileX, tileZ));
        if (it == m_lookup.end())
            return nullptr;
        return &m_tiles[it->second];
    }

    uint32_t GrassField::appendBlades(int lod, const GrassBlade* src, uint32_t count)
    {
        Region&        region = m_region[lod];
        const uint32_t begin  = static_cast<uint32_t>(region.blades.size());
        if (count == 0 || !src)
            return begin;
        region.blades.insert(region.blades.end(), src, src + count);
        return begin;
    }

    void GrassField::removeSpan(int lod, uint32_t begin, uint32_t count)
    {
        if (count == 0)
            return;
        Region&        region = m_region[lod];
        const uint32_t end    = begin + count;
        DE_ASSERT(end <= region.blades.size());
        const auto first = region.blades.begin() + static_cast<std::ptrdiff_t>(begin);
        const auto last  = region.blades.begin() + static_cast<std::ptrdiff_t>(end);
        region.blades.erase(first, last);
        for (Tile& tile : m_tiles)
        {
            if (tile.lod == lod && tile.begin >= end)
                tile.begin -= count;
        }
    }

    void GrassField::eraseTileAt(size_t index)
    {
        Tile&          tile  = m_tiles[index];
        const int      lod   = tile.lod;
        const uint32_t begin = tile.begin;
        const uint32_t count = tile.count;
        const uint32_t slot  = tile.windSlot;
        const uint64_t key   = tileKey(tile.tileX, tile.tileZ);

        removeSpan(lod, begin, count);
        freeWindSlot(slot);
        m_lookup.erase(key);

        const size_t last = m_tiles.size() - 1;
        if (index != last)
        {
            m_tiles[index] = std::move(m_tiles[last]);
            m_lookup[tileKey(m_tiles[index].tileX, m_tiles[index].tileZ)] = static_cast<uint32_t>(index);
        }
        m_tiles.pop_back();
    }

    bool GrassField::splatReady(const TerrainGrid& grid, float x, float z) const
    {
        if (grid.editableWorkingSplat())
            return true;
        int tx = 0;
        int tz = 0;
        terrainTileOf(grid, x, z, tx, tz);
        return grid.residentSplat(tx, tz) != nullptr;
    }

    bool GrassField::acceptCandidate(const TerrainGrid& grid, int i, uint32_t tileSeed, float originX, float originZ, uint32_t tileSlot, float waterLevel, GrassBlade& out) const
    {
        const int   ix   = i & 63;
        const int   iz   = i >> 6;
        const float cell = kGrassTileMetres / static_cast<float>(kGrid);
        // Salts 1 and 2 are the placement jitter. Later salts are independent attribute draws.
        const float jx     = (hash21(i, 1, tileSeed) - 0.5f) * cell * 0.70f;
        const float jz     = (hash21(i, 2, tileSeed) - 0.5f) * cell * 0.70f;
        const float worldX = originX + (static_cast<float>(ix) + 0.5f) * cell + jx;
        const float worldZ = originZ + (static_cast<float>(iz) + 0.5f) * cell + jz;

        if (!grid.containsXZ(worldX, worldZ))
            return false;

        float            surfaceY = 0.0f;
        Math::Vector3f   normal(0.0f, 1.0f, 0.0f);
        const HeightMap* working  = grid.editableWorking();
        if (working)
        {
            surfaceY = working->heightAtWorld(worldX, worldZ);
            normal   = working->normalAtWorld(worldX, worldZ);
        }
        else
        {
            surfaceY = grid.heightAtWorld(worldX, worldZ);
            normal   = grid.normalAtWorld(worldX, worldZ);
        }

        const SplatMap*  splat = grid.editableWorkingSplat();
        const HeightMap* uv    = working;
        if (!splat)
        {
            int tx = 0;
            int tz = 0;
            terrainTileOf(grid, worldX, worldZ, tx, tz);
            splat = grid.residentSplat(tx, tz);
            uv    = grid.residentHeight(tx, tz);
        }
        if (!uv || !uv->valid())
            uv = grid.coarse().valid() ? &grid.coarse() : nullptr;
        if (!splat || !uv)
            return false;

        float fx = 0.0f;
        float fz = 0.0f;
        uv->worldToSample(worldX, worldZ, fx, fz);
        float weights[kMaxTerrainLayers]{};
        splat->sampleWeights(fx, fz, weights);

        if (weights[1] < kGrassWeightMin)
            return false;
        if (weights[2] > kRockWeightMax || weights[3] > kSnowWeightMax)
            return false;
        if (normal.y < kNormalYMin)
            return false;
        if (surfaceY < waterLevel)
            return false;

        out.x         = worldX + normal.x * kRootLiftMetres;
        out.y         = surfaceY + normal.y * kRootLiftMetres;
        out.z         = worldZ + normal.z * kRootLiftMetres;
        out.heightMul = Math::Lerp(0.70f, 1.30f, hash21(i, 3, tileSeed));
        out.flexMul   = Math::Lerp(0.80f, 1.20f, hash21(i, 4, tileSeed));
        out.phase     = hash21(i, 5, tileSeed) * 2.0f - 1.0f;
        out.yaw       = hash21(i, 6, tileSeed) * Math::TwoPi;
        out.tileSlot  = tileSlot;
        return true;
    }

    void GrassField::buildSubset(const TerrainGrid& grid, int tileX, int tileZ, int newK, int oldK, const uint64_t* oldBits, const GrassBlade* oldBlades, uint32_t oldCount,
                                 uint32_t tileSlot, float waterLevel, bool resampleAll, uint64_t* newBits)
    {
        m_scratch.clear();
        std::memset(newBits, 0, 64u * sizeof(uint64_t));
        const uint32_t tileSeed = makeTileSeed(tileX, tileZ, m_params.seed);
        const float    originX  = tileOrigin(tileX);
        const float    originZ  = tileOrigin(tileZ);
        uint32_t       oldIndex = 0;

        for (int i = 0; i < kCandidates; ++i)
        {
            const int  rank = keepRank(i);
            const bool pred = rank < newK;
            const bool was  = oldBits && bitSet(oldBits, i);
            if (was)
            {
                DE_ASSERT(oldBlades && oldIndex < oldCount);
                if (pred)
                {
                    GrassBlade kept = oldBlades[oldIndex];
                    kept.tileSlot   = tileSlot;
                    m_scratch.push_back(kept);
                    setBit(newBits, i);
                }
                ++oldIndex;
                continue;
            }
            if (!pred)
                continue;
            // Coarser predicate already rejected these. Promotion only samples the new bits.
            if (!resampleAll && oldK >= 0 && rank < oldK)
                continue;
            GrassBlade blade;
            if (!acceptCandidate(grid, i, tileSeed, originX, originZ, tileSlot, waterLevel, blade))
                continue;
            m_scratch.push_back(blade);
            setBit(newBits, i);
        }
    }

    bool GrassField::commitBlades(size_t index, int newLod, int newK, const uint64_t* newBits, bool clearDirty)
    {
        Tile&          tile     = m_tiles[index];
        const int      oldLod   = tile.lod;
        const uint32_t oldBegin = tile.begin;
        const uint32_t oldCount = tile.count;
        const bool     wasDirty = tile.dirty;
        const uint32_t newCount = static_cast<uint32_t>(m_scratch.size());

        uint32_t newBegin = 0;
        if (oldLod == newLod)
        {
            // The tile's own blades pay for a same-region rebuild. A full region can still refresh.
            removeSpan(oldLod, oldBegin, oldCount);
            newBegin = appendBlades(newLod, m_scratch.data(), newCount);
        }
        else
        {
            newBegin = appendBlades(newLod, m_scratch.data(), newCount);
            removeSpan(oldLod, oldBegin, oldCount);
        }

        Tile& placed = m_tiles[index];
        placed.lod   = newLod;
        placed.keepK = newK;
        placed.begin = newBegin;
        placed.count = newCount;
        placed.dirty = clearDirty ? false : wasDirty;
        std::memcpy(placed.kept, newBits, sizeof(placed.kept));
        return true;
    }

    bool GrassField::tryCreate(const TerrainGrid& grid, int tileX, int tileZ, int lod, float waterLevel)
    {
        const float cx = tileCenterCoord(tileX);
        const float cz = tileCenterCoord(tileZ);
        if (!splatReady(grid, cx, cz))
        {
            if (!m_loggedNoSplat)
            {
                m_loggedNoSplat = true;
                DE_LOG_WARN(LogCategory::Render, "GrassField: no splat, skipping grass tile ({}, {})", tileX, tileZ);
            }
            return false;
        }

        const uint32_t free = kRegionSlots[lod] - static_cast<uint32_t>(m_region[lod].blades.size());
        const int      k    = fitKeep(lod, m_params.densityScale, free, -1, 0, nullptr);
        if (k < 0)
        {
            warnFull(lod);
            return false;
        }

        const uint32_t slot = allocWindSlot();
        if (slot >= kGrassWindSlots)
            return false;

        uint64_t bits[64]{};
        buildSubset(grid, tileX, tileZ, k, -1, nullptr, nullptr, 0, slot, waterLevel, true, bits);
        const uint32_t count = static_cast<uint32_t>(m_scratch.size());
        if (count > free)
        {
            freeWindSlot(slot);
            warnFull(lod);
            return false;
        }

        Tile tile;
        tile.tileX    = tileX;
        tile.tileZ    = tileZ;
        tile.lod      = lod;
        tile.keepK    = k;
        tile.windSlot = slot;
        tile.begin    = appendBlades(lod, m_scratch.data(), count);
        tile.count    = count;
        tile.dirty    = false;
        std::memcpy(tile.kept, bits, sizeof(tile.kept));

        const uint32_t index = static_cast<uint32_t>(m_tiles.size());
        m_tiles.push_back(tile);
        m_lookup[tileKey(tileX, tileZ)] = index;
        return true;
    }

    bool GrassField::tryPromote(const TerrainGrid& grid, size_t index, int newLod, float waterLevel)
    {
        const Tile&    tile = m_tiles[index];
        const uint32_t free = kRegionSlots[newLod] - static_cast<uint32_t>(m_region[newLod].blades.size());
        const int      k    = fitKeep(newLod, m_params.densityScale, free, tile.keepK, tile.count, tile.kept);
        if (k < 0)
        {
            warnFull(newLod);
            return false;
        }

        const GrassBlade* oldBlades = tile.count > 0 ? m_region[tile.lod].blades.data() + tile.begin : nullptr;
        uint64_t          bits[64]{};
        buildSubset(grid, tile.tileX, tile.tileZ, k, tile.keepK, tile.kept, oldBlades, tile.count, tile.windSlot, waterLevel, false, bits);
        if (m_scratch.size() > free)
        {
            warnFull(newLod);
            return false;
        }
        return commitBlades(index, newLod, k, bits, true);
    }

    bool GrassField::tryRebuild(const TerrainGrid& grid, size_t index, float waterLevel)
    {
        const Tile&    tile = m_tiles[index];
        const uint32_t used = static_cast<uint32_t>(m_region[tile.lod].blades.size());
        const uint32_t free = kRegionSlots[tile.lod] - used;
        const uint32_t room = free + tile.count;
        const int      k    = fitKeep(tile.lod, m_params.densityScale, room, -1, 0, nullptr);
        if (k < 0)
        {
            warnFull(tile.lod);
            return false;
        }

        uint64_t bits[64]{};
        buildSubset(grid, tile.tileX, tile.tileZ, k, -1, nullptr, nullptr, 0, tile.windSlot, waterLevel, true, bits);
        if (m_scratch.size() > room)
        {
            warnFull(tile.lod);
            return false;
        }
        return commitBlades(index, tile.lod, k, bits, true);
    }

    bool GrassField::tryDemote(const TerrainGrid& grid, size_t index)
    {
        const Tile& tile   = m_tiles[index];
        const int   newLod = tile.lod + 1;
        if (newLod >= kGrassLodCount)
            return false;
        // Demotion only drops blades. Never widen the predicate, or the filter would sample height.
        int k = keepThreshold(newLod, m_params.densityScale);
        if (k > tile.keepK)
            k = tile.keepK;
        const int      need = countKept(tile.kept, k);
        const uint32_t free = kRegionSlots[newLod] - static_cast<uint32_t>(m_region[newLod].blades.size());
        if (static_cast<uint32_t>(need) > free)
        {
            warnFull(newLod);
            return false;
        }

        const GrassBlade* oldBlades = tile.count > 0 ? m_region[tile.lod].blades.data() + tile.begin : nullptr;
        uint64_t          bits[64]{};
        buildSubset(grid, tile.tileX, tile.tileZ, k, tile.keepK, tile.kept, oldBlades, tile.count, tile.windSlot, 0.0f, false, bits);
        if (m_scratch.size() > free)
        {
            warnFull(newLod);
            return false;
        }
        return commitBlades(index, newLod, k, bits, false);
    }

    void GrassField::sampleWind(float playerX, float playerZ, double playTimeSec)
    {
        if (m_wind.empty())
            return;
        const float tip = m_params.windTipMetres;
        for (const Tile& tile : m_tiles)
        {
            const float  cx     = tileCenterCoord(tile.tileX);
            const float  cz     = tileCenterCoord(tile.tileZ);
            const double d2     = dist2(cx, cz, playerX, playerZ);
            const float  d      = static_cast<float>(std::sqrt(d2));
            const GrassWindSample sample = sampleGrassWind(cx, cz, playTimeSec, m_params);
            GrassTileWind& wind = m_wind[tile.windSlot];
            wind.windX          = sample.dirX * sample.strength * tip;
            wind.windZ          = sample.dirZ * sample.strength * tip;
            wind.pad            = 0.0f;
            if (d <= kGrassFadeStartMetres)
            {
                wind.fade = 1.0f;
                continue;
            }
            const float span = kGrassFadeEndMetres - kGrassFadeStartMetres;
            float       fade = span > 0.0f ? (kGrassFadeEndMetres - d) / span : 0.0f;
            if (fade < 0.0f)
                fade = 0.0f;
            if (fade > 1.0f)
                fade = 1.0f;
            wind.fade = fade;
        }
    }

    void GrassField::update(const TerrainGrid& grid, float playerX, float playerZ, double playTimeSec, float waterLevel, const GrassPlayerSample& player)
    {
        if (!m_params.enabled || !grid.valid())
        {
            reset();
            return;
        }

        sampleWind(playerX, playerZ, playTimeSec);
        updateInteraction(player, playTimeSec);

        std::vector<Demote> demotes;
        demotes.reserve(m_tiles.size());
        for (size_t i = 0; i < m_tiles.size(); ++i)
        {
            const Tile&  tile = m_tiles[i];
            const double d2   = dist2(tileCenterCoord(tile.tileX), tileCenterCoord(tile.tileZ), playerX, playerZ);
            if (d2 > sq(kGrassEvictRadiusMetres))
                continue;
            if (demoteTo(tile.lod, d2) < 0)
                continue;
            Demote item;
            item.dist2 = d2;
            item.index = i;
            demotes.push_back(item);
        }
        std::sort(demotes.begin(), demotes.end(), closerDemote);

        int demoteMoves = 0;
        for (const Demote& item : demotes)
        {
            if (demoteMoves >= kDemoteBudget)
                break;
            if (item.index >= m_tiles.size())
                continue;
            const Tile& tile = m_tiles[item.index];
            const double d2  = dist2(tileCenterCoord(tile.tileX), tileCenterCoord(tile.tileZ), playerX, playerZ);
            if (demoteTo(tile.lod, d2) < 0)
                continue;
            if (tryDemote(grid, item.index))
                ++demoteMoves;
        }

        for (size_t i = m_tiles.size(); i-- > 0;)
        {
            const Tile&  tile = m_tiles[i];
            const double d2   = dist2(tileCenterCoord(tile.tileX), tileCenterCoord(tile.tileZ), playerX, playerZ);
            if (d2 > sq(kGrassEvictRadiusMetres))
                eraseTileAt(i);
        }

        const float reach = kGrassCreateRadiusMetres;
        const int   minX  = static_cast<int>(std::floor((static_cast<double>(playerX) - reach) / static_cast<double>(kGrassTileMetres))) - 1;
        const int   maxX  = static_cast<int>(std::floor((static_cast<double>(playerX) + reach) / static_cast<double>(kGrassTileMetres))) + 1;
        const int   minZ  = static_cast<int>(std::floor((static_cast<double>(playerZ) - reach) / static_cast<double>(kGrassTileMetres))) - 1;
        const int   maxZ  = static_cast<int>(std::floor((static_cast<double>(playerZ) + reach) / static_cast<double>(kGrassTileMetres))) + 1;

        std::vector<Job> jobs;
        bool             warm = false;
        for (int tz = minZ; tz <= maxZ; ++tz)
        {
            for (int tx = minX; tx <= maxX; ++tx)
            {
                const float  cx = tileCenterCoord(tx);
                const float  cz = tileCenterCoord(tz);
                const double d2 = dist2(cx, cz, playerX, playerZ);
                if (d2 > sq(kGrassCreateRadiusMetres))
                    continue;
                if (!grid.containsXZ(cx, cz))
                    continue;
                if (findTile(tx, tz))
                    continue;
                if (d2 < sq(kWarmRadius))
                    warm = true;
                Job job;
                job.dist2 = d2;
                job.tileX = tx;
                job.tileZ = tz;
                job.kind  = 0;
                job.lod   = nominalLod(d2);
                jobs.push_back(job);
            }
        }

        for (size_t i = 0; i < m_tiles.size(); ++i)
        {
            const Tile&  tile = m_tiles[i];
            const double d2   = dist2(tileCenterCoord(tile.tileX), tileCenterCoord(tile.tileZ), playerX, playerZ);
            if (tile.dirty)
            {
                Job job;
                job.dist2 = d2;
                job.tileX = tile.tileX;
                job.tileZ = tile.tileZ;
                job.kind  = 2;
                job.lod   = tile.lod;
                job.index = i;
                jobs.push_back(job);
                continue;
            }
            const int target = promoteTo(tile.lod, d2);
            if (target < 0)
                continue;
            if (d2 < sq(kWarmRadius))
                warm = true;
            Job job;
            job.dist2 = d2;
            job.tileX = tile.tileX;
            job.tileZ = tile.tileZ;
            job.kind  = 1;
            job.lod   = target;
            job.index = i;
            jobs.push_back(job);
        }

        std::sort(jobs.begin(), jobs.end(), closerJob);
        const int budget = warm ? kGenerateWarm : kGenerateBudget;
        int       done   = 0;
        for (const Job& job : jobs)
        {
            if (done >= budget)
                break;
            bool admitted = false;
            if (job.kind == 0)
            {
                if (findTile(job.tileX, job.tileZ))
                    continue;
                admitted = tryCreate(grid, job.tileX, job.tileZ, job.lod, waterLevel);
            }
            else if (job.kind == 1)
            {
                if (job.index >= m_tiles.size())
                    continue;
                const Tile& tile = m_tiles[job.index];
                if (tile.tileX != job.tileX || tile.tileZ != job.tileZ)
                    continue;
                if (promoteTo(tile.lod, dist2(tileCenterCoord(tile.tileX), tileCenterCoord(tile.tileZ), playerX, playerZ)) != job.lod)
                    continue;
                admitted = tryPromote(grid, job.index, job.lod, waterLevel);
            }
            else
            {
                if (job.index >= m_tiles.size())
                    continue;
                const Tile& tile = m_tiles[job.index];
                if (tile.tileX != job.tileX || tile.tileZ != job.tileZ || !tile.dirty)
                    continue;
                admitted = tryRebuild(grid, job.index, waterLevel);
            }
            if (admitted)
                ++done;
        }

        // Tiles admitted above still need a wind row this frame.
        sampleWind(playerX, playerZ, playTimeSec);
    }

} // namespace Dark::Terrain
