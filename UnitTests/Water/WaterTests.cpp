#include <gtest/gtest.h>

#include "Math/MathHelper.h"
#include "Terrain/HeightMap.h"
#include "Terrain/TerrainLod.h"
#include "Water/Water.h"
#include "Water/WaterWaves.h"

using namespace Dark;
using namespace Dark::Math;
using namespace Dark::Terrain;
using namespace Dark::Water;

TEST(WaterWaves, DefaultHasFourFrequencies)
{
    const WaterParams p = defaultWaterParams(2.0f);
    EXPECT_FLOAT_EQ(p.waterLevel, 2.0f);
    EXPECT_GT(p.waves[0].amplitude, p.waves[1].amplitude);
    EXPECT_GT(p.waves[1].amplitude, p.waves[2].amplitude);
    EXPECT_LT(p.waves[0].frequency, p.waves[3].frequency);
    EXPECT_GT(maxWaveAmplitude(p), 0.0f);
}

TEST(WaterWaves, HeightIsWaterLevelPlusDisplacement)
{
    WaterParams p = defaultWaterParams(5.0f);
    for (int i = 0; i < kWaterWaveCount; ++i)
        p.waves[i].amplitude = 0.0f;

    EXPECT_NEAR(waveHeight(p, 3.0f, 4.0f, 1.0f), 5.0f, 1.0e-5f);

    p.waves[0].amplitude = 0.5f;
    p.waves[0].frequency = 1.0f;
    p.waves[0].speed     = 0.0f;
    p.waves[0].angleFromFlow = 0.0f;
    p.flowDir = Vector2f(1.0f, 0.0f);
    p.flowStrength = 1.0f;

    const float y = waveHeight(p, 0.0f, 0.0f, 0.0f);
    EXPECT_NEAR(y, 5.0f, 1.0e-4f); // sin(0) == 0
}

TEST(WaterWaves, FlowRotatesDirection)
{
    WaterParams p = defaultWaterParams(0.0f);
    p.flowStrength = 0.0f;
    p.waves[0].angleFromFlow = 0.0f;
    p.flowDir = Vector2f(1.0f, 0.0f);
    const Vector2f alongX = waveDirection(p, 0);

    p.flowDir = Vector2f(0.0f, 1.0f);
    const Vector2f alongZ = waveDirection(p, 0);

    EXPECT_NEAR(alongX.x, 1.0f, 1.0e-4f);
    EXPECT_NEAR(alongZ.y, 1.0f, 1.0e-4f);
}

TEST(WaterWorld, OnlyValleysAreWet)
{
    HeightMap hm;
    ASSERT_TRUE(hm.create(17, 17, 1.0f, 1.0f));
    for (int z = 0; z < 17; ++z)
    {
        for (int x = 0; x < 17; ++x)
        {
            // SW quadrant is a pit, NE is a plateau.
            const float h = (x < 8 && z < 8) ? 0.0f : 10.0f;
            hm.setHeight(x, z, h);
        }
    }

    WaterDesc desc;
    desc.chunkCells = 8;
    desc.waterLevel = 3.0f;
    desc.params     = defaultWaterParams(3.0f);

    WaterWorld water;
    ASSERT_TRUE(water.create(hm, desc));
    EXPECT_EQ(water.chunksX(), 2);
    EXPECT_EQ(water.chunksZ(), 2);

    ASSERT_NE(water.chunk(0, 0), nullptr);
    EXPECT_TRUE(water.chunk(0, 0)->wet);
    EXPECT_FALSE(water.chunk(1, 1)->wet);
    EXPECT_GE(water.wetChunkCount(), 1);
    EXPECT_LT(water.wetChunkCount(), 4);
}

TEST(WaterWorld, WeldedLodSharesEdgeXZ)
{
    HeightMap hm;
    ASSERT_TRUE(hm.create(17, 17, 2.0f, 1.0f));
    for (int z = 0; z < 17; ++z)
    {
        for (int x = 0; x < 17; ++x)
            hm.setHeight(x, z, 0.0f);
    }

    WaterDesc desc;
    desc.chunkCells       = 8;
    desc.waterLevel       = 2.0f;
    desc.params           = defaultWaterParams(2.0f);
    desc.lodDistanceCount = 4;
    desc.lodDistances[0]  = 4.0f;
    desc.lodDistances[1]  = 8.0f;
    desc.lodDistances[2]  = 16.0f;
    desc.lodDistances[3]  = 32.0f;

    WaterWorld water;
    ASSERT_TRUE(water.create(hm, desc));
    water.updateLod(Vector3f{ 4.0f, 2.0f, 4.0f });
    water.rebuildDirtyCpuMeshes();

    const WaterChunk* a = water.chunk(0, 0);
    const WaterChunk* b = water.chunk(1, 0);
    ASSERT_TRUE(a && b && a->wet && b->wet);

    auto collectEast = [](const MeshData& mesh, int cells)
    {
        std::vector<Vector3f> pts;
        const int verts = cells + 1;
        for (int z = 0; z < verts; ++z)
            pts.push_back(mesh.positions[static_cast<size_t>(z * verts + cells)]);
        return pts;
    };
    auto collectWest = [](const MeshData& mesh, int cells)
    {
        std::vector<Vector3f> pts;
        const int verts = cells + 1;
        for (int z = 0; z < verts; ++z)
            pts.push_back(mesh.positions[static_cast<size_t>(z * verts + 0)]);
        return pts;
    };

    const int aCells = 8 / lodStep(a->lod);
    const int bCells = 8 / lodStep(b->lod);
    const auto east = collectEast(a->cpu, aCells);
    const auto west = collectWest(b->cpu, bCells);

    const auto& fine = (a->lod <= b->lod) ? east : west;
    const auto& coarse = (a->lod <= b->lod) ? west : east;
    ASSERT_FALSE(coarse.empty());
    for (const Vector3f& p : coarse)
    {
        bool found = false;
        for (const Vector3f& q : fine)
        {
            if (NearEqual(p.x, q.x, 1.0e-4f) && NearEqual(p.z, q.z, 1.0e-4f))
            {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found);
    }
}

TEST(WaterWorld, HeightQueryOnlyInValleys)
{
    HeightMap hm;
    ASSERT_TRUE(hm.create(9, 9, 1.0f, 1.0f));
    for (int z = 0; z < 9; ++z)
    {
        for (int x = 0; x < 9; ++x)
            hm.setHeight(x, z, (x < 4) ? 0.0f : 8.0f);
    }

    WaterDesc desc;
    desc.chunkCells = 4;
    desc.waterLevel = 2.0f;
    desc.params     = defaultWaterParams(2.0f);
    for (int i = 0; i < kWaterWaveCount; ++i)
        desc.params.waves[i].amplitude = 0.0f;

    WaterWorld water;
    ASSERT_TRUE(water.create(hm, desc));

    float y = 0.0f;
    EXPECT_TRUE(water.tryHeightAtWorld(1.0f, 1.0f, y));
    EXPECT_NEAR(y, 2.0f, 1.0e-4f);
    EXPECT_FALSE(water.tryHeightAtWorld(7.0f, 1.0f, y));
}
