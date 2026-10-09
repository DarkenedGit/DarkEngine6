#include <gtest/gtest.h>

#include <cmath>

#include "Math/MathHelper.h"
#include "Terrain/HeightMap.h"
#include "Terrain/TerrainLod.h"
#include "Scene/SceneFile.h"
#include "Water/Water.h"
#include "Water/WaterBody.h"
#include "Water/WaterStream.h"
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
    EXPECT_NEAR(maxWaveAmplitude(p), kShoreChopMax, 1.0e-5f);

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

    float minX = 0.0f;
    float maxX = 0.0f;
    float minZ = 0.0f;
    float maxZ = 0.0f;
    ASSERT_TRUE(placedWaterFootprint(10.0f, -4.0f, 48.0f, 48.0f, minX, maxX, minZ, maxZ));
    EXPECT_TRUE(body.containsXZ(minX, minZ));
    EXPECT_TRUE(body.containsXZ(maxX, maxZ));
    EXPECT_FALSE(body.containsXZ(minX - 0.05f, -4.0f));
    EXPECT_NEAR(maxX, 34.0f, 1.0e-4f);

    ASSERT_TRUE(placedWaterFootprint(10.0f, -4.0f, 1.0f, 48.0f, minX, maxX, minZ, maxZ));
    EXPECT_NEAR(maxX - minX, 4.0f, 1.0e-4f);
    EXPECT_FALSE(placedWaterFootprint(0.0f, 0.0f, 1025.0f, 48.0f, minX, maxX, minZ, maxZ));
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
    const float swell = displacementAmplitude(water.params());
    const float amp = maxWaveAmplitude(water.params());
    EXPECT_NEAR(amp, swell + kShoreChopMax, 1.0e-4f);
    EXPECT_NEAR(chunk->bounds.Max.y, 2.0f + amp, 1.0e-4f);
    EXPECT_NEAR(chunk->bounds.Min.y, 2.0f - amp, 1.0e-4f);

    water.params().amplitudeScale = 2.0f;
    water.updateLod(Vector3f(0.0f, 0.0f, 0.0f));
    chunk = water.chunk(0, 0);
    ASSERT_NE(chunk, nullptr);
    const float amp2 = maxWaveAmplitude(water.params());
    EXPECT_NEAR(amp2, swell * 2.0f + kShoreChopMax, 1.0e-4f);
    EXPECT_NEAR(chunk->bounds.Max.y, 2.0f + amp2, 1.0e-4f);
    EXPECT_NEAR(chunk->bounds.Min.y, 2.0f - amp2, 1.0e-4f);
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

TEST(WaterWaves, HashPinsMatchTheShader)
{
    EXPECT_NEAR(hash01(0, 0), 0.108964f, 1.0e-5f);
    EXPECT_NEAR(hash01(-1, 2), 0.682527f, 1.0e-5f);
}

TEST(WaterWaves, ShoreBandFollowsSlopeAndAmplitude)
{
    const ShoreSample gentle = evaluateShore(0.0f, 0.0f, 0.15f, 1.0f, 0.72f);
    EXPECT_NEAR(gentle.weight, 1.0f, 1.0e-4f);
    EXPECT_NEAR(gentle.bandMeters, 9.8f, 0.3f);

    const ShoreSample steep = evaluateShore(0.0f, 0.0f, 0.70f, 1.0f, 0.72f);
    EXPECT_NEAR(steep.bandMeters, 4.4f, 0.3f);

    const ShoreSample open = evaluateShore(10.0f, 0.0f, 0.15f, 1.0f, 0.72f);
    EXPECT_NEAR(open.weight, 0.0f, 1.0e-4f);
}

TEST(WaterWaves, ShoreChopStaysInsideThePad)
{
    EXPECT_NEAR(evaluateShoreChop(1.0f, 2.0f, 0.3f, 0.0f, 1.0f, 0.0f), 0.0f, 1.0e-6f);
    const float y = evaluateShoreChop(1.0f, 2.0f, 0.3f, 1.0f, 1.0f, 0.0f);
    EXPECT_LE(fabsf(y), kShoreChopMax + 1.0e-4f);
}

TEST(WaterWaves, LegacyDetectorKeepsPartialEdits)
{
    WaterParams legacy = defaultWaterParams(4.0f);
    copyLegacyHarmonicWaves(legacy.waves);
    EXPECT_TRUE(isLegacyHarmonicPreset(legacy));

    WaterParams speedEdit = legacy;
    speedEdit.waves[0].speed = 2.0f;
    EXPECT_FALSE(isLegacyHarmonicPreset(speedEdit));

    WaterParams ampEdit = legacy;
    ampEdit.waves[0].amplitude = 0.5f;
    EXPECT_FALSE(isLegacyHarmonicPreset(ampEdit));

    WaterSceneDesc one{};
    one.waveCount = 1;
    one.waves[0].angleFromFlow = 0.10f;
    one.waves[0].frequency = 0.224399f;
    one.waves[0].amplitude = 0.42f;
    one.waves[0].speed = 1.15f;
    one.flowDir[0] = 1.0f;
    one.flowDir[1] = 0.35f;
    const WaterParams partial = sceneWaterParams(one, 10.0f);
    EXPECT_NEAR(partial.waves[0].amplitude, 0.42f, 1.0e-4f);
    EXPECT_NEAR(partial.waterLevel, 10.0f, 1.0e-4f);

    WaterSceneDesc full{};
    full.waveCount = 4;
    full.flowStrength = 0.85f;
    full.steepness = 0.55f;
    full.amplitudeScale = 1.5f;
    for (int i = 0; i < kWaterWaveCount; ++i)
    {
        full.waves[i].angleFromFlow = legacy.waves[i].angleFromFlow;
        full.waves[i].frequency = legacy.waves[i].frequency;
        full.waves[i].amplitude = legacy.waves[i].amplitude;
        full.waves[i].speed = legacy.waves[i].speed;
    }
    const WaterParams replaced = sceneWaterParams(full, 10.0f);
    EXPECT_FALSE(isLegacyHarmonicPreset(replaced));
    EXPECT_NEAR(replaced.waves[0].amplitude, 0.36f, 1.0e-4f);
    EXPECT_NEAR(replaced.amplitudeScale, 1.5f, 1.0e-4f);
    EXPECT_NEAR(replaced.waterLevel, 10.0f, 1.0e-4f);

    WaterSceneDesc keptSpeed = full;
    keptSpeed.waves[0].speed = 2.0f;
    const WaterParams kept = sceneWaterParams(keptSpeed, 10.0f);
    EXPECT_NEAR(kept.waves[0].speed, 2.0f, 1.0e-3f);
    EXPECT_NEAR(kept.waves[0].amplitude, 0.42f, 1.0e-4f);
}

TEST(WaterWaves, OpposingHeadingsFallBackToTheTangent)
{
    const Vector2f lake(1.0f, 0.0f);
    const Vector2f back = blendFlowDirection(lake, Vector2f(-1.0f, 0.0f), 0.5f);
    EXPECT_NEAR(back.x, -1.0f, 1.0e-4f);
    EXPECT_NEAR(back.y, 0.0f, 1.0e-4f);

    const Vector2f quarter = blendFlowDirection(lake, Vector2f(0.0f, 1.0f), 0.5f);
    EXPECT_NEAR(quarter.x, 0.70710678f, 1.0e-4f);
    EXPECT_NEAR(quarter.y, 0.70710678f, 1.0e-4f);

    const Vector2f mouth = blendFlowDirection(lake, Vector2f(0.0f, 0.0f), 0.0f);
    EXPECT_NEAR(mouth.x, 1.0f, 1.0e-4f);
    EXPECT_NEAR(mouth.y, 0.0f, 1.0e-4f);
}

TEST(WaterBody, CellSizeStaysTwoMeters)
{
    WaterBody pondBody;
    WaterBodyDesc pond;
    pond.center = Vector3f(0.0f, 1.0f, 0.0f);
    pond.extentX = 48.0f;
    pond.extentZ = 48.0f;
    ASSERT_TRUE(pondBody.build(nullptr, pond, defaultWaterParams(1.0f)));
    EXPECT_EQ(pondBody.chunkCount(), 1);
    EXPECT_EQ(pondBody.vertexCount(), 25 * 25);

    WaterBody lake;
    WaterBodyDesc wide;
    wide.center = Vector3f(0.0f, 1.0f, 0.0f);
    wide.extentX = 400.0f;
    wide.extentZ = 400.0f;
    ASSERT_TRUE(lake.build(nullptr, wide, defaultWaterParams(1.0f)));
    EXPECT_EQ(lake.chunksX(), 7);
    EXPECT_EQ(lake.chunksZ(), 7);
    const WaterBodyChunk* first = lake.chunkAt(0, 0);
    ASSERT_NE(first, nullptr);
    ASSERT_EQ(first->cpu.positions.size(), 33u * 33u);
    EXPECT_NEAR(first->cpu.positions[1].x - first->cpu.positions[0].x, 2.0f, 1.0e-3f);

    WaterBodyDesc huge = wide;
    huge.extentX = 1025.0f;
    WaterBody rejected;
    EXPECT_FALSE(rejected.build(nullptr, huge, defaultWaterParams(1.0f)));
}

TEST(WaterBody, AdjacentChunksShareTheSeam)
{
    WaterBody body;
    WaterBodyDesc desc;
    desc.center = Vector3f(0.0f, 2.0f, 0.0f);
    desc.extentX = 128.0f;
    desc.extentZ = 64.0f;
    ASSERT_TRUE(body.build(nullptr, desc, defaultWaterParams(2.0f)));
    EXPECT_EQ(body.chunksX(), 2);
    EXPECT_EQ(body.chunksZ(), 1);

    body.updateLod(Vector3f(-32.0f, 2.0f, 0.0f));
    ASSERT_TRUE(body.needsRebuild());
    body.rebuildDirtyCpuMeshes();

    const WaterBodyChunk* fine = body.chunkAt(0, 0);
    const WaterBodyChunk* coarse = body.chunkAt(1, 0);
    ASSERT_NE(fine, nullptr);
    ASSERT_NE(coarse, nullptr);
    EXPECT_EQ(fine->lod, 0);
    EXPECT_EQ(coarse->lod, 1);

    int shared = 0;
    for (const Vector3f& coarsePos : coarse->cpu.positions)
    {
        if (fabsf(coarsePos.x) > 1.0e-3f)
            continue;
        bool found = false;
        for (const Vector3f& finePos : fine->cpu.positions)
        {
            if (fabsf(finePos.x - coarsePos.x) < 1.0e-3f && fabsf(finePos.z - coarsePos.z) < 1.0e-3f)
            {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found);
        ++shared;
    }
    EXPECT_GT(shared, 2);

    const float a = waveHeight(body.params(), 0.0f, 0.0f, 1.25f);
    const float b = waveHeight(body.params(), 0.0f, 0.0f, 1.25f);
    EXPECT_FLOAT_EQ(a, b);
}

TEST(WaterStream, DownhillReachesTheLake)
{
    HeightMap hm;
    ASSERT_TRUE(hm.create(17, 17, 1.0f, 1.0f));
    for (int z = 0; z < 17; ++z)
    {
        for (int x = 0; x < 17; ++x)
            hm.setHeight(x, z, static_cast<float>(x));
    }

    std::vector<Vector2f> points;
    std::string reason;
    ASSERT_TRUE(followDownhill(hm, Vector2f(16.0f, 8.0f), 0.0f, points, &reason));
    EXPECT_EQ(reason, "reached lake");
    ASSERT_GE(points.size(), 2u);
    for (size_t i = 1; i < points.size(); ++i)
    {
        const float prev = hm.heightAtWorld(points[i - 1].x, points[i - 1].y);
        const float next = hm.heightAtWorld(points[i].x, points[i].y);
        EXPECT_LE(next, prev + 0.05f);
    }
    EXPECT_LE(hm.heightAtWorld(points.back().x, points.back().y), 0.0f);
}

TEST(WaterStream, MouthSitsOnTheLake)
{
    HeightMap hm;
    ASSERT_TRUE(hm.create(33, 33, 1.0f, 1.0f));
    for (int z = 0; z < 33; ++z)
    {
        for (int x = 0; x < 33; ++x)
            hm.setHeight(x, z, static_cast<float>(x));
    }

    StreamDesc desc;
    desc.width = 4.0f;
    desc.pointsXZ.push_back(Vector2f(20.0f, 8.0f));
    desc.pointsXZ.push_back(Vector2f(4.0f, 8.0f));
    MeshData mesh;
    ASSERT_TRUE(buildStreamRibbon(hm, desc, 5.0f, mesh, nullptr, nullptr));
    ASSERT_GE(mesh.positions.size(), 10u);
    ASSERT_EQ(mesh.positions.size() % 5u, 0u);

    const int rings = static_cast<int>(mesh.positions.size() / 5u);
    const int lastCenter = (rings - 1) * 5 + 2;
    // The ribbon sits 0.45 m along the terrain normal, so a 45-degree bank shifts X.
    const Vector3f mouthNormal = hm.normalAtWorld(2.0f, 8.0f);
    const float mouthX = 2.0f + mouthNormal.x * 0.45f;
    EXPECT_NEAR(mesh.uvs[static_cast<size_t>(lastCenter)].x, 1.0f, 1.0e-3f);
    EXPECT_NEAR(mesh.positions[static_cast<size_t>(lastCenter)].x, mouthX, 1.0e-2f);
    EXPECT_NEAR(mesh.positions[static_cast<size_t>(lastCenter)].y, 5.0f, 1.0e-3f);
    EXPECT_NEAR(mesh.tangents[static_cast<size_t>(lastCenter)].w, 0.0f, 1.0e-3f);
    EXPECT_NEAR(mesh.normals[static_cast<size_t>(lastCenter)].x, 4.0f, 1.0e-3f);

    const float upstreamX = 6.0f + mouthNormal.x * 0.45f;
    int back = -1;
    for (int i = 0; i < rings; ++i)
    {
        const int center = i * 5 + 2;
        if (fabsf(mesh.positions[static_cast<size_t>(center)].x - upstreamX) < 0.2f)
            back = center;
    }
    ASSERT_GE(back, 0);
    EXPECT_NEAR(mesh.tangents[static_cast<size_t>(back)].w, 1.0f, 0.05f);

    const WaterParams lake = defaultWaterParams(5.0f);
    const float x = mesh.positions[static_cast<size_t>(lastCenter)].x;
    const float z = mesh.positions[static_cast<size_t>(lastCenter)].z;
    const float cpuOffset = waveHeight(lake, x, z, 1.25f) - 5.0f;
    EXPECT_NEAR(cpuOffset, waveHeight(lake, x, z, 1.25f) - lake.waterLevel, 1.0e-5f);
    EXPECT_NEAR(mesh.positions[static_cast<size_t>(lastCenter)].y, lake.waterLevel, 1.0e-3f);
}
