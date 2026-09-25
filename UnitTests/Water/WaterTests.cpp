#include <gtest/gtest.h>

#include "Math/MathHelper.h"
#include "Terrain/HeightMap.h"
#include "Terrain/TerrainLod.h"
#include "Water/Water.h"
#include "Water/WaterBody.h"
#include "Water/WaterWaves.h"

using namespace Dark;
using namespace Dark::Math;
using namespace Dark::Terrain;

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

TEST(WaterWaves, ScaleChangesHeightAndTravelSpeed)
{
    WaterParams p = defaultWaterParams(4.0f);
    p.speedScale  = 0.0f;
    const float y1 = waveHeight(p, 2.0f, 3.0f, 1.5f);
    p.amplitudeScale = 2.5f;
    const float y2 = waveHeight(p, 2.0f, 3.0f, 1.5f);
    EXPECT_NEAR(y2 - p.waterLevel, 2.5f * (y1 - p.waterLevel), 1.0e-4f);

    p.amplitudeScale = 0.0f;
    EXPECT_NEAR(waveHeight(p, 2.0f, 3.0f, 1.5f), p.waterLevel, 1.0e-5f);
    EXPECT_NEAR(maxWaveAmplitude(p), 0.0f, 1.0e-5f);

    p.amplitudeScale = 1.0f;
    p.speedScale     = 1.0f;
    const float traveled = waveHeight(p, 1.25f, -0.5f, 0.37f);
    p.speedScale = 2.0f;
    const float doubled = waveHeight(p, 1.25f, -0.5f, 0.37f * 0.5f);
    EXPECT_NEAR(traveled, doubled, 1.0e-4f);

    p.speedScale = 0.0f;
    const float held = waveHeight(p, 1.25f, -0.5f, 9.0f);
    const float rest = waveHeight(p, 1.25f, -0.5f, 0.0f);
    EXPECT_NEAR(held, rest, 1.0e-4f);
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

TEST(WaterBody, FiniteFootprintAndEdgeFade)
{
    WaterBody body;
    WaterBodyDesc desc;
    desc.center  = Vector3f(10.0f, 3.0f, -4.0f);
    desc.extentX = 48.0f;
    desc.extentZ = 48.0f;
    ASSERT_TRUE(body.build(nullptr, desc, defaultWaterParams(3.0f)));

    Vector3f pos;
    Vector2f uv;
    ASSERT_TRUE(body.vertex(0, pos, uv));
    EXPECT_NEAR(pos.x, 10.0f - 24.0f, 1.0e-3f);
    EXPECT_NEAR(pos.y, 3.0f, 1.0e-3f);
    EXPECT_NEAR(pos.z, -4.0f - 24.0f, 1.0e-3f);
    EXPECT_NEAR(uv.x, 0.0f, 1.0e-3f);

    const int cells = 24;
    const int center = 12 * (cells + 1) + 12;
    ASSERT_TRUE(body.vertex(center, pos, uv));
    EXPECT_NEAR(pos.x, 10.0f, 1.0e-3f);
    EXPECT_NEAR(pos.z, -4.0f, 1.0e-3f);
    EXPECT_NEAR(uv.x, 1.0f, 1.0e-3f);

    EXPECT_TRUE(body.containsXZ(10.0f, -4.0f));
    EXPECT_TRUE(body.containsXZ(34.0f, 20.0f));
    EXPECT_FALSE(body.containsXZ(34.1f, -4.0f));
    EXPECT_NEAR(body.bounds().Min.y, 3.0f - maxWaveAmplitude(body.params()), 1.0e-3f);

    WaterBodyDesc moved = desc;
    moved.center.y = 8.0f;
    EXPECT_FALSE(body.matches(moved));
    EXPECT_TRUE(body.matches(desc));

    WaterBody other;
    WaterBodyDesc pond = desc;
    pond.center = Vector3f(-30.0f, 12.0f, 6.0f);
    pond.extentX = 16.0f;
    pond.extentZ = 20.0f;
    ASSERT_TRUE(other.build(nullptr, pond, defaultWaterParams(12.0f)));
    EXPECT_NEAR(other.params().waterLevel, 12.0f, 1.0e-4f);
    EXPECT_FALSE(other.containsXZ(10.0f, -4.0f));
    EXPECT_TRUE(other.containsXZ(-30.0f, 6.0f));
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

TEST(WaterWorld, Coarse1025_UsesChunkCells64)
{
    HeightMap coarse;
    ASSERT_TRUE(coarse.create(kMaxHeightMapSize, kMaxHeightMapSize, 2.0f, 1.0f));
    coarse.setOrigin(Vector3f{ -1024.0f, 0.0f, -1024.0f });
    coarse.setHeight(1024, 1024, 40.0f);

    WaterDesc desc;
    desc.chunkCells = 16;
    desc.waterLevel = 1.0f;
    desc.params     = defaultWaterParams(1.0f);
    for (int i = 0; i < kWaterWaveCount; ++i)
        desc.params.waves[i].amplitude = 0.0f;

    WaterWorld water;
    ASSERT_TRUE(water.create(coarse, desc));
    EXPECT_EQ(water.chunkCells(), kWaterChunkCellsCoarse);
    EXPECT_EQ(water.chunksX(), 16);
    EXPECT_EQ(water.chunksZ(), 16);
    EXPECT_EQ(water.wetChunkCount(), 16 * 16);

    float y = 0.0f;
    EXPECT_TRUE(water.tryHeightAtWorld(0.0f, 0.0f, y));
    EXPECT_NEAR(y, 1.0f, 1.0e-4f);

    WaterDesc explicit64 = desc;
    explicit64.chunkCells = kWaterChunkCellsCoarse;
    WaterWorld water64;
    ASSERT_TRUE(water64.create(coarse, explicit64));
    EXPECT_EQ(water64.chunkCells(), kWaterChunkCellsCoarse);
    EXPECT_EQ(water64.chunksX(), 16);
}

TEST(WaterWorld, SmallMapKeepsRequestedChunkCells)
{
    HeightMap hm;
    ASSERT_TRUE(hm.create(129, 129, 2.0f, 1.0f));

    WaterDesc desc;
    desc.chunkCells = 16;
    desc.waterLevel = 1.0f;
    WaterWorld water;
    ASSERT_TRUE(water.create(hm, desc));
    EXPECT_EQ(water.chunkCells(), 16);
    EXPECT_EQ(water.chunksX(), 8);
    EXPECT_EQ(water.chunksZ(), 8);
}

TEST(WaterWorld, BoundsTrackWaveHeight)
{
    HeightMap hm;
    ASSERT_TRUE(hm.create(9, 9, 1.0f, 1.0f));
    for (int z = 0; z < 9; ++z)
    {
        for (int x = 0; x < 9; ++x)
            hm.setHeight(x, z, 0.0f);
    }

    WaterDesc desc;
    desc.chunkCells = 4;
    desc.waterLevel = 2.0f;
    desc.params     = defaultWaterParams(2.0f);

    WaterWorld water;
    ASSERT_TRUE(water.create(hm, desc));
    const WaterChunk* chunk = water.chunk(0, 0);
    ASSERT_NE(chunk, nullptr);
    const float amp = maxWaveAmplitude(water.params());
    EXPECT_NEAR(chunk->bounds.Max.y, 2.0f + amp, 1.0e-4f);
    EXPECT_NEAR(chunk->bounds.Min.y, 2.0f - amp, 1.0e-4f);

    water.params().amplitudeScale = 2.0f;
    water.updateLod(Vector3f(0.0f, 0.0f, 0.0f));
    chunk = water.chunk(0, 0);
    ASSERT_NE(chunk, nullptr);
    EXPECT_NEAR(chunk->bounds.Max.y, 2.0f + amp * 2.0f, 1.0e-4f);
    EXPECT_NEAR(chunk->bounds.Min.y, 2.0f - amp * 2.0f, 1.0e-4f);
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
