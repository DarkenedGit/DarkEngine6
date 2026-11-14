#include "Terrain/FoliageSpawn.h"

#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Terrain/TerrainGen.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Dark::Terrain
{
    namespace
    {
        constexpr int      kFlowerSlots       = 2;
        constexpr uint32_t kSaltTreeAccept    = 0x1000u;
        constexpr uint32_t kSaltFlowerAccept  = 0x2000u;
        constexpr uint32_t kSaltRockAccept    = 0x3000u;
        constexpr uint32_t kSaltGrassAccept   = 0x3500u;
        constexpr uint32_t kSaltJitterX       = 0x4000u;
        constexpr uint32_t kSaltFlowerJitterX = 0x4100u;
        constexpr uint32_t kSaltGrassJitterX  = 0x4500u;
        constexpr uint32_t kSaltJitterZ       = 0x5000u;
        constexpr uint32_t kSaltFlowerJitterZ = 0x5100u;
        constexpr uint32_t kSaltGrassJitterZ  = 0x5500u;
        constexpr uint32_t kSaltYaw           = 0x6000u;
        constexpr uint32_t kSaltScale         = 0x7000u;
        constexpr uint32_t kSaltChild         = 0x8000u;
        constexpr uint32_t kSaltChildRadius   = 0x8100u;
        constexpr uint32_t kSaltChildKind     = 0x8200u;
        constexpr uint32_t kSaltChildCount    = 0x8300u;
        constexpr uint32_t kSaltChildScale    = 0x8400u;
        constexpr uint32_t kSaltPebble        = 0x8600u;
        constexpr uint32_t kSaltPebbleCount   = 0x8700u;
        constexpr uint32_t kSaltPebbleLateral = 0x8800u;
        constexpr uint32_t kSaltPebbleScale   = 0x8900u;
        constexpr uint32_t kSaltPatchLarge    = 0x9000u;
        constexpr uint32_t kSaltPatchMedium   = 0x9100u;
        constexpr uint32_t kSaltPatchFine     = 0x9200u;
        constexpr int      kMaxChildrenPerParent = 8;
        constexpr float    kPebbleParentScale = 1.2f;
        constexpr float    kTreeRockJitter    = 0.45f;
        constexpr float    kFlowerJitter      = 0.12f;
        constexpr float    kFlowerOffsetX[kFlowerSlots] = { -0.25f, 0.25f };
        constexpr uint8_t  kCanopyHeightByte  = 89;

        struct SpacePoint
        {
            float x     = 0.0f;
            float z     = 0.0f;
            float scale = 1.0f;
        };

        struct SpaceHash
        {
            float spacing = 0.0f;
            std::unordered_map<uint64_t, std::vector<SpacePoint>> cells;

            static uint64_t key(int x, int z)
            {
                return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(z);
            }

            bool cellOf(float world, int& out) const
            {
                const double q = static_cast<double>(world) / static_cast<double>(spacing);
                if (q < static_cast<double>(INT32_MIN) || q > static_cast<double>(INT32_MAX))
                    return false;
                out = static_cast<int>(std::floor(q));
                return true;
            }

            bool blocked(float x, float z, float scale) const
            {
                if (!(spacing > 0.0f))
                    return false;
                int hx = 0;
                int hz = 0;
                if (!cellOf(x, hx) || !cellOf(z, hz))
                    return false;
                const double limitBase = static_cast<double>(spacing) * 0.5;
                for (int dz = -2; dz <= 2; ++dz)
                {
                    for (int dx = -2; dx <= 2; ++dx)
                    {
                        const auto it = cells.find(key(hx + dx, hz + dz));
                        if (it == cells.end())
                            continue;
                        for (const SpacePoint& point : it->second)
                        {
                            const double lim = limitBase * (static_cast<double>(scale) + static_cast<double>(point.scale));
                            const double ddx = static_cast<double>(x) - static_cast<double>(point.x);
                            const double ddz = static_cast<double>(z) - static_cast<double>(point.z);
                            if (ddx * ddx + ddz * ddz < lim * lim)
                                return true;
                        }
                    }
                }
                return false;
            }

            void insert(float x, float z, float scale)
            {
                if (!(spacing > 0.0f))
                    return;
                int hx = 0;
                int hz = 0;
                if (!cellOf(x, hx) || !cellOf(z, hz))
                    return;
                cells[key(hx, hz)].push_back(SpacePoint{ x, z, scale });
            }
        };

        bool failSpawn(FoliageSpawnOut& out)
        {
            std::vector<FoliageRecord>().swap(out.records);
            out.cover.reset();
            out.accepted = 0;
            out.kept     = 0;
            out.capped   = false;
            return false;
        }

        bool coveredByWater(const std::vector<FoliageWaterVolume>& water, float x, float z, float bedY)
        {
            for (const FoliageWaterVolume& volume : water)
            {
                if (!(bedY < volume.surfaceY))
                    continue;
                if (volume.entireMap || (x >= volume.minX && x <= volume.maxX && z >= volume.minZ && z <= volume.maxZ))
                    return true;
            }
            return false;
        }

        struct TreeDisk
        {
            float trunk  = 0.0f;
            float canopy = 0.0f;
        };

        struct ParentRef
        {
            int ix = 0;
            int iz = 0;
        };

        struct LargeMaskPin
        {
            float x      = 0.0f;
            float z      = 0.0f;
            float radius = 0.0f;
            float mask   = 0.0f;
        };

        int noiseCell(float v)
        {
            return static_cast<int>(std::floor(static_cast<double>(v)));
        }

        float fade(float t)
        {
            return t * t * (3.0f - 2.0f * t);
        }

        float valueNoise(float x, float z, float wavelength, uint32_t salt, uint32_t seed)
        {
            const float inv = 1.0f / wavelength;
            const float fx  = x * inv;
            const float fz  = z * inv;
            const int   x0  = noiseCell(fx);
            const int   z0  = noiseCell(fz);
            const float tx  = fade(fx - static_cast<float>(x0));
            const float tz  = fade(fz - static_cast<float>(z0));
            const float v00 = hash21(x0, z0, seed ^ salt);
            const float v10 = hash21(x0 + 1, z0, seed ^ salt);
            const float v01 = hash21(x0, z0 + 1, seed ^ salt);
            const float v11 = hash21(x0 + 1, z0 + 1, seed ^ salt);
            const float a   = v00 + (v10 - v00) * tx;
            const float b   = v01 + (v11 - v01) * tx;
            return a + (b - a) * tz;
        }

        float fbm(float x, float z, float wavelength, uint32_t salt, uint32_t seed)
        {
            float amp  = 1.0f;
            float sum  = 0.0f;
            float norm = 0.0f;
            float wl   = wavelength;
            for (int oct = 0; oct < 3; ++oct)
            {
                sum += amp * valueNoise(x, z, wl, salt + static_cast<uint32_t>(oct) * 17u, seed);
                norm += amp;
                amp *= 0.5f;
                wl *= 0.5f;
            }
            return norm > 0.0f ? sum / norm : 0.0f;
        }

        // 1 at a hashed cell seed, falling to 0 between the 3x3 neighbours.
        float worleyGrove(float x, float z, float wavelength, uint32_t salt, uint32_t seed)
        {
            const float inv = 1.0f / wavelength;
            const float fx  = x * inv;
            const float fz  = z * inv;
            const int   cx  = noiseCell(fx);
            const int   cz  = noiseCell(fz);
            float       best = 2.0f;
            for (int dz = -1; dz <= 1; ++dz)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int   nx  = cx + dx;
                    const int   nz  = cz + dz;
                    const float px  = static_cast<float>(nx) + hash21(nx, nz, seed ^ salt);
                    const float pz  = static_cast<float>(nz) + hash21(nx, nz, seed ^ (salt * 0x9E3779B9u + 0x85EBCA6Bu));
                    const float ddx = fx - px;
                    const float ddz = fz - pz;
                    const float d   = std::sqrt(ddx * ddx + ddz * ddz);
                    if (d < best)
                        best = d;
                }
            }
            const float u = best > 1.0f ? 1.0f : best;
            return 1.0f - u;
        }

        TreeDisk treeDisk(const FoliageRecord& rec, float extentX, float extentZ)
        {
            TreeDisk disk;
            disk.trunk  = 0.5f * rec.scale;
            disk.canopy = 2.0f * rec.scale;
            if (extentX > 0.0f && extentZ > 0.0f)
            {
                const float mn = extentX < extentZ ? extentX : extentZ;
                const float mx = extentX > extentZ ? extentX : extentZ;
                disk.trunk  = 0.25f * mn * rec.scale;
                disk.canopy = 0.45f * mx * rec.scale;
            }
            return disk;
        }

        void thinPaired(std::vector<FoliageRecord>& recs, std::vector<ParentRef>& refs, uint64_t keepCap)
        {
            const uint64_t total = static_cast<uint64_t>(recs.size());
            if (keepCap >= total)
                return;
            uint64_t acc   = total / 2ull;
            size_t   write = 0;
            for (size_t i = 0; i < recs.size(); ++i)
            {
                acc += keepCap;
                if (acc >= total)
                {
                    acc -= total;
                    if (write != i)
                    {
                        recs[write] = recs[i];
                        refs[write] = refs[i];
                    }
                    ++write;
                }
            }
            recs.resize(write);
            refs.resize(write);
        }

        void stampTree(FoliageCover& cover, const FoliageRecord& rec, const FoliageSpawnIn& in)
        {
            const TreeDisk disk = treeDisk(rec, in.treeExtentX, in.treeExtentZ);
            cover.stampDisk(rec.x, rec.z, disk.trunk, 0, 0, true);
            if (disk.canopy > disk.trunk)
                cover.stampSoftAnnulus(rec.x, rec.z, disk.trunk, disk.canopy, 0, 255, kCanopyHeightByte, 255, true);
        }

        bool cancelled(const FoliageSpawnIn& in, float t, const char* phase)
        {
            return in.onProgress != nullptr && !in.onProgress(t, phase, in.user);
        }

        void rasterTrails(FoliageCover& cover, const FoliageSpawnIn& in)
        {
            const uint32_t seed = in.density.seed;
            for (size_t i = 0; i < in.trails.size(); ++i)
                rasterTrail(cover, in.trails[i], seed, static_cast<uint32_t>(i + 1u));
        }
    } // namespace

    bool spawnFoliageImpl(const FoliageSpawnIn& in, FoliageSpawnOut& out, const LargeMaskPin pins[2])
    {
        using namespace Math;

        out.accepted = 0;
        out.kept     = 0;
        out.capped   = false;
        out.records.clear();
        out.cover.reset();

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

        const float dirtTrees    = Math::Clamp(in.density.dirtTreesPerM2, 0.0f, 1.0f);
        const float grassTrees   = Math::Clamp(in.density.grassTreesPerM2, 0.0f, 1.0f);
        const float dirtFlowers  = Math::Clamp(in.density.dirtFlowersPerM2, 0.0f, 2.0f);
        const float grassFlowers = Math::Clamp(in.density.grassFlowersPerM2, 0.0f, 2.0f);
        const float rockPerM2    = Math::Clamp(in.density.rockPerM2, 0.0f, 1.0f);
        const float grassPerM2   = Math::Clamp(in.density.grassPerM2, 0.0f, 1.0f);
        const float treeMinNormalY   = Clamp(in.density.treeMinNormalY, 0.0f, 1.0f);
        const float flowerMinNormalY = Clamp(in.density.flowerMinNormalY, 0.0f, 1.0f);
        const float tuftMinNormalY   = Clamp(in.density.tuftMinNormalY, 0.0f, 1.0f);
        const float rockMinNormalY   = Clamp(in.density.rockMinNormalY, 0.0f, 1.0f);
        const float treeMaxLeanDeg   = Clamp(in.density.treeMaxLeanDeg, 0.0f, 15.0f);
        const float rockMaxPitchDeg  = Clamp(in.density.rockMaxPitchDeg, 0.0f, 89.0f);
        const float treeSpacingM     = Clamp(in.density.treeSpacingM, 0.0f, 16.0f);
        const float rockSpacingM     = Clamp(in.density.rockSpacingM, 0.0f, 16.0f);
        const float patchStrength    = Clamp(in.density.patchStrength, 0.0f, 1.0f);
        const float patchLargeM      = Clamp(in.density.patchLargeM, 60.0f, 120.0f);
        const float patchMediumM     = Clamp(in.density.patchMediumM, 15.0f, 30.0f);
        const float patchFineM       = Clamp(in.density.patchFineM, 2.0f, 6.0f);
        const float treeMaxLeanRad   = treeMaxLeanDeg * DegToRad;
        const float rockMaxPitchRad  = rockMaxPitchDeg * DegToRad;
        const uint32_t seed     = in.density.seed;
        const float seaLevel    = in.seaLevel;
        const HeightMap* height = in.height;
        const SplatMap* splat   = in.splat;
        const uint64_t cap      = kMaxFoliageInstances;

        if (cols == 0 || rows == 0)
        {
            DE_LOG_INFO(LogCategory::Render, "FoliageSpawn: kept 0 accepted 0 capped 0 seed {}", seed);
            return true;
        }

        if (!out.cover.create(static_cast<float>(widthM), static_cast<float>(depthM), in.origin.x, in.origin.z))
            return failSpawn(out);
        out.cover.clear(255, 255);
        if (!in.trails.empty())
        {
            if (cancelled(in, 0.0f, "trails"))
            {
                DE_LOG_INFO(LogCategory::Render, "FoliageSpawn: cancelled");
                return failSpawn(out);
            }
            rasterTrails(out.cover, in);
        }
        if (cancelled(in, 0.0f, "cover"))
        {
            DE_LOG_INFO(LogCategory::Render, "FoliageSpawn: cancelled");
            return failSpawn(out);
        }

        const bool placeTrees   = dirtTrees > 0.0f || grassTrees > 0.0f;
        const bool placeFlowers = dirtFlowers > 0.0f || grassFlowers > 0.0f;
        const bool placeRocks   = rockPerM2 > 0.0f;
        const bool placeGrass   = grassPerM2 > 0.0f;

        std::vector<FoliageRecord> parents;
        std::vector<ParentRef>     parentRefs;
        SpaceHash treeHash;
        SpaceHash rockHash;
        treeHash.spacing = treeSpacingM;
        rockHash.spacing = rockSpacingM;

        bool     writing = false;
        uint64_t counted = 0;
        uint64_t total   = 0;
        uint64_t room    = cap;
        uint64_t acc     = 0;

        auto pushRecord = [&](FoliageKind kind, int slot, float x, float z, int ix, int iz, float scale, std::vector<FoliageRecord>& dest, uint32_t extraSalt)
        {
            const bool     flower = kind == FoliageKind::Flower;
            const uint32_t slotU  = static_cast<uint32_t>(slot);
            const uint32_t kindU  = static_cast<uint32_t>(kind);
            uint32_t yawSalt = kSaltYaw + kindU;
            if (flower)
                yawSalt += slotU * 16u;
            yawSalt ^= extraSalt;

            FoliageRecord rec{};
            rec.x     = x;
            rec.y     = height->heightAtWorld(x, z);
            rec.z     = z;
            rec.yaw   = hash21(ix, iz, seed ^ yawSalt) * TwoPi;
            rec.scale = scale;
            rec.kind  = static_cast<uint8_t>(kind);
            const Vector3f n = height->normalAtWorld(x, z);
            if (kind == FoliageKind::Tree)
            {
                if (treeMaxLeanDeg > 0.0f && !(n.y > 0.999f))
                {
                    const float slope = acosf(Clamp(n.y, -1.0f, 1.0f));
                    rec.pitch = slope < treeMaxLeanRad ? slope : treeMaxLeanRad;
                    if (rec.pitch != 0.0f)
                        rec.tiltYaw = atan2f(n.x, n.z);
                }
            }
            else if (kind == FoliageKind::Rock)
            {
                if (!(n.y > 0.999f))
                {
                    rec.pitch = acosf(Clamp(n.y, -1.0f, 1.0f));
                    if (rec.pitch > rockMaxPitchRad)
                        rec.pitch = rockMaxPitchRad;
                    if (rec.pitch != 0.0f)
                        rec.tiltYaw = atan2f(n.x, n.z);
                }
            }
            dest.push_back(rec);
        };

        auto largeAt = [&](float x, float z) -> float
        {
            float pinned = -1.0f;
            for (int i = 0; i < 2; ++i)
            {
                const LargeMaskPin& disk = pins[i];
                if (!(disk.radius > 0.0f))
                    continue;
                const float dx = x - disk.x;
                const float dz = z - disk.z;
                if (dx * dx + dz * dz <= disk.radius * disk.radius)
                    pinned = Clamp(disk.mask, 0.0f, 1.0f);
            }
            if (pinned >= 0.0f)
                return pinned;
            return fbm(x, z, patchLargeM, kSaltPatchLarge, seed);
        };

        auto kindMask = [&](FoliageKind kind, float x, float z) -> float
        {
            const float large = largeAt(x, z);
            float       mask  = 1.0f;
            if (kind == FoliageKind::Tree)
            {
                const float medium = worleyGrove(x, z, patchMediumM, kSaltPatchMedium, seed);
                mask = large * (0.35f + 0.65f * medium);
            }
            else if (kind == FoliageKind::Flower)
            {
                const float fine = fbm(x, z, patchFineM, kSaltPatchFine, seed);
                mask = (1.0f - large) * (0.70f + 0.30f * fine) + large * fine * 0.15f;
            }
            else if (kind == FoliageKind::Grass)
                mask = fbm(x, z, patchFineM, kSaltPatchFine, seed);
            return Clamp(mask, 0.0f, 1.0f);
        };

        auto consider = [&](FoliageKind kind, int slot, float anchorX, float anchorZ, int ix, int iz, bool parentPass, SpaceHash* hash)
        {
            const bool     flower = kind == FoliageKind::Flower;
            const bool     grass  = kind == FoliageKind::Grass;
            const uint32_t slotU  = static_cast<uint32_t>(slot);
            const uint32_t kindU  = static_cast<uint32_t>(kind);
            const uint32_t jxSalt = flower ? (kSaltFlowerJitterX + slotU) : (grass ? kSaltGrassJitterX : kSaltJitterX);
            const uint32_t jzSalt = flower ? (kSaltFlowerJitterZ + slotU) : (grass ? kSaltGrassJitterZ : kSaltJitterZ);
            const float    jitter = flower ? kFlowerJitter : kTreeRockJitter;
            const float    jx     = (hash21(ix, iz, seed ^ jxSalt) * 2.0f - 1.0f) * jitter;
            const float    jz     = (hash21(ix, iz, seed ^ jzSalt) * 2.0f - 1.0f) * jitter;
            const float    x      = anchorX + jx;
            const float    z      = anchorZ + jz;
            if (!height->containsXZ(x, z))
                return;
            if (!in.water.empty() && (kind == FoliageKind::Tree || kind == FoliageKind::Grass || kind == FoliageKind::Flower))
            {
                if (coveredByWater(in.water, x, z, height->heightAtWorld(x, z)))
                    return;
            }

            const Vector3f n = height->normalAtWorld(x, z);
            float minY = rockMinNormalY;
            if (kind == FoliageKind::Tree)
                minY = treeMinNormalY;
            else if (kind == FoliageKind::Flower)
                minY = flowerMinNormalY;
            else if (kind == FoliageKind::Grass)
                minY = tuftMinNormalY;
            if (n.y < minY)
                return;
            if (parentPass && (kind == FoliageKind::Tree || kind == FoliageKind::Rock) && out.cover.sampleNearest(x, z).coverage == 0)
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
            }
            else if (kind == FoliageKind::Grass)
                p = grassPerM2 * w[1];
            else
                p = rockPerM2 * w[2];

            if (flower || grass)
            {
                if (out.cover.sampleNearest(x, z).coverage == 0)
                    return;
                p *= static_cast<float>(out.cover.sampleBilinear(x, z).coverage) / 255.0f;
            }

            if (patchStrength > 0.0f && p > 0.0f && kind != FoliageKind::Rock)
                p *= Lerp(1.0f - patchStrength, 1.0f, kindMask(kind, x, z));

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
                else if (kind == FoliageKind::Grass)
                    acceptSalt = kSaltGrassAccept;
                accept = hash21(ix, iz, seed ^ acceptSalt) < p;
            }
            if (!accept)
                return;

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
            else if (kind == FoliageKind::Grass)
            {
                scaleMin = 0.80f;
                scaleMax = 1.25f;
            }
            uint32_t scaleSalt = kSaltScale + kindU;
            if (flower)
                scaleSalt += slotU * 16u;
            const float scale = scaleMin + hash21(ix, iz, seed ^ scaleSalt) * (scaleMax - scaleMin);

            if (parentPass)
            {
                if (hash != nullptr && hash->blocked(x, z, scale))
                    return;
                if (hash != nullptr)
                    hash->insert(x, z, scale);
                pushRecord(kind, slot, x, z, ix, iz, scale, parents, 0u);
                parentRefs.push_back(ParentRef{ ix, iz });
                return;
            }

            if (!writing)
            {
                ++counted;
                return;
            }

            bool keep = true;
            if (total > room)
            {
                acc += room;
                if (acc >= total)
                    acc -= total;
                else
                    keep = false;
            }
            if (!keep)
                return;
            pushRecord(kind, slot, x, z, ix, iz, scale, out.records, 0u);
        };

        for (uint32_t cellZ = 0; cellZ < rows; ++cellZ)
        {
            const int   iz      = static_cast<int>(cellZ);
            const float centreZ = in.origin.z + static_cast<float>(cellZ) + 0.5f;
            for (uint32_t cellX = 0; cellX < cols; ++cellX)
            {
                const int   ix      = static_cast<int>(cellX);
                const float centreX = in.origin.x + static_cast<float>(cellX) + 0.5f;
                if (!height->containsXZ(centreX, centreZ))
                    continue;
                if (height->heightAtWorld(centreX, centreZ) < seaLevel)
                    continue;
                if (placeTrees)
                    consider(FoliageKind::Tree, 0, centreX, centreZ, ix, iz, true, treeSpacingM > 0.0f ? &treeHash : nullptr);
                if (placeRocks)
                    consider(FoliageKind::Rock, 0, centreX, centreZ, ix, iz, true, rockSpacingM > 0.0f ? &rockHash : nullptr);
            }
            if (cancelled(in, 0.0f, "cover"))
            {
                DE_LOG_INFO(LogCategory::Render, "FoliageSpawn: cancelled");
                return failSpawn(out);
            }
        }

        const uint64_t parentAccepted = static_cast<uint64_t>(parents.size());
        if (parentAccepted > cap)
            thinPaired(parents, parentRefs, cap);
        out.records.swap(parents);
        for (const FoliageRecord& rec : out.records)
        {
            if (rec.kind == static_cast<uint8_t>(FoliageKind::Tree))
                stampTree(out.cover, rec, in);
        }

        const size_t parentCount = parentRefs.size();

        auto noteChild = [&](FoliageKind kind, int slot, float x, float z, int ix, int iz, float scale, uint32_t extraSalt)
        {
            if (!writing)
            {
                ++counted;
                return;
            }
            bool keep = true;
            if (total > room)
            {
                acc += room;
                if (acc >= total)
                    acc -= total;
                else
                    keep = false;
            }
            if (!keep)
                return;
            pushRecord(kind, slot, x, z, ix, iz, scale, out.records, extraSalt);
        };

        auto hashedCount = [&](int ix, int iz, uint32_t salt, int lo, int hi) -> int
        {
            const int span = hi - lo + 1;
            int       n    = lo;
            if (span > 0)
            {
                const float h = hash21(ix, iz, seed ^ salt);
                n += static_cast<int>(std::floor(h * static_cast<float>(span)));
            }
            if (n > hi)
                n = hi;
            if (n > kMaxChildrenPerParent)
                n = kMaxChildrenPerParent;
            return n;
        };

        // Child XZ is hashed from the parent cell and slot, not from how many records were kept.
        auto emitChildren = [&]()
        {
            if (!(patchStrength > 0.0f) || parentCount == 0)
                return;
            for (size_t i = 0; i < parentCount; ++i)
            {
                const FoliageRecord parent = out.records[i];
                if (parent.kind != static_cast<uint8_t>(FoliageKind::Tree))
                    continue;
                const ParentRef ref  = parentRefs[i];
                const TreeDisk  disk = treeDisk(parent, in.treeExtentX, in.treeExtentZ);
                float           inner = Clamp(disk.canopy * 0.6f, 1.5f, 4.0f);
                float           outer = Clamp(disk.canopy * 1.3f, 1.5f, 4.0f);
                if (outer < inner)
                    outer = inner;
                const int childCount = hashedCount(ref.ix, ref.iz, kSaltChildCount, 1, kMaxChildrenPerParent);
                for (int slot = 0; slot < childCount; ++slot)
                {
                    const uint32_t    slotU  = static_cast<uint32_t>(slot);
                    const float       kindH  = hash21(ref.ix, ref.iz, seed ^ (kSaltChildKind + slotU));
                    const FoliageKind kind   = kindH < 0.45f ? FoliageKind::Flower : FoliageKind::Grass;
                    const float       ang    = hash21(ref.ix, ref.iz, seed ^ (kSaltChild + slotU)) * TwoPi;
                    const float       radH   = hash21(ref.ix, ref.iz, seed ^ (kSaltChildRadius + slotU));
                    const float       radius = inner + radH * (outer - inner);
                    const float       x      = parent.x + std::cos(ang) * radius;
                    const float       z      = parent.z + std::sin(ang) * radius;
                    if (!height->containsXZ(x, z))
                        continue;
                    const float bed = height->heightAtWorld(x, z);
                    if (bed < seaLevel)
                        continue;
                    if (!in.water.empty() && coveredByWater(in.water, x, z, bed))
                        continue;
                    const Vector3f n = height->normalAtWorld(x, z);
                    const float minY = kind == FoliageKind::Flower ? flowerMinNormalY : tuftMinNormalY;
                    if (n.y < minY)
                        continue;
                    if (out.cover.sampleNearest(x, z).coverage == 0)
                        continue;
                    const double dx    = static_cast<double>(x) - static_cast<double>(parent.x);
                    const double dz    = static_cast<double>(z) - static_cast<double>(parent.z);
                    const double trunk = static_cast<double>(disk.trunk);
                    if (dx * dx + dz * dz < trunk * trunk)
                        continue;
                    const float scaleMin = 0.80f;
                    const float scaleMax = kind == FoliageKind::Grass ? 1.25f : 1.20f;
                    const float scale    = scaleMin + hash21(ref.ix, ref.iz, seed ^ (kSaltChildScale + slotU)) * (scaleMax - scaleMin);
                    noteChild(kind, slot, x, z, ref.ix, ref.iz, scale, kSaltChild + slotU);
                }
            }
            for (size_t i = 0; i < parentCount; ++i)
            {
                const FoliageRecord parent = out.records[i];
                if (parent.kind != static_cast<uint8_t>(FoliageKind::Rock) || parent.scale < kPebbleParentScale)
                    continue;
                const ParentRef ref = parentRefs[i];
                const Vector3f slope = height->normalAtWorld(parent.x, parent.z);
                const float    downhillX = -slope.x;
                const float    downhillZ = -slope.z;
                const float    len2 = downhillX * downhillX + downhillZ * downhillZ;
                if (!(len2 > 1.0e-8f))
                    continue;
                const float invLen = 1.0f / std::sqrt(len2);
                const float dirX   = downhillX * invLen;
                const float dirZ   = downhillZ * invLen;
                const float perpX  = -dirZ;
                const float perpZ  = dirX;
                const int   childCount = hashedCount(ref.ix, ref.iz, kSaltPebbleCount, 2, 5);
                for (int slot = 0; slot < childCount; ++slot)
                {
                    const uint32_t slotU   = static_cast<uint32_t>(slot);
                    const float    along   = 0.4f + hash21(ref.ix, ref.iz, seed ^ (kSaltPebble + slotU)) * 0.8f;
                    const float    lateral = (hash21(ref.ix, ref.iz, seed ^ (kSaltPebbleLateral + slotU)) * 2.0f - 1.0f) * 0.12f;
                    float          ox      = dirX * along + perpX * lateral;
                    float          oz      = dirZ * along + perpZ * lateral;
                    const float    olen    = std::sqrt(ox * ox + oz * oz);
                    if (olen > 1.2f)
                    {
                        ox *= 1.2f / olen;
                        oz *= 1.2f / olen;
                    }
                    else if (olen < 0.4f && olen > 0.0f)
                    {
                        ox *= 0.4f / olen;
                        oz *= 0.4f / olen;
                    }
                    const float x = parent.x + ox;
                    const float z = parent.z + oz;
                    if (!height->containsXZ(x, z))
                        continue;
                    const float bed = height->heightAtWorld(x, z);
                    if (bed < seaLevel)
                        continue;
                    if (!in.water.empty() && coveredByWater(in.water, x, z, bed))
                        continue;
                    const Vector3f cn = height->normalAtWorld(x, z);
                    if (cn.y < rockMinNormalY)
                        continue;
                    if (out.cover.sampleNearest(x, z).coverage == 0)
                        continue;
                    float scale = 0.35f + hash21(ref.ix, ref.iz, seed ^ (kSaltPebbleScale + slotU)) * 0.40f;
                    if (!(scale < parent.scale))
                        scale = parent.scale * 0.5f;
                    noteChild(FoliageKind::Rock, slot, x, z, ref.ix, ref.iz, scale, kSaltPebble + slotU);
                }
            }
        };

        auto scanUnder = [&]() -> bool
        {
            for (uint32_t cellZ = 0; cellZ < rows; ++cellZ)
            {
                const int   iz      = static_cast<int>(cellZ);
                const float centreZ = in.origin.z + static_cast<float>(cellZ) + 0.5f;
                for (uint32_t cellX = 0; cellX < cols; ++cellX)
                {
                    const int   ix      = static_cast<int>(cellX);
                    const float centreX = in.origin.x + static_cast<float>(cellX) + 0.5f;
                    if (!height->containsXZ(centreX, centreZ))
                        continue;
                    if (height->heightAtWorld(centreX, centreZ) < seaLevel)
                        continue;
                    if (placeFlowers)
                    {
                        for (int slot = 0; slot < kFlowerSlots; ++slot)
                            consider(FoliageKind::Flower, slot, centreX + kFlowerOffsetX[slot], centreZ, ix, iz, false, nullptr);
                    }
                    if (placeGrass)
                        consider(FoliageKind::Grass, 0, centreX, centreZ, ix, iz, false, nullptr);
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
            emitChildren();
            return true;
        };

        if (!scanUnder())
        {
            DE_LOG_INFO(LogCategory::Render, "FoliageSpawn: cancelled");
            return failSpawn(out);
        }

        total = counted;
        const uint64_t parentKept = static_cast<uint64_t>(out.records.size());
        room = parentKept >= cap ? 0ull : cap - parentKept;
        const uint64_t keepUnder = total < room ? total : room;
        if (keepUnder > 0)
            out.records.reserve(static_cast<size_t>(parentKept + keepUnder));

        writing = true;
        acc     = total / 2ull;
        if (!scanUnder())
        {
            DE_LOG_INFO(LogCategory::Render, "FoliageSpawn: cancelled");
            return failSpawn(out);
        }

        uint32_t nTree   = 0;
        uint32_t nFlower = 0;
        uint32_t nRock   = 0;
        uint32_t nGrass  = 0;
        for (const FoliageRecord& rec : out.records)
        {
            if (rec.kind == static_cast<uint8_t>(FoliageKind::Tree))
                ++nTree;
            else if (rec.kind == static_cast<uint8_t>(FoliageKind::Flower))
                ++nFlower;
            else if (rec.kind == static_cast<uint8_t>(FoliageKind::Grass))
                ++nGrass;
            else
                ++nRock;
        }

        out.accepted = parentAccepted + total;
        out.kept     = static_cast<uint32_t>(out.records.size());
        out.capped   = out.accepted > cap;
        if (out.capped)
            DE_LOG_WARN(LogCategory::Render, "FoliageSpawn: cap thinned accepted {} to {}", out.accepted, cap);
        DE_LOG_INFO(LogCategory::Render, "FoliageSpawn: kept {} accepted {} capped {} seed {} trees {} flowers {} rocks {} grass {}", out.kept, out.accepted, out.capped ? 1 : 0, seed, nTree, nFlower, nRock, nGrass);
        return true;
    }

    bool spawnFoliage(const FoliageSpawnIn& in, FoliageSpawnOut& out)
    {
        const LargeMaskPin none[2]{};
        return spawnFoliageImpl(in, out, none);
    }

    bool spawnFoliageForcedLargeMask(const FoliageSpawnIn& in, FoliageSpawnOut& out,
        float x0, float z0, float radius0, float mask0,
        float x1, float z1, float radius1, float mask1)
    {
        const LargeMaskPin pins[2] = {
            LargeMaskPin{ x0, z0, radius0, mask0 },
            LargeMaskPin{ x1, z1, radius1, mask1 },
        };
        return spawnFoliageImpl(in, out, pins);
    }

    bool restampFoliageCover(const FoliageSpawnIn& in, const std::vector<FoliageRecord>& records, FoliageCover& cover)
    {
        cover.reset();
        if (in.height == nullptr || !in.height->valid() || in.splat == nullptr || !in.splat->valid())
            return false;
        if (in.tilesX == 0 || in.tilesZ == 0 || in.tileCells == 0 || !(in.cellSize > 0.0f))
            return false;

        const uint64_t spanX = static_cast<uint64_t>(in.tilesX) * static_cast<uint64_t>(in.tileCells);
        const uint64_t spanZ = static_cast<uint64_t>(in.tilesZ) * static_cast<uint64_t>(in.tileCells);
        if (static_cast<uint64_t>(in.height->width()) < 1ull || static_cast<uint64_t>(in.height->height()) < 1ull)
            return false;
        if (static_cast<uint64_t>(in.height->width()) - 1ull != spanX || static_cast<uint64_t>(in.height->height()) - 1ull != spanZ)
            return false;

        const double widthM = static_cast<double>(spanX) * static_cast<double>(in.cellSize);
        const double depthM = static_cast<double>(spanZ) * static_cast<double>(in.cellSize);
        if (!(widthM > 0.0) || !(depthM > 0.0) || widthM > static_cast<double>(INT32_MAX) || depthM > static_cast<double>(INT32_MAX))
            return false;
        if (!cover.create(static_cast<float>(widthM), static_cast<float>(depthM), in.origin.x, in.origin.z))
            return false;
        cover.clear(255, 255);

        if (!in.trails.empty())
        {
            if (cancelled(in, 0.0f, "trails"))
            {
                cover.reset();
                return false;
            }
            rasterTrails(cover, in);
        }
        if (cancelled(in, 0.0f, "cover"))
        {
            cover.reset();
            return false;
        }
        for (const FoliageRecord& rec : records)
        {
            if (rec.kind == static_cast<uint8_t>(FoliageKind::Tree))
                stampTree(cover, rec, in);
        }
        return true;
    }

} // namespace Dark::Terrain
