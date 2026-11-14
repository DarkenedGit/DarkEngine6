#include <gtest/gtest.h>

#include "Math/MathHelper.h"
#include "Terrain/FoliageSpawn.h"
#include "Water/WaterBody.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace Dark::Terrain;

namespace
{
    bool makeMap(HeightMap& hm, SplatMap& splat, uint32_t samplesX, uint32_t samplesZ, float cellSize, float heightScale, float raw, Dark::Math::Vector3f origin, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        if (!hm.createWorking(samplesX, samplesZ, cellSize, heightScale))
            return false;
        hm.setOrigin(origin);
        float* samples = hm.mutableSamples();
        const size_t count = static_cast<size_t>(samplesX) * static_cast<size_t>(samplesZ);
        for (size_t i = 0; i < count; ++i)
            samples[i] = raw;
        if (!splat.createWorking(samplesX, samplesZ))
            return false;
        for (uint32_t z = 0; z < samplesZ; ++z)
        {
            for (uint32_t x = 0; x < samplesX; ++x)
                splat.setTexel(static_cast<int>(x), static_cast<int>(z), r, g, b, a);
        }
        return true;
    }

    bool fillRamp(HeightMap& hm, SplatMap& splat, uint32_t samples, float cellSize, float risePerMetre, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        if (!hm.createWorking(samples, samples, cellSize, 1.0f))
            return false;
        hm.setOrigin(Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f });
        for (uint32_t z = 0; z < samples; ++z)
        {
            for (uint32_t x = 0; x < samples; ++x)
                hm.setHeight(static_cast<int>(x), static_cast<int>(z), static_cast<float>(x) * cellSize * risePerMetre);
        }
        if (!splat.createWorking(samples, samples))
            return false;
        for (uint32_t z = 0; z < samples; ++z)
        {
            for (uint32_t x = 0; x < samples; ++x)
                splat.setTexel(static_cast<int>(x), static_cast<int>(z), r, g, b, a);
        }
        return true;
    }

    FoliageSpawnIn baseIn(const HeightMap& hm, const SplatMap& splat, uint32_t tilesX, uint32_t tilesZ, uint32_t tileCells)
    {
        FoliageSpawnIn in;
        in.height                  = &hm;
        in.splat                   = &splat;
        in.tilesX                  = tilesX;
        in.tilesZ                  = tilesZ;
        in.tileCells               = tileCells;
        in.cellSize                = hm.cellSize();
        in.origin                  = hm.origin();
        in.seaLevel                = -1000.0f;
        in.density.dirtTreesPerM2  = 0.0f;
        in.density.dirtFlowersPerM2 = 0.0f;
        in.density.grassTreesPerM2 = 0.0f;
        in.density.grassFlowersPerM2 = 0.0f;
        in.density.rockPerM2       = 0.0f;
        in.density.treeSpacingM    = 0.0f;
        in.density.rockSpacingM    = 0.0f;
        in.density.seed            = 1337u;
        return in;
    }

    void expectCleared(const FoliageSpawnOut& out)
    {
        EXPECT_TRUE(out.records.empty());
        EXPECT_EQ(out.accepted, 0u);
        EXPECT_EQ(out.kept, 0u);
        EXPECT_FALSE(out.capped);
    }

    void expectFailed(const FoliageSpawnOut& out)
    {
        expectCleared(out);
        EXPECT_FALSE(out.cover.valid());
    }

    void expectPadAndYaw(const FoliageRecord& rec)
    {
        EXPECT_EQ(rec.pad[0], 0);
        EXPECT_EQ(rec.pad[1], 0);
        EXPECT_EQ(rec.pad[2], 0);
        EXPECT_GE(rec.yaw, 0.0f);
        EXPECT_LE(rec.yaw, Dark::Math::TwoPi);
    }

    struct ProgressProbe
    {
        int   coverTicks    = 0;
        int   countRows     = 0;
        int   keepRows      = 0;
        int   keepSeen      = 0;
        int   cancelAfterKeep = 0;
        float maxT          = -1.0f;
    };

    bool onProgress(float t, const char* phase, void* user)
    {
        auto* probe = static_cast<ProgressProbe*>(user);
        EXPECT_NE(phase, nullptr);
        EXPECT_GE(t, 0.0f);
        EXPECT_LE(t, 1.0f);
        if (t > probe->maxT)
            probe->maxT = t;
        if (phase != nullptr && std::strcmp(phase, "cover") == 0)
        {
            ++probe->coverTicks;
            EXPECT_FLOAT_EQ(t, 0.0f);
            return true;
        }
        if (phase != nullptr && phase[0] == 'c')
        {
            ++probe->countRows;
            EXPECT_LE(t, 0.5f);
            return true;
        }
        ++probe->keepRows;
        EXPECT_GE(t, 0.5f);
        ++probe->keepSeen;
        if (probe->cancelAfterKeep > 0 && probe->keepSeen >= probe->cancelAfterKeep)
            return false;
        return true;
    }

    void expectSameRecords(const FoliageSpawnOut& a, const FoliageSpawnOut& b)
    {
        ASSERT_EQ(a.records.size(), b.records.size());
        EXPECT_EQ(a.accepted, b.accepted);
        EXPECT_EQ(a.kept, b.kept);
        for (size_t i = 0; i < a.records.size(); ++i)
        {
            EXPECT_FLOAT_EQ(a.records[i].x, b.records[i].x);
            EXPECT_FLOAT_EQ(a.records[i].y, b.records[i].y);
            EXPECT_FLOAT_EQ(a.records[i].z, b.records[i].z);
            EXPECT_FLOAT_EQ(a.records[i].yaw, b.records[i].yaw);
            EXPECT_FLOAT_EQ(a.records[i].scale, b.records[i].scale);
            EXPECT_FLOAT_EQ(a.records[i].pitch, b.records[i].pitch);
            EXPECT_FLOAT_EQ(a.records[i].tiltYaw, b.records[i].tiltYaw);
            EXPECT_EQ(a.records[i].kind, b.records[i].kind);
        }
    }

    int countKind(const std::vector<FoliageRecord>& records, FoliageKind kind)
    {
        int n = 0;
        for (const FoliageRecord& rec : records)
        {
            if (rec.kind == static_cast<uint8_t>(kind))
                ++n;
        }
        return n;
    }

    int countKindInDisk(const std::vector<FoliageRecord>& records, FoliageKind kind, float cx, float cz, float radius)
    {
        const double r2 = static_cast<double>(radius) * static_cast<double>(radius);
        int          n  = 0;
        for (const FoliageRecord& rec : records)
        {
            if (rec.kind != static_cast<uint8_t>(kind))
                continue;
            const double dx = static_cast<double>(rec.x) - static_cast<double>(cx);
            const double dz = static_cast<double>(rec.z) - static_cast<double>(cz);
            if (dx * dx + dz * dz <= r2)
                ++n;
        }
        return n;
    }
} // namespace

TEST(FoliageSpawn, RejectsBadInputs)
{
    HeightMap hm;
    SplatMap  splat;
    ASSERT_TRUE(makeMap(hm, splat, 5, 3, 1.0f, 1.0f, 1.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));

    FoliageSpawnOut out;
    out.records.push_back(FoliageRecord{});
    out.accepted = 4;
    out.kept     = 4;
    out.capped   = true;

    FoliageSpawnIn in = baseIn(hm, splat, 4, 2, 1);
    in.height         = nullptr;
    EXPECT_FALSE(spawnFoliage(in, out));
    expectFailed(out);

    in         = baseIn(hm, splat, 4, 2, 1);
    in.splat   = nullptr;
    out.records.push_back(FoliageRecord{});
    EXPECT_FALSE(spawnFoliage(in, out));
    expectFailed(out);

    HeightMap invalid;
    in        = baseIn(hm, splat, 4, 2, 1);
    in.height = &invalid;
    EXPECT_FALSE(spawnFoliage(in, out));
    expectFailed(out);

    SplatMap invalidSplat;
    in       = baseIn(hm, splat, 4, 2, 1);
    in.splat = &invalidSplat;
    EXPECT_FALSE(spawnFoliage(in, out));
    expectFailed(out);

    in           = baseIn(hm, splat, 4, 2, 1);
    in.tileCells = 0;
    EXPECT_FALSE(spawnFoliage(in, out));
    expectFailed(out);

    in        = baseIn(hm, splat, 4, 2, 1);
    in.tilesX = 0;
    EXPECT_FALSE(spawnFoliage(in, out));
    expectFailed(out);

    in        = baseIn(hm, splat, 4, 2, 1);
    in.tilesZ = 0;
    EXPECT_FALSE(spawnFoliage(in, out));
    expectFailed(out);

    in           = baseIn(hm, splat, 4, 2, 1);
    in.cellSize  = 0.0f;
    EXPECT_FALSE(spawnFoliage(in, out));
    expectFailed(out);

    in          = baseIn(hm, splat, 4, 2, 1);
    in.cellSize = 2.0f;
    EXPECT_FALSE(spawnFoliage(in, out));
    expectFailed(out);

    in            = baseIn(hm, splat, 4, 2, 1);
    in.origin.y   = 3.0f;
    EXPECT_FALSE(spawnFoliage(in, out));
    expectFailed(out);

    in           = baseIn(hm, splat, 1, 1, 3);
    EXPECT_FALSE(spawnFoliage(in, out));
    expectFailed(out);

    in        = baseIn(hm, splat, 4, 1, 1);
    EXPECT_FALSE(spawnFoliage(in, out));
    expectFailed(out);

    in = baseIn(hm, splat, 4, 2, 1);
    in.density.grassTreesPerM2 = 1.0f;
    ASSERT_TRUE(spawnFoliage(in, out));
    EXPECT_EQ(out.accepted, 8u);
    EXPECT_EQ(out.kept, 8u);
    EXPECT_FALSE(out.capped);
}

TEST(FoliageSpawn, PureGrassPlacesTreesAndFlowersOnly)
{
    HeightMap hm;
    SplatMap  splat;
    ASSERT_TRUE(makeMap(hm, splat, 2, 2, 1.0f, 1.0f, 4.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));
    FoliageSpawnIn in = baseIn(hm, splat, 1, 1, 1);
    in.density.dirtTreesPerM2      = 1.0f;
    in.density.grassTreesPerM2     = 1.0f;
    in.density.dirtFlowersPerM2    = 2.0f;
    in.density.grassFlowersPerM2   = 1.0f;
    in.density.rockPerM2           = 1.0f;
    in.seaLevel                    = 4.0f;

    FoliageSpawnOut out;
    ASSERT_TRUE(spawnFoliage(in, out));
    ASSERT_EQ(out.records.size(), 1u);
    EXPECT_EQ(out.accepted, 1u);
    EXPECT_EQ(out.kept, 1u);
    EXPECT_FALSE(out.capped);
    EXPECT_EQ(out.records[0].kind, static_cast<uint8_t>(FoliageKind::Tree));
    EXPECT_FLOAT_EQ(out.records[0].pitch, 0.0f);
    EXPECT_FLOAT_EQ(out.records[0].tiltYaw, 0.0f);
    EXPECT_FLOAT_EQ(out.records[0].y, 4.0f);
    EXPECT_GE(out.records[0].scale, 0.85f);
    EXPECT_LE(out.records[0].scale, 1.15f);
    EXPECT_NEAR(out.records[0].x, 0.5f, 0.45f);
    EXPECT_NEAR(out.records[0].z, 0.5f, 0.45f);
    expectPadAndYaw(out.records[0]);
    EXPECT_EQ(out.cover.sampleNearest(out.records[0].x, out.records[0].z).coverage, 0);

    in.density.dirtTreesPerM2  = 0.0f;
    in.density.grassTreesPerM2 = 0.0f;
    in.density.rockPerM2       = 0.0f;
    ASSERT_TRUE(spawnFoliage(in, out));
    ASSERT_EQ(out.records.size(), 1u);
    EXPECT_EQ(out.records[0].kind, static_cast<uint8_t>(FoliageKind::Flower));
    EXPECT_FLOAT_EQ(out.records[0].pitch, 0.0f);
    EXPECT_FLOAT_EQ(out.records[0].tiltYaw, 0.0f);
    EXPECT_FLOAT_EQ(out.records[0].y, 4.0f);
    EXPECT_GE(out.records[0].scale, 0.80f);
    EXPECT_LE(out.records[0].scale, 1.20f);
    EXPECT_NEAR(out.records[0].x, 0.25f, 0.12f);
    EXPECT_NEAR(out.records[0].z, 0.5f, 0.12f);
    EXPECT_LT(out.records[0].x, 0.5f);
    expectPadAndYaw(out.records[0]);
}

TEST(FoliageSpawn, PureRockPlacesRocksOnly)
{
    HeightMap hm;
    SplatMap  splat;
    ASSERT_TRUE(makeMap(hm, splat, 2, 2, 1.0f, 1.0f, 2.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 0, 255, 0));
    FoliageSpawnIn in = baseIn(hm, splat, 1, 1, 1);
    in.density.dirtTreesPerM2    = 1.0f;
    in.density.grassTreesPerM2   = 1.0f;
    in.density.dirtFlowersPerM2  = 2.0f;
    in.density.grassFlowersPerM2 = 2.0f;
    in.density.rockPerM2         = 5.0f;

    FoliageSpawnOut out;
    ASSERT_TRUE(spawnFoliage(in, out));
    ASSERT_EQ(out.records.size(), 1u);
    EXPECT_EQ(out.records[0].kind, static_cast<uint8_t>(FoliageKind::Rock));
    EXPECT_FLOAT_EQ(out.records[0].pitch, 0.0f);
    EXPECT_FLOAT_EQ(out.records[0].tiltYaw, 0.0f);
    EXPECT_FLOAT_EQ(out.records[0].y, 2.0f);
    EXPECT_GE(out.records[0].scale, 0.60f);
    EXPECT_LE(out.records[0].scale, 1.80f);
    expectPadAndYaw(out.records[0]);
}

TEST(FoliageSpawn, TreeCountFollowsGrassWeight)
{
    HeightMap full;
    SplatMap  fullSplat;
    ASSERT_TRUE(makeMap(full, fullSplat, 9, 9, 1.0f, 1.0f, 2.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));
    FoliageSpawnIn fullIn = baseIn(full, fullSplat, 1, 1, 8);
    fullIn.density.grassTreesPerM2 = 1.0f;

    FoliageSpawnOut fullOut;
    ASSERT_TRUE(spawnFoliage(fullIn, fullOut));
    uint32_t fullTrees = 0;
    for (const FoliageRecord& rec : fullOut.records)
    {
        if (rec.kind == static_cast<uint8_t>(FoliageKind::Tree))
            ++fullTrees;
    }
    EXPECT_EQ(fullTrees, 64u);

    HeightMap half;
    SplatMap  halfSplat;
    ASSERT_TRUE(makeMap(half, halfSplat, 9, 9, 1.0f, 1.0f, 2.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 128, 127, 0));
    FoliageSpawnIn halfIn = baseIn(half, halfSplat, 1, 1, 8);
    halfIn.density.grassTreesPerM2 = 1.0f;
    halfIn.density.seed            = fullIn.density.seed;

    FoliageSpawnOut halfOut;
    ASSERT_TRUE(spawnFoliage(halfIn, halfOut));
    uint32_t halfTrees = 0;
    for (const FoliageRecord& rec : halfOut.records)
    {
        EXPECT_NE(rec.kind, static_cast<uint8_t>(FoliageKind::Rock));
        if (rec.kind == static_cast<uint8_t>(FoliageKind::Tree))
            ++halfTrees;
    }
    EXPECT_GT(halfTrees, 0u);
    EXPECT_LT(halfTrees, fullTrees);
}

TEST(FoliageSpawn, PureSnowPlacesNothing)
{
    HeightMap hm;
    SplatMap  splat;
    ASSERT_TRUE(makeMap(hm, splat, 3, 3, 1.0f, 1.0f, 8.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 0, 0, 255));
    FoliageSpawnIn in = baseIn(hm, splat, 1, 1, 2);
    in.density.dirtTreesPerM2    = 1.0f;
    in.density.grassTreesPerM2   = 1.0f;
    in.density.dirtFlowersPerM2  = 2.0f;
    in.density.grassFlowersPerM2 = 2.0f;
    in.density.rockPerM2         = 1.0f;
    in.density.grassPerM2        = 1.0f;

    FoliageSpawnOut out;
    ASSERT_TRUE(spawnFoliage(in, out));
    expectCleared(out);
}

TEST(FoliageSpawn, ZeroTexelIsNotGrass)
{
    HeightMap hm;
    SplatMap  splat;
    ASSERT_TRUE(makeMap(hm, splat, 2, 2, 1.0f, 1.0f, 3.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 0, 0, 0));
    FoliageSpawnIn in = baseIn(hm, splat, 1, 1, 1);
    in.density.dirtTreesPerM2    = 1.0f;
    in.density.grassTreesPerM2   = 1.0f;
    in.density.dirtFlowersPerM2  = 2.0f;
    in.density.grassFlowersPerM2 = 2.0f;
    in.density.rockPerM2         = 1.0f;
    in.density.grassPerM2        = 1.0f;

    FoliageSpawnOut out;
    ASSERT_TRUE(spawnFoliage(in, out));
    expectCleared(out);

    in.density.grassTreesPerM2   = 0.0f;
    in.density.grassFlowersPerM2 = 0.0f;
    in.density.dirtTreesPerM2    = -4.0f;
    in.density.rockPerM2         = -1.0f;
    ASSERT_TRUE(makeMap(hm, splat, 4, 4, 1.0f, 1.0f, 3.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));
    in = baseIn(hm, splat, 1, 1, 3);
    in.density.grassTreesPerM2 = -2.0f;
    in.density.grassFlowersPerM2 = -3.0f;
    in.density.rockPerM2 = -1.0f;
    ASSERT_TRUE(spawnFoliage(in, out));
    expectCleared(out);
}

TEST(FoliageSpawn, SeaLevelUsesWorldY)
{
    HeightMap hm;
    SplatMap  splat;
    ASSERT_TRUE(makeMap(hm, splat, 2, 2, 1.0f, 0.1f, 10.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));
    FoliageSpawnIn in = baseIn(hm, splat, 1, 1, 1);
    in.density.grassTreesPerM2 = 1.0f;
    in.seaLevel                = 2.0f;

    FoliageSpawnOut out;
    ASSERT_TRUE(spawnFoliage(in, out));
    expectCleared(out);

    ASSERT_TRUE(makeMap(hm, splat, 2, 2, 1.0f, 0.5f, 10.0f, Dark::Math::Vector3f{ 1.0f, 0.0f, -2.0f }, 0, 255, 0, 0));
    in          = baseIn(hm, splat, 1, 1, 1);
    in.density.grassTreesPerM2 = 1.0f;
    in.seaLevel = 5.0f;
    ASSERT_TRUE(spawnFoliage(in, out));
    ASSERT_EQ(out.records.size(), 1u);
    EXPECT_FLOAT_EQ(out.records[0].y, 5.0f);
    EXPECT_NEAR(out.records[0].x, 1.5f, 0.45f);
    EXPECT_NEAR(out.records[0].z, -1.5f, 0.45f);
    EXPECT_EQ(out.accepted, 1u);
    EXPECT_FALSE(out.capped);
}

TEST(FoliageSpawn, FlowerSlotsFollowRate)
{
    HeightMap hm;
    SplatMap  splat;
    ASSERT_TRUE(makeMap(hm, splat, 2, 2, 1.0f, 1.0f, 1.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));
    FoliageSpawnIn in = baseIn(hm, splat, 1, 1, 1);
    in.density.grassFlowersPerM2 = 2.0f;

    FoliageSpawnOut out;
    ASSERT_TRUE(spawnFoliage(in, out));
    ASSERT_EQ(out.records.size(), 2u);
    EXPECT_EQ(out.records[0].kind, static_cast<uint8_t>(FoliageKind::Flower));
    EXPECT_EQ(out.records[1].kind, static_cast<uint8_t>(FoliageKind::Flower));
    EXPECT_NEAR(out.records[0].x, 0.25f, 0.12f);
    EXPECT_NEAR(out.records[1].x, 0.75f, 0.12f);
    EXPECT_LT(out.records[0].x, 0.5f);
    EXPECT_GT(out.records[1].x, 0.5f);
    EXPECT_LT(out.records[0].x, out.records[1].x);
    EXPECT_FLOAT_EQ(out.records[0].pitch, 0.0f);
    EXPECT_FLOAT_EQ(out.records[1].tiltYaw, 0.0f);

    in.density.grassFlowersPerM2 = 1.0f;
    ASSERT_TRUE(spawnFoliage(in, out));
    ASSERT_EQ(out.records.size(), 1u);
    EXPECT_EQ(out.records[0].kind, static_cast<uint8_t>(FoliageKind::Flower));
    EXPECT_NEAR(out.records[0].x, 0.25f, 0.12f);
    EXPECT_LT(out.records[0].x, 0.5f);

    in.density.grassFlowersPerM2 = 5.0f;
    in.density.grassTreesPerM2   = 5.0f;
    ASSERT_TRUE(spawnFoliage(in, out));
    ASSERT_EQ(out.records.size(), 1u);
    EXPECT_EQ(out.records[0].kind, static_cast<uint8_t>(FoliageKind::Tree));
    EXPECT_EQ(out.accepted, 1u);
    EXPECT_EQ(out.cover.sampleNearest(out.records[0].x, out.records[0].z).coverage, 0);
}

TEST(FoliageSpawn, SameSeedRepeats)
{
    HeightMap hm;
    SplatMap  splat;
    ASSERT_TRUE(makeMap(hm, splat, 9, 9, 1.0f, 1.0f, 2.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));
    FoliageSpawnIn in = baseIn(hm, splat, 1, 1, 8);
    in.density.grassTreesPerM2   = 0.35f;
    in.density.grassFlowersPerM2 = 0.8f;
    in.density.rockPerM2         = 0.5f;
    in.density.seed              = 99u;

    FoliageSpawnOut a;
    FoliageSpawnOut b;
    ASSERT_TRUE(spawnFoliage(in, a));
    ASSERT_TRUE(spawnFoliage(in, b));
    EXPECT_GT(a.records.size(), 0u);
    expectSameRecords(a, b);

    in.density.patchStrength = 1.0f;
    FoliageSpawnOut clusteredA;
    FoliageSpawnOut clusteredB;
    ASSERT_TRUE(spawnFoliage(in, clusteredA));
    ASSERT_TRUE(spawnFoliage(in, clusteredB));
    EXPECT_GT(clusteredA.records.size(), 0u);
    expectSameRecords(clusteredA, clusteredB);
}

TEST(FoliageSpawn, LatticeIsOneMetreNotSamples)
{
    HeightMap hm;
    SplatMap  splat;
    ASSERT_TRUE(makeMap(hm, splat, 129, 129, 2.0f, 1.0f, 1.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));
    FoliageSpawnIn in = baseIn(hm, splat, 1, 1, 128);
    in.density.grassTreesPerM2 = 1.0f;
    in.density.treeSpacingM    = 0.0f;

    FoliageSpawnOut out;
    ASSERT_TRUE(spawnFoliage(in, out));
    EXPECT_EQ(out.accepted, 256ull * 256ull);
    EXPECT_EQ(out.kept, 256u * 256u);
    EXPECT_FALSE(out.capped);
    ASSERT_EQ(out.records.size(), static_cast<size_t>(256u * 256u));
    EXPECT_EQ(out.records.front().kind, static_cast<uint8_t>(FoliageKind::Tree));
    EXPECT_EQ(out.records.back().kind, static_cast<uint8_t>(FoliageKind::Tree));

    in.tileCells = 512;
    EXPECT_FALSE(spawnFoliage(in, out));
    expectFailed(out);

    in.tileCells = 0;
    EXPECT_FALSE(spawnFoliage(in, out));
    expectFailed(out);

    ASSERT_TRUE(makeMap(hm, splat, 2, 2, 1.5f, 1.0f, 1.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));
    in = baseIn(hm, splat, 1, 1, 1);
    in.density.grassTreesPerM2 = 1.0f;
    ASSERT_TRUE(spawnFoliage(in, out));
    EXPECT_EQ(out.accepted, 1u);
    ASSERT_EQ(out.records.size(), 1u);

    ASSERT_TRUE(makeMap(hm, splat, 2, 2, 0.5f, 1.0f, 1.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));
    in = baseIn(hm, splat, 1, 1, 1);
    in.density.grassTreesPerM2 = 1.0f;
    ASSERT_TRUE(spawnFoliage(in, out));
    expectCleared(out);
}

TEST(FoliageSpawn, TreesRejectedOn45DegGrassRamp)
{
    HeightMap hm;
    SplatMap  grass;
    ASSERT_TRUE(fillRamp(hm, grass, 4, 1.0f, 1.0f, 0, 255, 0, 0));
    FoliageSpawnIn in = baseIn(hm, grass, 1, 1, 3);
    in.density.grassTreesPerM2 = 1.0f;
    in.seaLevel                = -1.0f;

    FoliageSpawnOut out;
    ASSERT_TRUE(spawnFoliage(in, out));
    EXPECT_EQ(out.accepted, 0u);
    EXPECT_TRUE(out.records.empty());
}

TEST(FoliageSpawn, GentleSlopeTreesOptionalLean)
{
    HeightMap hm;
    SplatMap  grass;
    ASSERT_TRUE(fillRamp(hm, grass, 4, 1.0f, 0.25f, 0, 255, 0, 0));
    FoliageSpawnIn in = baseIn(hm, grass, 1, 1, 3);
    in.density.grassTreesPerM2 = 1.0f;
    in.density.treeMaxLeanDeg  = 0.0f;
    in.seaLevel                = -1.0f;

    FoliageSpawnOut upright;
    ASSERT_TRUE(spawnFoliage(in, upright));
    ASSERT_EQ(upright.records.size(), 9u);
    for (const FoliageRecord& rec : upright.records)
    {
        EXPECT_EQ(rec.kind, static_cast<uint8_t>(FoliageKind::Tree));
        EXPECT_FLOAT_EQ(rec.pitch, 0.0f);
        EXPECT_FLOAT_EQ(rec.tiltYaw, 0.0f);
        expectPadAndYaw(rec);
    }

    in.density.treeMaxLeanDeg = 7.0f;
    FoliageSpawnOut leaned;
    ASSERT_TRUE(spawnFoliage(in, leaned));
    ASSERT_EQ(leaned.records.size(), 9u);
    const float cap = 7.0f * Dark::Math::DegToRad;
    for (const FoliageRecord& rec : leaned.records)
    {
        EXPECT_EQ(rec.kind, static_cast<uint8_t>(FoliageKind::Tree));
        const Dark::Math::Vector3f n = hm.normalAtWorld(rec.x, rec.z);
        const float slope = std::acos(Dark::Math::Clamp(n.y, -1.0f, 1.0f));
        const float expectPitch = slope < cap ? slope : cap;
        EXPECT_GT(rec.pitch, 0.0f);
        EXPECT_LE(rec.pitch, cap + 1.0e-5f);
        EXPECT_NEAR(rec.pitch, expectPitch, 1.0e-5f);
        EXPECT_NEAR(rec.tiltYaw, std::atan2(n.x, n.z), 1.0e-5f);
        expectPadAndYaw(rec);
    }
}

TEST(FoliageSpawn, RockPitchIsClamped)
{
    HeightMap hm;
    SplatMap  rock;
    ASSERT_TRUE(fillRamp(hm, rock, 4, 1.0f, 1.0f, 0, 0, 255, 0));
    FoliageSpawnIn in = baseIn(hm, rock, 1, 1, 3);
    // A 1 m cell softens the border below 45 degrees. This seed keeps those normals under 0.80 and steeper than 30.
    in.density.seed        = 43u;
    in.density.rockPerM2   = 1.0f;
    in.density.rockMaxPitchDeg = 30.0f;
    in.seaLevel            = -1.0f;

    FoliageSpawnOut out;
    ASSERT_TRUE(spawnFoliage(in, out));
    ASSERT_EQ(out.records.size(), 9u);
    const float cap = 30.0f * Dark::Math::DegToRad;
    for (const FoliageRecord& rec : out.records)
    {
        EXPECT_EQ(rec.kind, static_cast<uint8_t>(FoliageKind::Rock));
        const Dark::Math::Vector3f n = hm.normalAtWorld(rec.x, rec.z);
        const float full = std::acos(Dark::Math::Clamp(n.y, -1.0f, 1.0f));
        EXPECT_GT(full, cap);
        EXPECT_NEAR(rec.pitch, cap, 1.0e-5f);
        EXPECT_NEAR(rec.tiltYaw, std::atan2(n.x, n.z), 1.0e-5f);
        EXPECT_GE(rec.scale, 0.60f);
        EXPECT_LE(rec.scale, 1.80f);
        expectPadAndYaw(rec);
    }

    in.density.rockMinNormalY = 0.80f;
    FoliageSpawnOut steep;
    ASSERT_TRUE(spawnFoliage(in, steep));
    EXPECT_EQ(steep.accepted, 0u);
    EXPECT_TRUE(steep.records.empty());

    in.density.rockMinNormalY  = 0.40f;
    in.density.rockMaxPitchDeg = 0.0f;
    FoliageSpawnOut upright;
    ASSERT_TRUE(spawnFoliage(in, upright));
    ASSERT_EQ(upright.records.size(), 9u);
    for (const FoliageRecord& rec : upright.records)
    {
        EXPECT_EQ(rec.kind, static_cast<uint8_t>(FoliageKind::Rock));
        EXPECT_FLOAT_EQ(rec.pitch, 0.0f);
        EXPECT_FLOAT_EQ(rec.tiltYaw, 0.0f);
    }
}

TEST(FoliageSpawn, FlowersAndTuftsFollowTheirThreshold)
{
    // A 1 m height cell clamps the border difference above 0.82. This cell keeps every candidate on the 45 degree face.
    HeightMap hm;
    SplatMap  grass;
    constexpr float    cell    = 0.03125f;
    constexpr uint32_t samples = 129u;
    ASSERT_TRUE(fillRamp(hm, grass, samples, cell, 1.0f, 0, 255, 0, 0));
    FoliageSpawnIn in = baseIn(hm, grass, 1, 1, samples - 1u);
    in.density.grassFlowersPerM2 = 2.0f;
    in.density.grassPerM2        = 1.0f;
    in.seaLevel                  = -1.0f;

    FoliageSpawnOut ramp;
    ASSERT_TRUE(spawnFoliage(in, ramp));
    EXPECT_EQ(ramp.accepted, 0u);
    EXPECT_TRUE(ramp.records.empty());

    HeightMap flatHm;
    SplatMap  flatSplat;
    ASSERT_TRUE(makeMap(flatHm, flatSplat, 9, 9, 1.0f, 1.0f, 1.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));
    FoliageSpawnIn flatIn = baseIn(flatHm, flatSplat, 1, 1, 8);
    flatIn.density.grassFlowersPerM2 = 2.0f;
    flatIn.density.grassPerM2        = 1.0f;
    FoliageSpawnOut flat;
    ASSERT_TRUE(spawnFoliage(flatIn, flat));
    uint32_t flowers = 0;
    uint32_t tufts   = 0;
    for (const FoliageRecord& rec : flat.records)
    {
        EXPECT_FLOAT_EQ(rec.pitch, 0.0f);
        EXPECT_FLOAT_EQ(rec.tiltYaw, 0.0f);
        if (rec.kind == static_cast<uint8_t>(FoliageKind::Flower))
            ++flowers;
        else if (rec.kind == static_cast<uint8_t>(FoliageKind::Grass))
            ++tufts;
        else
            ADD_FAILURE() << "unexpected kind " << static_cast<int>(rec.kind);
    }
    EXPECT_EQ(flowers, 128u);
    EXPECT_EQ(tufts, 64u);
}

TEST(FoliageSpawn, ProgressAndCancel)
{
    HeightMap hm;
    SplatMap  splat;
    ASSERT_TRUE(makeMap(hm, splat, 4, 4, 1.0f, 1.0f, 1.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));
    FoliageSpawnIn in = baseIn(hm, splat, 1, 1, 3);
    in.density.grassTreesPerM2 = 1.0f;
    ProgressProbe probe;
    in.onProgress = &onProgress;
    in.user       = &probe;

    FoliageSpawnOut out;
    out.records.push_back(FoliageRecord{});
    ASSERT_TRUE(spawnFoliage(in, out));
    EXPECT_EQ(probe.coverTicks, 4);
    EXPECT_EQ(probe.countRows, 3);
    EXPECT_EQ(probe.keepRows, 3);
    EXPECT_FLOAT_EQ(probe.maxT, 1.0f);
    EXPECT_EQ(out.records.size(), 9u);
    EXPECT_TRUE(out.cover.valid());

    probe                 = {};
    probe.cancelAfterKeep = 1;
    out.records.push_back(FoliageRecord{});
    EXPECT_FALSE(spawnFoliage(in, out));
    expectFailed(out);
    EXPECT_EQ(probe.coverTicks, 4);
    EXPECT_EQ(probe.countRows, 3);
    EXPECT_GE(probe.keepRows, 1);
}

TEST(FoliageSpawn, CapSpreadsAcrossMap)
{
    constexpr uint32_t kSamples = 2050u;
    constexpr uint32_t kCells   = 2049u;
    HeightMap          hm;
    SplatMap           splat;
    ASSERT_TRUE(makeMap(hm, splat, kSamples, kSamples, 1.0f, 1.0f, 1.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));

    FoliageSpawnIn in = baseIn(hm, splat, 1, 1, kCells);
    in.density.grassFlowersPerM2 = 2.0f;
    in.density.grassPerM2        = 1.0f;
    in.density.treeSpacingM      = 0.0f;
    in.seaLevel                  = 0.0f;

    FoliageSpawnOut out;
    ASSERT_TRUE(spawnFoliage(in, out));

    const uint64_t cells = static_cast<uint64_t>(kCells) * static_cast<uint64_t>(kCells);
    EXPECT_EQ(kMaxFoliageInstances, 12582912u);
    EXPECT_EQ(out.accepted, cells * 3ull);
    EXPECT_EQ(out.accepted, 12595203ull);
    EXPECT_EQ(out.kept, 12582912u);
    EXPECT_EQ(out.kept, kMaxFoliageInstances);
    EXPECT_TRUE(out.capped);
    ASSERT_EQ(out.records.size(), static_cast<size_t>(kMaxFoliageInstances));

    bool     low   = false;
    bool     high  = false;
    float    maxZ  = -1.0f;
    uint32_t rocks = 0;
    uint32_t other = 0;
    for (const FoliageRecord& rec : out.records)
    {
        if (rec.z < 1024.5f)
            low = true;
        if (rec.z >= 1024.5f)
            high = true;
        if (rec.z > maxZ)
            maxZ = rec.z;
        if (rec.kind == static_cast<uint8_t>(FoliageKind::Rock))
            ++rocks;
        else if (rec.kind != static_cast<uint8_t>(FoliageKind::Flower) && rec.kind != static_cast<uint8_t>(FoliageKind::Grass))
            ++other;
    }
    EXPECT_TRUE(low);
    EXPECT_TRUE(high);
    EXPECT_GT(maxZ, 2048.0f);
    EXPECT_EQ(rocks, 0u);
    EXPECT_EQ(other, 0u);
}

TEST(FoliageSpawn, GrassFollowsGrassLayer)
{
    HeightMap grassHm;
    SplatMap  grassSplat;
    ASSERT_TRUE(makeMap(grassHm, grassSplat, 9, 9, 1.0f, 1.0f, 2.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));
    FoliageSpawnIn grassIn = baseIn(grassHm, grassSplat, 1, 1, 8);
    grassIn.density.grassPerM2 = 1.0f;

    FoliageSpawnOut grassOut;
    ASSERT_TRUE(spawnFoliage(grassIn, grassOut));
    ASSERT_EQ(grassOut.records.size(), 64u);
    for (const FoliageRecord& rec : grassOut.records)
    {
        EXPECT_EQ(rec.kind, static_cast<uint8_t>(FoliageKind::Grass));
        EXPECT_GE(rec.scale, 0.80f);
        EXPECT_LE(rec.scale, 1.25f);
        expectPadAndYaw(rec);
    }

    HeightMap dirtHm;
    SplatMap  dirtSplat;
    ASSERT_TRUE(makeMap(dirtHm, dirtSplat, 9, 9, 1.0f, 1.0f, 2.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 255, 0, 0, 0));
    FoliageSpawnIn dirtIn = baseIn(dirtHm, dirtSplat, 1, 1, 8);
    dirtIn.density.grassPerM2 = 1.0f;
    FoliageSpawnOut dirtOut;
    ASSERT_TRUE(spawnFoliage(dirtIn, dirtOut));
    expectCleared(dirtOut);
}

TEST(FoliageSpawn, TreesAndGrassSkipWaterVolumes)
{
    HeightMap hm;
    SplatMap  splat;
    ASSERT_TRUE(makeMap(hm, splat, 9, 9, 1.0f, 1.0f, 1.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));

    FoliageSpawnIn in = baseIn(hm, splat, 1, 1, 8);
    in.density.grassTreesPerM2 = 1.0f;
    // Covers cell columns 0 and 1 after ±0.45 m jitter. Column 2 starts at x = 2.05.
    // A bed under surfaceY is rejected. Trees are spawned alone so the trunk mask is not what removes them.
    FoliageWaterVolume pond;
    pond.minX     = -1.0f;
    pond.maxX     = 2.0f;
    pond.minZ     = -10.0f;
    pond.maxZ     = 100.0f;
    pond.surfaceY = 3.0f;
    in.water.push_back(pond);

    FoliageSpawnOut out;
    ASSERT_TRUE(spawnFoliage(in, out));
    ASSERT_EQ(out.records.size(), 48u);
    for (const FoliageRecord& rec : out.records)
    {
        EXPECT_EQ(rec.kind, static_cast<uint8_t>(FoliageKind::Tree));
        EXPECT_GT(rec.x, 2.0f);
        EXPECT_FLOAT_EQ(rec.y, 1.0f);
    }

    in.density.grassTreesPerM2   = 0.0f;
    in.density.grassFlowersPerM2 = 2.0f;
    in.density.grassPerM2        = 1.0f;
    ASSERT_TRUE(spawnFoliage(in, out));
    uint32_t flowers = 0;
    uint32_t grass = 0;
    uint32_t flowersInPond = 0;
    for (const FoliageRecord& rec : out.records)
    {
        const auto kind = static_cast<FoliageKind>(rec.kind);
        if (kind == FoliageKind::Grass)
        {
            ++grass;
            EXPECT_GT(rec.x, 2.0f);
        }
        else if (kind == FoliageKind::Flower)
        {
            ++flowers;
            EXPECT_GT(rec.x, pond.maxX);
            if (rec.x <= pond.maxX && rec.z >= pond.minZ && rec.z <= pond.maxZ)
                ++flowersInPond;
        }
        else
            ADD_FAILURE() << "unexpected kind " << static_cast<int>(rec.kind);
    }
    EXPECT_EQ(grass, 48u);
    EXPECT_EQ(flowers, 96u);
    EXPECT_EQ(flowersInPond, 0u);

    FoliageSpawnIn shore = baseIn(hm, splat, 1, 1, 8);
    shore.density.grassTreesPerM2 = 1.0f;
    FoliageWaterVolume atBed;
    atBed.entireMap = true;
    atBed.surfaceY  = 1.0f;
    shore.water.push_back(atBed);
    FoliageSpawnOut shoreOut;
    ASSERT_TRUE(spawnFoliage(shore, shoreOut));
    EXPECT_EQ(shoreOut.records.size(), 64u);

    shore.density.grassTreesPerM2 = 0.0f;
    shore.density.grassPerM2      = 1.0f;
    ASSERT_TRUE(spawnFoliage(shore, shoreOut));
    EXPECT_EQ(shoreOut.records.size(), 64u);
    for (const FoliageRecord& rec : shoreOut.records)
        EXPECT_EQ(rec.kind, static_cast<uint8_t>(FoliageKind::Grass));

    FoliageSpawnIn flooded = in;
    flooded.water.clear();
    FoliageWaterVolume lake;
    lake.entireMap = true;
    lake.surfaceY  = 3.0f;
    flooded.water.push_back(lake);
    FoliageSpawnOut floodOut;
    ASSERT_TRUE(spawnFoliage(flooded, floodOut));
    expectCleared(floodOut);

    HeightMap rockHm;
    SplatMap  rockSplat;
    ASSERT_TRUE(makeMap(rockHm, rockSplat, 2, 2, 1.0f, 1.0f, 2.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 0, 255, 0));
    FoliageSpawnIn rockIn = baseIn(rockHm, rockSplat, 1, 1, 1);
    rockIn.density.rockPerM2       = 1.0f;
    rockIn.density.grassTreesPerM2 = 1.0f;
    rockIn.density.grassPerM2      = 1.0f;
    FoliageWaterVolume overRock;
    overRock.entireMap = true;
    overRock.surfaceY  = 10.0f;
    rockIn.water.push_back(overRock);
    FoliageSpawnOut rockOut;
    ASSERT_TRUE(spawnFoliage(rockIn, rockOut));
    ASSERT_EQ(rockOut.records.size(), 1u);
    EXPECT_EQ(rockOut.records[0].kind, static_cast<uint8_t>(FoliageKind::Rock));
    EXPECT_FLOAT_EQ(rockOut.records[0].y, 2.0f);
}

TEST(FoliageSpawn, PlacedWaterMatchesEditorVolumes)
{
    HeightMap hm;
    SplatMap  splat;
    ASSERT_TRUE(makeMap(hm, splat, 9, 9, 1.0f, 1.0f, 1.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));

    struct Placed
    {
        bool  water;
        float x;
        float y;
        float z;
        float extentX;
        float extentZ;
    };
    const Placed placed[] = {
        { false, 2.0f, 5.0f, 4.0f, 4.0f, 16.0f },
        { true, 2.0f, 5.0f, 4.0f, 4.0f, 16.0f },
        { true, 0.0f, 8.0f, 0.0f, 2000.0f, 16.0f },
    };

    FoliageSpawnIn in = baseIn(hm, splat, 1, 1, 8);
    in.density.grassTreesPerM2 = 1.0f;
    in.seaLevel                = -1.0f;
    FoliageWaterVolume sheet;
    sheet.entireMap = true;
    sheet.surfaceY  = in.seaLevel;
    in.water.push_back(sheet);
    for (const Placed& body : placed)
    {
        if (!body.water)
            continue;
        float minX = 0.0f;
        float maxX = 0.0f;
        float minZ = 0.0f;
        float maxZ = 0.0f;
        if (!Dark::placedWaterFootprint(body.x, body.z, body.extentX, body.extentZ, minX, maxX, minZ, maxZ))
            continue;
        FoliageWaterVolume pond;
        pond.minX     = minX;
        pond.maxX     = maxX;
        pond.minZ     = minZ;
        pond.maxZ     = maxZ;
        pond.surfaceY = body.y;
        in.water.push_back(pond);
    }
    ASSERT_EQ(in.water.size(), 2u);

    FoliageSpawnOut out;
    ASSERT_TRUE(spawnFoliage(in, out));
    ASSERT_EQ(out.records.size(), 32u);
    for (const FoliageRecord& rec : out.records)
    {
        EXPECT_EQ(rec.kind, static_cast<uint8_t>(FoliageKind::Tree));
        EXPECT_GT(rec.x, 4.0f);
        EXPECT_FLOAT_EQ(rec.y, 1.0f);
    }
}

TEST(FoliageSpawn, TreesRespectMinSpacing)
{
    HeightMap hm;
    SplatMap  splat;
    ASSERT_TRUE(makeMap(hm, splat, 49, 49, 1.0f, 1.0f, 1.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));
    FoliageSpawnIn in = baseIn(hm, splat, 1, 1, 48);
    in.density.grassTreesPerM2 = 1.0f;
    in.density.treeSpacingM    = 3.0f;
    in.density.seed            = 1337u;

    FoliageSpawnOut a;
    ASSERT_TRUE(spawnFoliage(in, a));
    EXPECT_TRUE(a.cover.valid());
    ASSERT_GT(a.records.size(), 8u);

    for (size_t i = 0; i < a.records.size(); ++i)
    {
        EXPECT_EQ(a.records[i].kind, static_cast<uint8_t>(FoliageKind::Tree));
        for (size_t j = i + 1; j < a.records.size(); ++j)
        {
            const double dx  = static_cast<double>(a.records[i].x) - static_cast<double>(a.records[j].x);
            const double dz  = static_cast<double>(a.records[i].z) - static_cast<double>(a.records[j].z);
            const double lim = 3.0 * 0.5 * (static_cast<double>(a.records[i].scale) + static_cast<double>(a.records[j].scale));
            EXPECT_GE(std::sqrt(dx * dx + dz * dz), lim - 1.0e-3);
        }
    }

    FoliageSpawnOut b;
    ASSERT_TRUE(spawnFoliage(in, b));
    ASSERT_EQ(a.records.size(), b.records.size());
    for (size_t i = 0; i < a.records.size(); ++i)
    {
        EXPECT_FLOAT_EQ(a.records[i].x, b.records[i].x);
        EXPECT_FLOAT_EQ(a.records[i].z, b.records[i].z);
        EXPECT_FLOAT_EQ(a.records[i].scale, b.records[i].scale);
        EXPECT_EQ(a.records[i].kind, b.records[i].kind);
    }
}

TEST(FoliageSpawn, FlowersAndTuftsAvoidTrunkCore)
{
    HeightMap hm;
    SplatMap  splat;
    ASSERT_TRUE(makeMap(hm, splat, 9, 9, 1.0f, 1.0f, 1.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));
    FoliageSpawnIn in = baseIn(hm, splat, 1, 1, 8);
    in.density.grassTreesPerM2   = 1.0f;
    in.density.grassFlowersPerM2 = 2.0f;
    in.density.grassPerM2        = 1.0f;
    in.density.treeSpacingM = 16.0f;
    in.treeExtentX          = 8.0f;
    in.treeExtentZ          = 8.0f;

    FoliageSpawnOut out;
    ASSERT_TRUE(spawnFoliage(in, out));
    ASSERT_TRUE(out.cover.valid());
    EXPECT_FLOAT_EQ(out.cover.cellSize(), 1.0f);

    const FoliageRecord* tree = nullptr;
    int                  trees = 0;
    for (const FoliageRecord& rec : out.records)
    {
        if (rec.kind == static_cast<uint8_t>(FoliageKind::Tree))
        {
            tree = &rec;
            ++trees;
        }
    }
    ASSERT_EQ(trees, 1);
    ASSERT_NE(tree, nullptr);
    EXPECT_EQ(out.cover.sampleNearest(tree->x, tree->z).coverage, 0);

    const double trunk = 0.25 * 8.0 * static_cast<double>(tree->scale);
    int          ring = 0;
    int          outside = 0;
    int          flowers = 0;
    int          tufts = 0;
    for (const FoliageRecord& rec : out.records)
    {
        if (rec.kind == static_cast<uint8_t>(FoliageKind::Tree))
            continue;
        if (rec.kind == static_cast<uint8_t>(FoliageKind::Flower))
            ++flowers;
        else if (rec.kind == static_cast<uint8_t>(FoliageKind::Grass))
            ++tufts;
        const CoverSample hard = out.cover.sampleNearest(rec.x, rec.z);
        EXPECT_NE(hard.coverage, 0);
        const double dx = static_cast<double>(rec.x) - static_cast<double>(tree->x);
        const double dz = static_cast<double>(rec.z) - static_cast<double>(tree->z);
        EXPECT_GE(dx * dx + dz * dz, trunk * trunk - 1.0e-4);
        const CoverSample soft = out.cover.sampleBilinear(rec.x, rec.z);
        if (soft.coverage > 0 && soft.coverage < 255)
            ++ring;
        else if (soft.coverage == 255)
            ++outside;
    }
    EXPECT_GT(flowers, 0);
    EXPECT_GT(tufts, 0);
    EXPECT_GT(ring, 0);
    EXPECT_GT(outside, 0);

    int ringCells = 0;
    int outsideCells = 0;
    for (int z = 0; z < 32; ++z)
    {
        for (int x = 0; x < 32; ++x)
        {
            const float wx = (static_cast<float>(x) + 0.5f) * 0.25f;
            const float wz = (static_cast<float>(z) + 0.5f) * 0.25f;
            const CoverSample sample = out.cover.sampleBilinear(wx, wz);
            if (sample.coverage == 0)
                continue;
            if (sample.coverage < 255)
                ++ringCells;
            else
                ++outsideCells;
        }
    }
    ASSERT_GT(ringCells, 0);
    ASSERT_GT(outsideCells, 0);
    const double ringDensity = static_cast<double>(ring) / static_cast<double>(ringCells);
    const double outsideDensity = static_cast<double>(outside) / static_cast<double>(outsideCells);
    EXPECT_LT(ringDensity, outsideDensity);
}

TEST(FoliageSpawn, PatchMaskZeroStrengthMatchesBaseline)
{
    HeightMap hm;
    SplatMap  splat;
    ASSERT_TRUE(makeMap(hm, splat, 33, 33, 1.0f, 1.0f, 1.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));
    FoliageSpawnIn in = baseIn(hm, splat, 1, 1, 32);
    in.density.grassTreesPerM2   = 1.0f;
    in.density.grassFlowersPerM2 = 0.5f;
    in.density.grassPerM2        = 0.25f;
    in.density.treeSpacingM      = 3.0f;
    in.density.patchStrength     = 0.0f;
    in.density.seed              = 1337u;

    FoliageSpawnOut baseline;
    ASSERT_TRUE(spawnFoliage(in, baseline));
    EXPECT_GT(countKind(baseline.records, FoliageKind::Tree), 0);

    in.density.patchLargeM   = 60.0f;
    in.density.patchMediumM  = 30.0f;
    in.density.patchFineM    = 2.0f;
    in.largeMaskDisks[0]     = FoliageLargeMaskDisk{ 16.0f, 16.0f, 80.0f, 0.0f };
    FoliageSpawnOut masked;
    ASSERT_TRUE(spawnFoliage(in, masked));
    expectSameRecords(baseline, masked);

    for (size_t i = 0; i < baseline.records.size(); ++i)
    {
        if (baseline.records[i].kind != static_cast<uint8_t>(FoliageKind::Tree))
            continue;
        for (size_t j = i + 1; j < baseline.records.size(); ++j)
        {
            if (baseline.records[j].kind != static_cast<uint8_t>(FoliageKind::Tree))
                continue;
            const double dx  = static_cast<double>(baseline.records[i].x) - static_cast<double>(baseline.records[j].x);
            const double dz  = static_cast<double>(baseline.records[i].z) - static_cast<double>(baseline.records[j].z);
            const double lim = 3.0 * 0.5 * (static_cast<double>(baseline.records[i].scale) + static_cast<double>(baseline.records[j].scale));
            EXPECT_GE(std::sqrt(dx * dx + dz * dz), lim - 1.0e-3);
        }
    }

    FoliageSpawnIn treesOnly = in;
    treesOnly.density.grassFlowersPerM2 = 0.0f;
    treesOnly.density.grassPerM2        = 0.0f;
    treesOnly.density.patchStrength     = 0.0f;
    treesOnly.largeMaskDisks[0]         = FoliageLargeMaskDisk{};
    FoliageSpawnOut trees;
    ASSERT_TRUE(spawnFoliage(treesOnly, trees));
    EXPECT_GT(countKind(trees.records, FoliageKind::Tree), 0);
    EXPECT_EQ(countKind(trees.records, FoliageKind::Flower), 0);
    EXPECT_EQ(countKind(trees.records, FoliageKind::Grass), 0);

    HeightMap rockHm;
    SplatMap  rockSplat;
    ASSERT_TRUE(makeMap(rockHm, rockSplat, 7, 7, 1.0f, 1.0f, 1.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 0, 255, 0));
    FoliageSpawnIn rockIn = baseIn(rockHm, rockSplat, 1, 1, 6);
    rockIn.density.rockPerM2      = 1.0f;
    rockIn.density.rockSpacingM   = 16.0f;
    rockIn.density.patchStrength  = 0.0f;
    bool            foundLarge    = false;
    FoliageSpawnOut rocks;
    for (uint32_t seed = 1; seed < 48u && !foundLarge; ++seed)
    {
        rockIn.density.seed = seed;
        ASSERT_TRUE(spawnFoliage(rockIn, rocks));
        if (rocks.records.size() == 1u && rocks.records[0].scale >= 1.2f)
            foundLarge = true;
    }
    ASSERT_TRUE(foundLarge);
    EXPECT_EQ(countKind(rocks.records, FoliageKind::Rock), 1);
    EXPECT_EQ(countKind(rocks.records, FoliageKind::Flower), 0);
    EXPECT_EQ(countKind(rocks.records, FoliageKind::Grass), 0);
}

TEST(FoliageSpawn, ForestMeadowSplitIsSeedStable)
{
    HeightMap hm;
    SplatMap  splat;
    ASSERT_TRUE(makeMap(hm, splat, 65, 65, 1.0f, 1.0f, 1.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));
    FoliageSpawnIn in = baseIn(hm, splat, 1, 1, 64);
    in.density.grassTreesPerM2   = 1.0f;
    in.density.grassFlowersPerM2 = 1.0f;
    in.density.treeSpacingM      = 3.0f;
    in.density.patchStrength     = 1.0f;
    in.density.patchLargeM       = 60.0f;
    in.density.patchMediumM      = 15.0f;
    in.density.seed              = 1337u;

    FoliageSpawnOut first;
    FoliageSpawnOut second;
    ASSERT_TRUE(spawnFoliage(in, first));
    ASSERT_TRUE(spawnFoliage(in, second));
    expectSameRecords(first, second);
    EXPECT_GT(countKind(first.records, FoliageKind::Tree), 0);
    for (size_t i = 0; i < first.records.size(); ++i)
    {
        if (first.records[i].kind != static_cast<uint8_t>(FoliageKind::Tree))
            continue;
        for (size_t j = i + 1; j < first.records.size(); ++j)
        {
            if (first.records[j].kind != static_cast<uint8_t>(FoliageKind::Tree))
                continue;
            const double dx  = static_cast<double>(first.records[i].x) - static_cast<double>(first.records[j].x);
            const double dz  = static_cast<double>(first.records[i].z) - static_cast<double>(first.records[j].z);
            const double lim = 3.0 * 0.5 * (static_cast<double>(first.records[i].scale) + static_cast<double>(first.records[j].scale));
            EXPECT_GE(std::sqrt(dx * dx + dz * dz), lim - 1.0e-3);
        }
    }

    in.density.seed = 9001u;
    FoliageSpawnOut moved;
    ASSERT_TRUE(spawnFoliage(in, moved));
    bool treesMoved = countKind(first.records, FoliageKind::Tree) != countKind(moved.records, FoliageKind::Tree);
    if (!treesMoved)
    {
        size_t ia = 0;
        size_t ib = 0;
        while (ia < first.records.size() && ib < moved.records.size())
        {
            while (ia < first.records.size() && first.records[ia].kind != static_cast<uint8_t>(FoliageKind::Tree))
                ++ia;
            while (ib < moved.records.size() && moved.records[ib].kind != static_cast<uint8_t>(FoliageKind::Tree))
                ++ib;
            if (ia >= first.records.size() || ib >= moved.records.size())
                break;
            if (first.records[ia].x != moved.records[ib].x || first.records[ia].z != moved.records[ib].z)
            {
                treesMoved = true;
                break;
            }
            ++ia;
            ++ib;
        }
    }
    EXPECT_TRUE(treesMoved);

    HeightMap wide;
    SplatMap  wideSplat;
    ASSERT_TRUE(makeMap(wide, wideSplat, 181, 181, 1.0f, 1.0f, 1.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));
    FoliageSpawnIn forced = baseIn(wide, wideSplat, 1, 1, 180);
    forced.density.grassTreesPerM2   = 1.0f;
    forced.density.grassFlowersPerM2 = 2.0f;
    forced.density.treeSpacingM      = 3.0f;
    forced.density.patchStrength     = 1.0f;
    forced.density.seed              = 1337u;
    forced.largeMaskDisks[0]         = FoliageLargeMaskDisk{ 45.0f, 90.0f, 40.0f, 0.0f };
    forced.largeMaskDisks[1]         = FoliageLargeMaskDisk{ 135.0f, 90.0f, 40.0f, 1.0f };

    FoliageSpawnOut split;
    ASSERT_TRUE(spawnFoliage(forced, split));
    EXPECT_EQ(countKindInDisk(split.records, FoliageKind::Tree, 45.0f, 90.0f, 40.0f), 0);
    EXPECT_GT(countKindInDisk(split.records, FoliageKind::Tree, 135.0f, 90.0f, 40.0f), 0);
    const int meadowFlowers = countKindInDisk(split.records, FoliageKind::Flower, 45.0f, 90.0f, 32.0f);
    const int forestFlowers = countKindInDisk(split.records, FoliageKind::Flower, 135.0f, 90.0f, 32.0f);
    EXPECT_GT(meadowFlowers, forestFlowers);
}

TEST(FoliageSpawn, UnderstoryLandsInAnnulus)
{
    HeightMap hm;
    SplatMap  splat;
    ASSERT_TRUE(makeMap(hm, splat, 41, 41, 1.0f, 1.0f, 1.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));

    bool            placed = false;
    FoliageSpawnOut kept;
    FoliageRecord   tree{};
    for (uint32_t seed = 1; seed < 24u && !placed; ++seed)
    {
        FoliageSpawnIn in = baseIn(hm, splat, 1, 1, 40);
        in.density.grassTreesPerM2 = 1.0f;
        in.density.treeSpacingM    = 16.0f;
        in.density.patchStrength   = 1.0f;
        in.density.seed            = seed;
        in.largeMaskDisks[0]       = FoliageLargeMaskDisk{ 20.0f, 20.0f, 100.0f, 0.0f };
        in.largeMaskDisks[1]       = FoliageLargeMaskDisk{ 20.0f, 20.0f, 3.0f, 1.0f };
        in.treeExtentX             = 1.0f;
        in.treeExtentZ             = 1.0f;

        FoliageSpawnOut probe;
        ASSERT_TRUE(spawnFoliage(in, probe));
        if (countKind(probe.records, FoliageKind::Tree) != 1)
            continue;
        const FoliageRecord* probed = nullptr;
        for (const FoliageRecord& rec : probe.records)
        {
            if (rec.kind == static_cast<uint8_t>(FoliageKind::Tree))
                probed = &rec;
        }
        if (probed == nullptr || !(probed->scale > 0.0f))
            continue;
        const float extent = 3.0f / (0.45f * probed->scale);
        in.treeExtentX     = extent;
        in.treeExtentZ     = extent;
        ASSERT_TRUE(spawnFoliage(in, kept));
        if (countKind(kept.records, FoliageKind::Flower) + countKind(kept.records, FoliageKind::Grass) > 0)
        {
            for (const FoliageRecord& rec : kept.records)
            {
                if (rec.kind == static_cast<uint8_t>(FoliageKind::Tree))
                    tree = rec;
            }
            placed = true;
        }
    }
    ASSERT_TRUE(placed);
    EXPECT_EQ(countKind(kept.records, FoliageKind::Tree), 1);

    const double trunkR = 0.25 * (3.0 / (0.45 * static_cast<double>(tree.scale))) * static_cast<double>(tree.scale);
    EXPECT_NEAR(trunkR, 0.25 * 3.0 / 0.45, 1.0e-4);
    int satellites = 0;
    for (const FoliageRecord& rec : kept.records)
    {
        if (rec.kind == static_cast<uint8_t>(FoliageKind::Tree))
            continue;
        EXPECT_TRUE(rec.kind == static_cast<uint8_t>(FoliageKind::Flower) || rec.kind == static_cast<uint8_t>(FoliageKind::Grass));
        EXPECT_FLOAT_EQ(rec.pitch, 0.0f);
        EXPECT_FLOAT_EQ(rec.tiltYaw, 0.0f);
        expectPadAndYaw(rec);
        const double dx = static_cast<double>(rec.x) - static_cast<double>(tree.x);
        const double dz = static_cast<double>(rec.z) - static_cast<double>(tree.z);
        const double dist = std::sqrt(dx * dx + dz * dz);
        EXPECT_GE(dist, 1.8 - 1.0e-3);
        EXPECT_LE(dist, 3.9 + 1.0e-3);
        EXPECT_GE(dist, trunkR - 1.0e-4);
        EXPECT_NE(kept.cover.sampleNearest(rec.x, rec.z).coverage, 0);
        ++satellites;
    }
    EXPECT_GT(satellites, 0);

    HeightMap ramp;
    SplatMap  rampSplat;
    ASSERT_TRUE(fillRamp(ramp, rampSplat, 41, 1.0f, 1.0f, 0, 255, 0, 0));
    FoliageSpawnIn steep = baseIn(ramp, rampSplat, 1, 1, 40);
    steep.density.grassTreesPerM2 = 1.0f;
    steep.density.treeSpacingM    = 16.0f;
    steep.density.treeMinNormalY  = 0.0f;
    steep.density.patchStrength   = 1.0f;
    steep.density.seed            = 1337u;
    steep.seaLevel                = -1.0f;
    steep.largeMaskDisks[0]       = FoliageLargeMaskDisk{ 20.0f, 20.0f, 100.0f, 0.0f };
    steep.largeMaskDisks[1]       = FoliageLargeMaskDisk{ 20.0f, 20.0f, 3.0f, 1.0f };
    steep.treeExtentX             = 6.0f;
    steep.treeExtentZ             = 6.0f;
    FoliageSpawnOut rampOut;
    ASSERT_TRUE(spawnFoliage(steep, rampOut));
    EXPECT_GT(countKind(rampOut.records, FoliageKind::Tree), 0);
    EXPECT_EQ(countKind(rampOut.records, FoliageKind::Flower), 0);
    EXPECT_EQ(countKind(rampOut.records, FoliageKind::Grass), 0);
}

TEST(FoliageSpawn, PebblesFollowDownslope)
{
    HeightMap hm;
    SplatMap  rock;
    ASSERT_TRUE(fillRamp(hm, rock, 17, 1.0f, 0.25f, 0, 0, 255, 0));
    for (int z = 0; z < 17; ++z)
    {
        for (int x = 0; x < 17; ++x)
        {
            if (x < 6 || z < 6 || x > 12 || z > 12)
                rock.setTexel(x, z, 0, 255, 0, 0);
        }
    }
    FoliageSpawnIn in = baseIn(hm, rock, 1, 1, 16);
    in.density.rockPerM2     = 1.0f;
    in.density.rockSpacingM  = 16.0f;
    in.density.patchStrength = 1.0f;

    bool            found = false;
    FoliageSpawnOut out;
    for (uint32_t seed = 1; seed < 48u && !found; ++seed)
    {
        in.density.seed = seed;
        ASSERT_TRUE(spawnFoliage(in, out));
        int large  = 0;
        int extras = 0;
        for (const FoliageRecord& rec : out.records)
        {
            if (rec.kind != static_cast<uint8_t>(FoliageKind::Rock))
                continue;
            if (rec.scale >= 1.2f)
                ++large;
            else
                ++extras;
        }
        if (large == 1 && extras >= 2)
            found = true;
    }
    ASSERT_TRUE(found);

    const FoliageRecord* parent = nullptr;
    for (const FoliageRecord& rec : out.records)
    {
        if (rec.kind == static_cast<uint8_t>(FoliageKind::Rock) && rec.scale >= 1.2f)
            parent = &rec;
    }
    ASSERT_NE(parent, nullptr);
    const Dark::Math::Vector3f n = hm.normalAtWorld(parent->x, parent->z);
    int children = 0;
    for (const FoliageRecord& rec : out.records)
    {
        if (&rec == parent)
            continue;
        EXPECT_EQ(rec.kind, static_cast<uint8_t>(FoliageKind::Rock));
        EXPECT_LT(rec.scale, parent->scale);
        const double dx  = static_cast<double>(rec.x) - static_cast<double>(parent->x);
        const double dz  = static_cast<double>(rec.z) - static_cast<double>(parent->z);
        const double dot = dx * static_cast<double>(-n.x) + dz * static_cast<double>(-n.z);
        EXPECT_GT(dot, 0.0);
        EXPECT_LE(rec.pitch, 30.0f * Dark::Math::DegToRad + 1.0e-5f);
        expectPadAndYaw(rec);
        ++children;
    }
    EXPECT_GE(children, 2);
    EXPECT_LE(children, 5);

    in.density.patchStrength = 0.0f;
    FoliageSpawnOut quiet;
    ASSERT_TRUE(spawnFoliage(in, quiet));
    EXPECT_EQ(quiet.records.size(), 1u);
    EXPECT_GE(quiet.records[0].scale, 1.2f);
}
