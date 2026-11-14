#include <gtest/gtest.h>

#include "Math/MathHelper.h"
#include "Terrain/FoliageCover.h"
#include "Terrain/FoliageFile.h"
#include "Terrain/FoliageSpawn.h"
#include "Terrain/HeightMap.h"
#include "Terrain/SplatMap.h"
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
