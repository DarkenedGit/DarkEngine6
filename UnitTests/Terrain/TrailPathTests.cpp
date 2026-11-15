#include <gtest/gtest.h>

#include "Math/MathHelper.h"
#include "Terrain/FoliageCover.h"
#include "Terrain/FoliageFile.h"
#include "Terrain/FoliageSpawn.h"
#include "Terrain/HeightMap.h"
#include "Terrain/SplatMap.h"
#include "Terrain/TerrainGround.h"
#include "Terrain/TrailPath.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace Dark::Terrain;
using namespace Dark::Math;

namespace
{
    TrailPath humanSegment(float x, float z0, float z1, float width)
    {
        TrailPath path;
        path.kind = TrailKind::Human;
        path.width = width;
        path.authored = true;
        path.points.push_back(TrailPoint{ x, z0 });
        path.points.push_back(TrailPoint{ x, z1 });
        return path;
    }

    bool flatMap(HeightMap& hm, SplatMap& splat, uint32_t samples, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        if (!hm.createWorking(samples, samples, 1.0f, 1.0f))
            return false;
        hm.setOrigin(Vector3f{ 0.0f, 0.0f, 0.0f });
        float* heights = hm.mutableSamples();
        const size_t count = static_cast<size_t>(samples) * static_cast<size_t>(samples);
        for (size_t i = 0; i < count; ++i)
            heights[i] = 1.0f;
        if (!splat.createWorking(samples, samples))
            return false;
        for (uint32_t z = 0; z < samples; ++z)
        {
            for (uint32_t x = 0; x < samples; ++x)
                splat.setTexel(static_cast<int>(x), static_cast<int>(z), r, g, b, a);
        }
        return true;
    }

    float distToSegment(float px, float pz, float ax, float az, float bx, float bz)
    {
        const float abx = bx - ax;
        const float abz = bz - az;
        const float apx = px - ax;
        const float apz = pz - az;
        const float ab2 = abx * abx + abz * abz;
        float t = 0.0f;
        if (ab2 > 1.0e-12f)
        {
            t = (apx * abx + apz * abz) / ab2;
            if (t < 0.0f)
                t = 0.0f;
            if (t > 1.0f)
                t = 1.0f;
        }
        const float dx = px - (ax + abx * t);
        const float dz = pz - (az + abz * t);
        return std::sqrt(dx * dx + dz * dz);
    }
}

TEST(TrailPath, TrailRasterLeavesBareCoreAndShoulder)
{
    FoliageCover cover;
    ASSERT_TRUE(cover.create(32.0f, 32.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(cover.cellSize(), 1.0f);
    cover.clear(255, 255);

    const TrailPath pathA = humanSegment(8.0f, 8.0f, 18.0f, 1.6f);
    rasterTrail(cover, pathA, 3u, 1u);

    const CoverSample core = cover.sampleNearest(8.0f, 13.0f);
    EXPECT_EQ(core.coverage, 0);
    EXPECT_EQ(core.heightScale, 0);

    const CoverSample shoulder = cover.sampleNearest(9.4f, 13.0f);
    EXPECT_NE(shoulder.coverage, 0);
    EXPECT_NE(shoulder.heightScale, 0);
    EXPECT_NE(shoulder.heightScale, 255);

    const CoverSample far = cover.sampleNearest(20.0f, 20.0f);
    EXPECT_EQ(far.coverage, 255);
    EXPECT_EQ(far.heightScale, 255);

    TrailPath pathB;
    pathB.kind = TrailKind::Human;
    pathB.width = 1.6f;
    pathB.authored = true;
    pathB.points.push_back(TrailPoint{ 2.0f, 13.0f });
    pathB.points.push_back(TrailPoint{ 20.0f, 13.0f });
    rasterTrail(cover, pathB, 3u, 2u);

    const CoverSample crossing = cover.sampleNearest(8.0f, 13.0f);
    EXPECT_EQ(crossing.coverage, 0);
    EXPECT_EQ(crossing.heightScale, 0);
    const CoverSample stillFar = cover.sampleNearest(20.0f, 20.0f);
    EXPECT_EQ(stillFar.coverage, 255);
    EXPECT_EQ(stillFar.heightScale, 255);
}

TEST(TrailPath, NoisyEdgeIsDeterministic)
{
    auto stamp = [](uint32_t seed, FoliageCover& cover) {
        ASSERT_TRUE(cover.create(32.0f, 32.0f, 0.0f, 0.0f));
        cover.clear(255, 255);
        rasterTrail(cover, humanSegment(8.0f, 4.0f, 28.0f, 1.6f), seed, 1u);
    };

    FoliageCover a;
    FoliageCover b;
    stamp(3u, a);
    stamp(3u, b);
    ASSERT_EQ(a.cellsX(), b.cellsX());
    ASSERT_EQ(a.cellsZ(), b.cellsZ());

    bool same = true;
    for (int z = 0; z < a.cellsZ() && same; ++z)
    {
        for (int x = 0; x < a.cellsX(); ++x)
        {
            const float wx = a.originX() + static_cast<float>(x) + 0.5f;
            const float wz = a.originZ() + static_cast<float>(z) + 0.5f;
            const CoverSample sa = a.sampleNearest(wx, wz);
            const CoverSample sb = b.sampleNearest(wx, wz);
            if (sa.coverage != sb.coverage || sa.heightScale != sb.heightScale)
            {
                same = false;
                break;
            }
        }
    }
    EXPECT_TRUE(same);

    FoliageCover c;
    stamp(9u, c);
    bool differ = false;
    for (int z = 0; z < a.cellsZ() && !differ; ++z)
    {
        for (int x = 0; x < a.cellsX(); ++x)
        {
            const float wx = a.originX() + static_cast<float>(x) + 0.5f;
            const float wz = a.originZ() + static_cast<float>(z) + 0.5f;
            const CoverSample sa = a.sampleNearest(wx, wz);
            const CoverSample sc = c.sampleNearest(wx, wz);
            if (sa.coverage != sc.coverage || sa.heightScale != sc.heightScale)
            {
                differ = true;
                break;
            }
        }
    }
    EXPECT_TRUE(differ);
}

TEST(TrailPath, TreesAndRocksSkipPathCore)
{
    HeightMap hm;
    SplatMap splat;
    ASSERT_TRUE(flatMap(hm, splat, 17u, 0, 255, 255, 0));

    FoliageSpawnIn in;
    in.height = &hm;
    in.splat = &splat;
    in.tilesX = 1;
    in.tilesZ = 1;
    in.tileCells = 16;
    in.cellSize = hm.cellSize();
    in.origin = hm.origin();
    in.seaLevel = -1000.0f;
    in.density.grassTreesPerM2 = 1.0f;
    in.density.rockPerM2 = 1.0f;
    in.density.dirtTreesPerM2 = 0.0f;
    in.density.dirtFlowersPerM2 = 0.0f;
    in.density.grassFlowersPerM2 = 0.0f;
    in.density.grassPerM2 = 0.0f;
    in.density.treeSpacingM = 0.0f;
    in.density.rockSpacingM = 0.0f;
    in.density.seed = 1337u;

    const float width = 1.6f;
    TrailPath path = humanSegment(8.0f, 2.0f, 14.0f, width);
    in.trails.push_back(path);

    FoliageSpawnOut out;
    ASSERT_TRUE(spawnFoliage(in, out));
    ASSERT_TRUE(out.cover.valid());

    const CoverSample core = out.cover.sampleNearest(8.0f, 8.0f);
    EXPECT_EQ(core.coverage, 0);
    EXPECT_EQ(core.heightScale, 0);

    FoliageCover pathOnly;
    ASSERT_TRUE(pathOnly.create(out.cover.extentX(), out.cover.extentZ(), out.cover.originX(), out.cover.originZ()));
    pathOnly.clear(255, 255);
    rasterTrail(pathOnly, path, in.density.seed, 1u);

    int trees = 0;
    int rocks = 0;
    for (const FoliageRecord& rec : out.records)
    {
        const bool tree = rec.kind == static_cast<uint8_t>(FoliageKind::Tree);
        const bool rock = rec.kind == static_cast<uint8_t>(FoliageKind::Rock);
        if (!tree && !rock)
            continue;
        if (tree)
            ++trees;
        else
            ++rocks;
        const float dist = distToSegment(rec.x, rec.z, 8.0f, 2.0f, 8.0f, 14.0f);
        EXPECT_GE(dist + 1.0e-3f, width * 0.5f);
        EXPECT_NE(pathOnly.sampleNearest(rec.x, rec.z).coverage, 0);
    }
    EXPECT_GT(trees, 0);
    EXPECT_GT(rocks, 0);
}

TEST(TrailPath, LeastCostAvoidsWaterAndSteep)
{
    constexpr uint32_t kSamples = 97u;
    HeightMap hm;
    SplatMap splat;
    ASSERT_TRUE(hm.createWorking(kSamples, kSamples, 1.0f, 1.0f));
    hm.setOrigin(Vector3f{ 0.0f, 0.0f, 0.0f });
    float* heights = hm.mutableSamples();
    for (uint32_t z = 0; z < kSamples; ++z)
    {
        for (uint32_t x = 0; x < kSamples; ++x)
        {
            float y = 5.0f;
            if (x >= 32u && x <= 64u && z >= 56u)
                y = 5.0f + static_cast<float>(x - 32u);
            heights[static_cast<size_t>(z) * kSamples + x] = y;
        }
    }
    ASSERT_TRUE(splat.createWorking(kSamples, kSamples));
    for (uint32_t z = 0; z < kSamples; ++z)
    {
        for (uint32_t x = 0; x < kSamples; ++x)
            splat.setTexel(static_cast<int>(x), static_cast<int>(z), 0, 255, 0, 0);
    }

    const Vector3f ridgeNormal = hm.normalAtWorld(48.0f, 80.0f);
    EXPECT_LT(ridgeNormal.y, 0.80f);

    FoliageWaterVolume lake;
    lake.minX = 32.0f;
    lake.maxX = 64.0f;
    lake.minZ = 0.0f;
    lake.maxZ = 40.0f;
    lake.surfaceY = 8.0f;
    std::vector<FoliageWaterVolume> water;
    water.push_back(lake);

    TrailRouteIn route;
    route.height = &hm;
    route.splat = &splat;
    route.water = &water;
    route.seaLevel = -1000.0f;
    route.kind = TrailKind::Human;
    route.seed = 11u;
    route.startX = 14.0f;
    route.startZ = 22.0f;
    route.endX = 82.0f;
    route.endZ = 22.0f;

    std::vector<TrailPoint> points;
    ASSERT_TRUE(findTrailRoute(route, points));
    ASSERT_GE(points.size(), 2u);

    bool crossedGap = false;
    bool climbed = false;
    for (size_t i = 1; i < points.size(); ++i)
    {
        const float dx = points[i].x - points[i - 1].x;
        const float dz = points[i].z - points[i - 1].z;
        const float mag = std::sqrt(dx * dx + dz * dz);
        int steps = static_cast<int>(std::ceil(mag / 0.5f));
        if (steps < 1)
            steps = 1;
        for (int s = 0; s <= steps; ++s)
        {
            const float t = static_cast<float>(s) / static_cast<float>(steps);
            const float x = points[i - 1].x + dx * t;
            const float z = points[i - 1].z + dz * t;
            const bool inLake = x >= lake.minX && x <= lake.maxX && z >= lake.minZ && z <= lake.maxZ;
            EXPECT_FALSE(inLake);
            const Vector3f normal = hm.normalAtWorld(x, z);
            EXPECT_GT(normal.y, 0.88f);
            if (z > 40.0f)
                climbed = true;
            if (x > 36.0f && x < 60.0f && z > 40.0f && z < 56.0f)
                crossedGap = true;
        }
    }
    EXPECT_TRUE(climbed);
    EXPECT_TRUE(crossedGap);

    HeightMap sea;
    SplatMap seaSplat;
    ASSERT_TRUE(flatMap(sea, seaSplat, 17u, 0, 255, 0, 0));
    uint8_t before[4] = {};
    seaSplat.getTexel(4, 4, before);

    TrailRouteIn flooded;
    flooded.height = &sea;
    flooded.splat = &seaSplat;
    flooded.seaLevel = 100.0f;
    flooded.kind = TrailKind::Human;
    flooded.startX = 2.0f;
    flooded.startZ = 2.0f;
    flooded.endX = 12.0f;
    flooded.endZ = 10.0f;
    std::vector<TrailPoint> none;
    ASSERT_TRUE(findTrailRoute(flooded, none));
    EXPECT_TRUE(none.empty());

    FoliageWaterVolume ocean;
    ocean.entireMap = true;
    ocean.surfaceY = 10.0f;
    std::vector<FoliageWaterVolume> oceans;
    oceans.push_back(ocean);
    flooded.water = &oceans;
    flooded.seaLevel = -100.0f;
    ASSERT_TRUE(findTrailRoute(flooded, none));
    EXPECT_TRUE(none.empty());

    TrailSet set;
    set.autoGenerate = true;
    TrailGenerateIn gen;
    gen.height = &sea;
    gen.splat = &seaSplat;
    gen.water = &oceans;
    gen.seaLevel = -100.0f;
    gen.seed = 3u;
    gen.maxAutoPaths = 12;
    gen.humanWidth = 1.6f;
    gen.animalWidth = 0.8f;
    ASSERT_TRUE(generateGameTrails(gen, set, nullptr));
    EXPECT_TRUE(set.paths.empty());

    FoliageCover cover;
    ASSERT_TRUE(cover.create(16.0f, 16.0f, 0.0f, 0.0f));
    cover.clear(255, 255);
    for (const TrailPath& path : set.paths)
        rasterTrail(cover, path, gen.seed, 1u);
    const CoverSample bare = cover.sampleNearest(4.0f, 4.0f);
    EXPECT_EQ(bare.coverage, 255);
    EXPECT_EQ(bare.heightScale, 255);

    ASSERT_TRUE(stampTrailDirt(sea, seaSplat, set.paths));
    uint8_t after[4] = {};
    seaSplat.getTexel(4, 4, after);
    EXPECT_EQ(after[0], before[0]);
    EXPECT_EQ(after[1], before[1]);
    EXPECT_EQ(after[2], before[2]);
    EXPECT_EQ(after[3], before[3]);
}

TEST(TrailPath, AuthoredPathsSurviveRegenerate)
{
    HeightMap hm;
    SplatMap splat;
    ASSERT_TRUE(flatMap(hm, splat, 17u, 0, 255, 0, 0));

    TrailSet set;
    set.autoGenerate = true;
    set.maxAutoPaths = 12;
    set.humanWidth = 1.6f;
    set.animalWidth = 0.8f;

    TrailPath authored;
    authored.kind = TrailKind::Human;
    authored.width = 1.6f;
    authored.authored = true;
    authored.points.push_back(TrailPoint{ 1.0f, 2.0f });
    authored.points.push_back(TrailPoint{ 8.0f, 2.0f });
    authored.points.push_back(TrailPoint{ 8.0f, 9.0f });

    TrailPath oldGenerated;
    oldGenerated.kind = TrailKind::Animal;
    oldGenerated.width = 0.8f;
    oldGenerated.authored = false;
    oldGenerated.points.push_back(TrailPoint{ 4.0f, 4.0f });
    oldGenerated.points.push_back(TrailPoint{ 6.0f, 6.0f });
    set.paths.push_back(authored);
    set.paths.push_back(oldGenerated);

    TrailGenerateIn in;
    in.height = &hm;
    in.splat = &splat;
    in.seaLevel = -10.0f;
    in.seed = 7u;
    in.maxAutoPaths = set.maxAutoPaths;
    in.humanWidth = set.humanWidth;
    in.animalWidth = set.animalWidth;
    ASSERT_TRUE(generateGameTrails(in, set, nullptr));

    const TrailPath* kept = nullptr;
    for (const TrailPath& path : set.paths)
    {
        EXPECT_TRUE(path.authored);
        EXPECT_EQ(kept, nullptr);
        kept = &path;
    }
    ASSERT_NE(kept, nullptr);
    ASSERT_EQ(kept->points.size(), 3u);
    EXPECT_NEAR(kept->points[0].x, 1.0f, 1.0e-5f);
    EXPECT_NEAR(kept->points[0].z, 2.0f, 1.0e-5f);
    EXPECT_NEAR(kept->points[1].x, 8.0f, 1.0e-5f);
    EXPECT_NEAR(kept->points[1].z, 2.0f, 1.0e-5f);
    EXPECT_NEAR(kept->points[2].x, 8.0f, 1.0e-5f);
    EXPECT_NEAR(kept->points[2].z, 9.0f, 1.0e-5f);
    EXPECT_TRUE(set.autoGenerate);

    const nlohmann::json saved = writeTrailSet(set);
    TrailSet loaded;
    std::string error;
    ASSERT_TRUE(readTrailSet(saved, loaded, &error));
    EXPECT_TRUE(loaded.autoGenerate);
    const TrailPath* round = nullptr;
    for (const TrailPath& path : loaded.paths)
    {
        if (!path.authored)
            continue;
        round = &path;
    }
    ASSERT_NE(round, nullptr);
    ASSERT_EQ(round->points.size(), 3u);
    EXPECT_NEAR(round->points[0].x, 1.0f, 1.0e-5f);
    EXPECT_NEAR(round->points[0].z, 2.0f, 1.0e-5f);
    EXPECT_NEAR(round->points[2].x, 8.0f, 1.0e-5f);
    EXPECT_NEAR(round->points[2].z, 9.0f, 1.0e-5f);
}

TEST(TrailPath, PoiOrderIsDeterministic)
{
    constexpr uint32_t kSamples = 65u;
    auto build = [](HeightMap& hm, SplatMap& splat) {
        EXPECT_TRUE(hm.createWorking(kSamples, kSamples, 1.0f, 1.0f));
        hm.setOrigin(Vector3f{ 0.0f, 0.0f, 0.0f });
        float* heights = hm.mutableSamples();
        for (uint32_t z = 0; z < kSamples; ++z)
        {
            for (uint32_t x = 0; x < kSamples; ++x)
            {
                const bool pond = x >= 8u && x <= 24u && z >= 8u && z <= 24u;
                heights[static_cast<size_t>(z) * kSamples + x] = pond ? 4.0f : 5.0f;
            }
        }
        EXPECT_TRUE(splat.createWorking(kSamples, kSamples));
        for (uint32_t z = 0; z < kSamples; ++z)
        {
            for (uint32_t x = 0; x < kSamples; ++x)
                splat.setTexel(static_cast<int>(x), static_cast<int>(z), 255, 0, 0, 0);
        }
        const int clearings[][2] = { { 12, 2 }, { 13, 4 }, { 12, 6 }, { 14, 8 }, { 12, 10 } };
        for (const int (&cell)[2] : clearings)
            splat.setTexel(cell[0] * 4 + 2, cell[1] * 4 + 2, 0, 255, 0, 0);
        const int forest[][2] = { { 4, 14 }, { 8, 14 }, { 2, 13 }, { 6, 15 } };
        for (const int (&cell)[2] : forest)
        {
            // sampleWeights divides by the channel sum, so grass has to share the texel to stay under 0.60.
            splat.setTexel(cell[0] * 4 + 2, cell[1] * 4 + 2, 155, 100, 0, 0);
        }
    };

    HeightMap hmA;
    SplatMap splatA;
    HeightMap hmB;
    SplatMap splatB;
    build(hmA, splatA);
    build(hmB, splatB);

    FoliageWaterVolume pond;
    pond.minX = 8.0f;
    pond.maxX = 24.0f;
    pond.minZ = 8.0f;
    pond.maxZ = 24.0f;
    pond.surfaceY = 5.0f;
    std::vector<FoliageWaterVolume> water;
    water.push_back(pond);

    auto run = [&](HeightMap& hm, SplatMap& splat, uint32_t seed, TrailGenerateOut& out) {
        TrailSet set;
        set.autoGenerate = true;
        set.maxAutoPaths = 12;
        set.humanWidth = kTrailHumanWidth;
        set.animalWidth = kTrailAnimalWidth;
        TrailGenerateIn in;
        in.height = &hm;
        in.splat = &splat;
        in.water = &water;
        in.seaLevel = -10.0f;
        in.seed = seed;
        in.maxAutoPaths = 12;
        in.humanWidth = kTrailHumanWidth;
        in.animalWidth = kTrailAnimalWidth;
        ASSERT_TRUE(generateGameTrails(in, set, &out));
    };

    TrailGenerateOut first;
    TrailGenerateOut second;
    run(hmA, splatA, 5u, first);
    run(hmB, splatB, 5u, second);

    ASSERT_FALSE(first.water.empty());
    ASSERT_FALSE(first.clearings.empty());
    ASSERT_FALSE(first.forest.empty());
    ASSERT_FALSE(first.generated.empty());
    ASSERT_EQ(first.water.size(), second.water.size());
    ASSERT_EQ(first.clearings.size(), second.clearings.size());
    ASSERT_EQ(first.forest.size(), second.forest.size());
    ASSERT_EQ(first.generated.size(), second.generated.size());
    for (size_t i = 0; i < first.water.size(); ++i)
    {
        EXPECT_EQ(first.water[i].cx, second.water[i].cx);
        EXPECT_EQ(first.water[i].cz, second.water[i].cz);
    }
    for (size_t i = 0; i < first.clearings.size(); ++i)
    {
        EXPECT_EQ(first.clearings[i].cx, second.clearings[i].cx);
        EXPECT_EQ(first.clearings[i].cz, second.clearings[i].cz);
    }
    for (size_t i = 0; i < first.forest.size(); ++i)
    {
        EXPECT_EQ(first.forest[i].cx, second.forest[i].cx);
        EXPECT_EQ(first.forest[i].cz, second.forest[i].cz);
    }

    bool seenAnimal = false;
    bool anyAnimal = false;
    for (size_t i = 0; i < first.generated.size(); ++i)
    {
        const TrailPath& a = first.generated[i];
        const TrailPath& b = second.generated[i];
        EXPECT_EQ(a.kind, b.kind);
        EXPECT_FALSE(a.authored);
        EXPECT_FALSE(b.authored);
        if (a.kind == TrailKind::Animal)
        {
            seenAnimal = true;
            anyAnimal = true;
            EXPECT_NEAR(a.width, 0.8f, 1.0e-5f);
        }
        else
        {
            EXPECT_FALSE(seenAnimal);
            EXPECT_NEAR(a.width, 1.6f, 1.0e-5f);
        }
        ASSERT_EQ(a.points.size(), b.points.size());
        for (size_t p = 0; p < a.points.size(); ++p)
        {
            EXPECT_FLOAT_EQ(a.points[p].x, b.points[p].x);
            EXPECT_FLOAT_EQ(a.points[p].z, b.points[p].z);
        }
    }
    EXPECT_TRUE(anyAnimal);

    HeightMap hmC;
    SplatMap splatC;
    build(hmC, splatC);
    TrailGenerateOut otherSeed;
    run(hmC, splatC, 99u, otherSeed);
    ASSERT_EQ(otherSeed.water.size(), first.water.size());
    for (size_t i = 0; i < first.water.size(); ++i)
    {
        EXPECT_EQ(first.water[i].cx, otherSeed.water[i].cx);
        EXPECT_EQ(first.water[i].cz, otherSeed.water[i].cz);
    }
    bool animalMoved = false;
    if (otherSeed.generated.size() == first.generated.size())
    {
        for (size_t i = 0; i < first.generated.size(); ++i)
        {
            if (first.generated[i].kind != TrailKind::Animal)
                continue;
            const TrailPath& a = first.generated[i];
            const TrailPath& b = otherSeed.generated[i];
            if (a.points.size() != b.points.size())
            {
                animalMoved = true;
                continue;
            }
            for (size_t p = 0; p < a.points.size(); ++p)
            {
                if (a.points[p].x != b.points[p].x || a.points[p].z != b.points[p].z)
                    animalMoved = true;
            }
        }
    }
    else
        animalMoved = true;
    EXPECT_TRUE(animalMoved);
}

TEST(TrailPath, DirtStampUniqueMaximum)
{
    HeightMap hm;
    SplatMap splat;
    ASSERT_TRUE(hm.create(8u, 8u, 1.0f, 1.0f));
    hm.setOrigin(Vector3f{ 0.0f, 0.0f, 0.0f });
    ASSERT_TRUE(splat.create(8u, 8u));
    for (int z = 0; z < 8; ++z)
    {
        for (int x = 0; x < 8; ++x)
            splat.setTexel(x, z, 0, 255, 0, 0);
    }

    splat.paintDisk(3.0f, 3.0f, 2.0f, 0, 1.0f);
    uint8_t texel[4] = {};
    splat.getTexel(3, 3, texel);
    EXPECT_EQ(texel[0], 127);
    EXPECT_EQ(texel[1], 128);
    EXPECT_EQ(texel[2], 0);
    EXPECT_EQ(texel[3], 0);

    TerrainGround ground;
    const GroundContact first = ground.at(&hm, &splat, 3.0f, 3.0f);
    EXPECT_EQ(first.layer, 1);
    ASSERT_NE(first.footstep, nullptr);
    EXPECT_STREQ(first.footstep, "audio/foot_grass.wav");

    splat.paintDisk(3.0f, 3.0f, 2.0f, 0, 1.0f);
    splat.getTexel(3, 3, texel);
    EXPECT_EQ(texel[0], 170);
    EXPECT_EQ(texel[1], 85);
    EXPECT_EQ(texel[2], 0);
    EXPECT_EQ(texel[3], 0);

    const GroundContact second = ground.at(&hm, &splat, 3.0f, 3.0f);
    EXPECT_EQ(second.layer, 0);
    ASSERT_NE(second.footstep, nullptr);
    EXPECT_STREQ(second.footstep, "audio/foot_dirt.wav");
}

TEST(TrailPath, AnimalOffsetDoesNotEnterBlockedCell)
{
    // Centre line from cell (0,0) to (13,5) misses cell (11,5) by about 0.3 m.
    // The animal shift is up to 0.4 m and must not be stored once it enters that cell.
    constexpr uint32_t kSamples = 57u;
    HeightMap hm;
    SplatMap splat;
    ASSERT_TRUE(hm.createWorking(kSamples, kSamples, 1.0f, 1.0f));
    hm.setOrigin(Vector3f{ 0.0f, 0.0f, 0.0f });
    float* heights = hm.mutableSamples();
    const size_t count = static_cast<size_t>(kSamples) * static_cast<size_t>(kSamples);
    for (size_t i = 0; i < count; ++i)
        heights[i] = 5.0f;
    ASSERT_TRUE(splat.createWorking(kSamples, kSamples));
    for (uint32_t z = 0; z < kSamples; ++z)
    {
        for (uint32_t x = 0; x < kSamples; ++x)
            splat.setTexel(static_cast<int>(x), static_cast<int>(z), 0, 255, 0, 0);
    }

    FoliageWaterVolume blocked;
    blocked.minX = 45.5f;
    blocked.maxX = 46.5f;
    blocked.minZ = 21.5f;
    blocked.maxZ = 22.5f;
    blocked.surfaceY = 8.0f;
    std::vector<FoliageWaterVolume> water;
    water.push_back(blocked);

    TrailRouteIn route;
    route.height = &hm;
    route.splat = &splat;
    route.water = &water;
    route.seaLevel = -1000.0f;
    route.kind = TrailKind::Animal;
    route.seed = 4u;
    route.startX = 2.0f;
    route.startZ = 2.0f;
    route.endX = 54.0f;
    route.endZ = 22.0f;

    std::vector<TrailPoint> points;
    ASSERT_TRUE(findTrailRoute(route, points));
    ASSERT_GE(points.size(), 2u);
    for (size_t i = 1; i < points.size(); ++i)
    {
        const float dx = points[i].x - points[i - 1u].x;
        const float dz = points[i].z - points[i - 1u].z;
        const float mag = std::sqrt(dx * dx + dz * dz);
        int steps = static_cast<int>(std::ceil(mag / 0.05f));
        if (steps < 1)
            steps = 1;
        for (int s = 0; s <= steps; ++s)
        {
            const float t = static_cast<float>(s) / static_cast<float>(steps);
            const float x = points[i - 1u].x + dx * t;
            const float z = points[i - 1u].z + dz * t;
            const bool inBlocked = x >= 44.0f && x < 48.0f && z >= 20.0f && z < 24.0f;
            EXPECT_FALSE(inBlocked);
        }
    }
}
