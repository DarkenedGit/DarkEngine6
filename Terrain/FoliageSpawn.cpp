#include "Terrain/FoliageSpawn.h"

#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Terrain/TerrainGen.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace Dark::Terrain
{
    namespace
    {
        constexpr int      kFlowerSlots       = 2;
        constexpr uint32_t kSaltTreeAccept    = 0x1000u;
        constexpr uint32_t kSaltFlowerAccept  = 0x2000u;
        constexpr uint32_t kSaltRockAccept    = 0x3000u;
        constexpr uint32_t kSaltJitterX       = 0x4000u;
        constexpr uint32_t kSaltFlowerJitterX = 0x4100u;
        constexpr uint32_t kSaltJitterZ       = 0x5000u;
        constexpr uint32_t kSaltFlowerJitterZ = 0x5100u;
        constexpr uint32_t kSaltYaw           = 0x6000u;
        constexpr uint32_t kSaltScale         = 0x7000u;
        constexpr float    kTreeRockJitter    = 0.45f;
        constexpr float    kFlowerJitter      = 0.12f;
        constexpr float    kFlowerOffsetX[kFlowerSlots] = { -0.25f, 0.25f };

        bool failSpawn(FoliageSpawnOut& out)
        {
            std::vector<FoliageRecord>().swap(out.records);
            out.accepted = 0;
            out.kept     = 0;
            out.capped   = false;
            return false;
        }

        float clamp01(float v)
        {
            return Math::Clamp(v, 0.0f, 1.0f);
        }

        float clampFlower(float v)
        {
            return Math::Clamp(v, 0.0f, 2.0f);
        }
    } // namespace

    bool spawnFoliage(const FoliageSpawnIn& in, FoliageSpawnOut& out)
    {
        using namespace Math;

        out.accepted = 0;
        out.kept     = 0;
        out.capped   = false;
        out.records.clear();

        if (in.height == nullptr)
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliageSpawn: null height");
            return failSpawn(out);
        }
        if (!in.height->valid())
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliageSpawn: invalid height");
            return failSpawn(out);
        }
        if (in.splat == nullptr)
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliageSpawn: null splat");
            return failSpawn(out);
        }
        if (!in.splat->valid())
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliageSpawn: invalid splat");
            return failSpawn(out);
        }
        if (in.tilesX == 0 || in.tilesZ == 0 || in.tileCells == 0 || !(in.cellSize > 0.0f))
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliageSpawn: grid extents must be positive");
            return failSpawn(out);
        }

        const Vector3f& origin = in.height->origin();
        if (in.cellSize != in.height->cellSize() || in.origin.x != origin.x || in.origin.y != origin.y || in.origin.z != origin.z)
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliageSpawn: cellSize or origin disagrees with the height map");
            return failSpawn(out);
        }

        const uint64_t spanX = static_cast<uint64_t>(in.tilesX) * static_cast<uint64_t>(in.tileCells);
        const uint64_t spanZ = static_cast<uint64_t>(in.tilesZ) * static_cast<uint64_t>(in.tileCells);
        if (static_cast<uint64_t>(in.height->width()) - 1ull != spanX || static_cast<uint64_t>(in.height->height()) - 1ull != spanZ)
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliageSpawn: tile span disagrees with the height map");
            return failSpawn(out);
        }

        const double widthM = static_cast<double>(spanX) * static_cast<double>(in.cellSize);
        const double depthM = static_cast<double>(spanZ) * static_cast<double>(in.cellSize);
        if (!(widthM >= 0.0) || !(depthM >= 0.0) || widthM > static_cast<double>(INT32_MAX) || depthM > static_cast<double>(INT32_MAX))
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliageSpawn: lattice does not fit");
            return failSpawn(out);
        }

        const uint32_t cols = static_cast<uint32_t>(std::floor(widthM));
        const uint32_t rows = static_cast<uint32_t>(std::floor(depthM));

        const float dirtTrees   = clamp01(in.density.dirtTreesPerM2);
        const float grassTrees  = clamp01(in.density.grassTreesPerM2);
        const float dirtFlowers = clampFlower(in.density.dirtFlowersPerM2);
        const float grassFlowers = clampFlower(in.density.grassFlowersPerM2);
        const float rockPerM2   = clamp01(in.density.rockPerM2);
        const uint32_t seed     = in.density.seed;
        const float seaLevel    = in.seaLevel;
        const HeightMap* height = in.height;
        const SplatMap* splat   = in.splat;
        const uint64_t cap      = kMaxFoliageInstances;

        bool     writing = false;
        uint64_t counted = 0;
        uint64_t total   = 0;
        uint64_t acc     = 0;
        uint32_t nTree   = 0;
        uint32_t nFlower = 0;
        uint32_t nRock   = 0;

        auto consider = [&](FoliageKind kind, int slot, float anchorX, float anchorZ, int ix, int iz)
        {
            const bool     flower  = kind == FoliageKind::Flower;
            const uint32_t slotU   = static_cast<uint32_t>(slot);
            const uint32_t kindU   = static_cast<uint32_t>(kind);
            const uint32_t jxSalt  = flower ? (kSaltFlowerJitterX + slotU) : kSaltJitterX;
            const uint32_t jzSalt  = flower ? (kSaltFlowerJitterZ + slotU) : kSaltJitterZ;
            const float    jitter  = flower ? kFlowerJitter : kTreeRockJitter;
            const float    jx      = (hash21(ix, iz, seed ^ jxSalt) * 2.0f - 1.0f) * jitter;
            const float    jz      = (hash21(ix, iz, seed ^ jzSalt) * 2.0f - 1.0f) * jitter;
            const float    x       = anchorX + jx;
            const float    z       = anchorZ + jz;
            if (!height->containsXZ(x, z))
                return;

            float fx = 0.0f;
            float fz = 0.0f;
            height->worldToSample(x, z, fx, fz);
            float w[kMaxTerrainLayers];
            splat->sampleWeights(fx, fz, w);

            float p = 0.0f;
            if (kind == FoliageKind::Tree)
                p = dirtTrees * w[0] + grassTrees * w[1];
            else if (kind == FoliageKind::Flower)
            {
                const float pFlower = dirtFlowers * w[0] + grassFlowers * w[1];
                p                   = pFlower - static_cast<float>(slot);
                if (p < 0.0f)
                    p = 0.0f;
                else if (p > 1.0f)
                    p = 1.0f;
            }
            else
                p = rockPerM2 * w[2];

            bool accept = false;
            if (p >= 1.0f)
                accept = true;
            else if (p > 0.0f)
            {
                uint32_t acceptSalt = kSaltTreeAccept;
                if (kind == FoliageKind::Flower)
                    acceptSalt = kSaltFlowerAccept + slotU;
                else if (kind == FoliageKind::Rock)
                    acceptSalt = kSaltRockAccept;
                accept = hash21(ix, iz, seed ^ acceptSalt) < p;
            }
            if (!accept)
                return;

            if (!writing)
            {
                ++counted;
                return;
            }

            bool keep = true;
            if (total > cap)
            {
                acc += cap;
                if (acc >= total)
                    acc -= total;
                else
                    keep = false;
            }
            if (!keep)
                return;

            uint32_t yawSalt   = kSaltYaw + kindU;
            uint32_t scaleSalt = kSaltScale + kindU;
            if (flower)
            {
                const uint32_t slotSalt = slotU * 16u;
                yawSalt += slotSalt;
                scaleSalt += slotSalt;
            }

            float scaleMin = 0.85f;
            float scaleMax = 1.15f;
            if (kind == FoliageKind::Flower)
            {
                scaleMin = 0.80f;
                scaleMax = 1.20f;
            }
            else if (kind == FoliageKind::Rock)
            {
                scaleMin = 0.60f;
                scaleMax = 1.80f;
            }

            FoliageRecord rec{};
            rec.x     = x;
            rec.y     = height->heightAtWorld(x, z);
            rec.z     = z;
            rec.yaw   = hash21(ix, iz, seed ^ yawSalt) * TwoPi;
            rec.scale = scaleMin + hash21(ix, iz, seed ^ scaleSalt) * (scaleMax - scaleMin);
            rec.kind  = static_cast<uint8_t>(kind);
            if (kind == FoliageKind::Rock)
            {
                const Vector3f n = height->normalAtWorld(x, z);
                if (!(n.y > 0.999f))
                {
                    rec.pitch   = acosf(Clamp(n.y, -1.0f, 1.0f));
                    rec.tiltYaw = atan2f(n.x, n.z);
                }
            }

            out.records.push_back(rec);
            if (kind == FoliageKind::Tree)
                ++nTree;
            else if (kind == FoliageKind::Flower)
                ++nFlower;
            else
                ++nRock;
        };

        auto scan = [&]() -> bool
        {
            const bool placeTrees   = dirtTrees > 0.0f || grassTrees > 0.0f;
            const bool placeFlowers = dirtFlowers > 0.0f || grassFlowers > 0.0f;
            const bool placeRocks   = rockPerM2 > 0.0f;
            for (uint32_t cellZ = 0; cellZ < rows; ++cellZ)
            {
                const int   iz = static_cast<int>(cellZ);
                const float centreZ = in.origin.z + static_cast<float>(cellZ) + 0.5f;
                for (uint32_t cellX = 0; cellX < cols; ++cellX)
                {
                    const int   ix = static_cast<int>(cellX);
                    const float centreX = in.origin.x + static_cast<float>(cellX) + 0.5f;
                    if (!height->containsXZ(centreX, centreZ))
                        continue;
                    // One centre sample gates the cell. Accepted props still store Y at their own XZ.
                    if (height->heightAtWorld(centreX, centreZ) < seaLevel)
                        continue;

                    if (placeTrees)
                        consider(FoliageKind::Tree, 0, centreX, centreZ, ix, iz);
                    if (placeFlowers)
                    {
                        for (int slot = 0; slot < kFlowerSlots; ++slot)
                            consider(FoliageKind::Flower, slot, centreX + kFlowerOffsetX[slot], centreZ, ix, iz);
                    }
                    if (placeRocks)
                        consider(FoliageKind::Rock, 0, centreX, centreZ, ix, iz);
                }

                if (in.onProgress != nullptr)
                {
                    const float u     = static_cast<float>(cellZ + 1u) / static_cast<float>(rows);
                    const float t     = writing ? (0.5f + 0.5f * u) : (0.5f * u);
                    const char* phase = writing ? "keep" : "count";
                    if (!in.onProgress(t, phase, in.user))
                        return false;
                }
            }
            return true;
        };

        if (cols == 0 || rows == 0)
        {
            DE_LOG_INFO(LogCategory::Render, "FoliageSpawn: kept 0 accepted 0 capped 0 seed {}", seed);
            return true;
        }

        if (!scan())
        {
            DE_LOG_INFO(LogCategory::Render, "FoliageSpawn: cancelled");
            return failSpawn(out);
        }

        total = counted;
        const uint64_t keepN = total < cap ? total : cap;
        if (keepN > 0)
            out.records.reserve(static_cast<size_t>(keepN));

        writing = true;
        // total/2 spreads keeps across the scan instead of taking a prefix at the origin.
        acc = total / 2ull;
        if (!scan())
        {
            DE_LOG_INFO(LogCategory::Render, "FoliageSpawn: cancelled");
            return failSpawn(out);
        }

        out.accepted = total;
        out.kept     = static_cast<uint32_t>(out.records.size());
        out.capped   = total > cap;
        if (out.capped)
            DE_LOG_WARN(LogCategory::Render, "FoliageSpawn: cap thinned accepted {} to {}", total, cap);
        DE_LOG_INFO(LogCategory::Render, "FoliageSpawn: kept {} accepted {} capped {} seed {} trees {} flowers {} rocks {}", out.kept, out.accepted, out.capped ? 1 : 0, seed, nTree, nFlower, nRock);
        return true;
    }

} // namespace Dark::Terrain
