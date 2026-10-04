#include <gtest/gtest.h>

#include "Math/MathHelper.h"
#include "Terrain/FoliageSpawn.h"

#include <cmath>
#include <cstdint>
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
    expectCleared(out);

    in         = baseIn(hm, splat, 4, 2, 1);
    in.splat   = nullptr;
    out.records.push_back(FoliageRecord{});
    EXPECT_FALSE(spawnFoliage(in, out));
    expectCleared(out);

    HeightMap invalid;
    in        = baseIn(hm, splat, 4, 2, 1);
    in.height = &invalid;
    EXPECT_FALSE(spawnFoliage(in, out));
    expectCleared(out);

    SplatMap invalidSplat;
    in       = baseIn(hm, splat, 4, 2, 1);
    in.splat = &invalidSplat;
    EXPECT_FALSE(spawnFoliage(in, out));
    expectCleared(out);

    in           = baseIn(hm, splat, 4, 2, 1);
    in.tileCells = 0;
    EXPECT_FALSE(spawnFoliage(in, out));
    expectCleared(out);

    in        = baseIn(hm, splat, 4, 2, 1);
    in.tilesX = 0;
    EXPECT_FALSE(spawnFoliage(in, out));
    expectCleared(out);

    in        = baseIn(hm, splat, 4, 2, 1);
    in.tilesZ = 0;
    EXPECT_FALSE(spawnFoliage(in, out));
    expectCleared(out);

    in           = baseIn(hm, splat, 4, 2, 1);
    in.cellSize  = 0.0f;
    EXPECT_FALSE(spawnFoliage(in, out));
    expectCleared(out);

    in          = baseIn(hm, splat, 4, 2, 1);
    in.cellSize = 2.0f;
    EXPECT_FALSE(spawnFoliage(in, out));
    expectCleared(out);

    in            = baseIn(hm, splat, 4, 2, 1);
    in.origin.y   = 3.0f;
    EXPECT_FALSE(spawnFoliage(in, out));
    expectCleared(out);

    in           = baseIn(hm, splat, 1, 1, 3);
    EXPECT_FALSE(spawnFoliage(in, out));
    expectCleared(out);

    in        = baseIn(hm, splat, 4, 1, 1);
    EXPECT_FALSE(spawnFoliage(in, out));
    expectCleared(out);

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
    ASSERT_EQ(out.records.size(), 2u);
    EXPECT_EQ(out.accepted, 2u);
    EXPECT_EQ(out.kept, 2u);
    EXPECT_FALSE(out.capped);
    EXPECT_EQ(out.records[0].kind, static_cast<uint8_t>(FoliageKind::Tree));
    EXPECT_EQ(out.records[1].kind, static_cast<uint8_t>(FoliageKind::Flower));
    EXPECT_FLOAT_EQ(out.records[0].pitch, 0.0f);
    EXPECT_FLOAT_EQ(out.records[0].tiltYaw, 0.0f);
    EXPECT_FLOAT_EQ(out.records[1].pitch, 0.0f);
    EXPECT_FLOAT_EQ(out.records[1].tiltYaw, 0.0f);
    EXPECT_FLOAT_EQ(out.records[0].y, 4.0f);
    EXPECT_FLOAT_EQ(out.records[1].y, 4.0f);
    EXPECT_GE(out.records[0].scale, 0.85f);
    EXPECT_LE(out.records[0].scale, 1.15f);
    EXPECT_GE(out.records[1].scale, 0.80f);
    EXPECT_LE(out.records[1].scale, 1.20f);
    EXPECT_NEAR(out.records[0].x, 0.5f, 0.45f);
    EXPECT_NEAR(out.records[0].z, 0.5f, 0.45f);
    EXPECT_NEAR(out.records[1].x, 0.25f, 0.12f);
    EXPECT_NEAR(out.records[1].z, 0.5f, 0.12f);
    EXPECT_LT(out.records[1].x, 0.5f);
    expectPadAndYaw(out.records[0]);
    expectPadAndYaw(out.records[1]);
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
    ASSERT_EQ(out.records.size(), 3u);
    EXPECT_EQ(out.records[0].kind, static_cast<uint8_t>(FoliageKind::Tree));
    EXPECT_EQ(out.records[1].kind, static_cast<uint8_t>(FoliageKind::Flower));
    EXPECT_EQ(out.records[2].kind, static_cast<uint8_t>(FoliageKind::Flower));
    EXPECT_EQ(out.accepted, 3u);
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
    ASSERT_EQ(a.records.size(), b.records.size());
    EXPECT_GT(a.records.size(), 0u);
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

TEST(FoliageSpawn, LatticeIsOneMetreNotSamples)
{
    HeightMap hm;
    SplatMap  splat;
    ASSERT_TRUE(makeMap(hm, splat, 129, 129, 2.0f, 1.0f, 1.0f, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 255, 0, 0));
    FoliageSpawnIn in = baseIn(hm, splat, 1, 1, 128);
    in.density.grassTreesPerM2 = 1.0f;

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
    expectCleared(out);

    in.tileCells = 0;
    EXPECT_FALSE(spawnFoliage(in, out));
    expectCleared(out);

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

TEST(FoliageSpawn, TreesStayUprightRocksFollowNormal)
{
    HeightMap hm;
    ASSERT_TRUE(hm.createWorking(4, 4, 1.0f, 1.0f));
    hm.setOrigin(Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f });
    for (uint32_t z = 0; z < 4; ++z)
    {
        for (uint32_t x = 0; x < 4; ++x)
            hm.setHeight(static_cast<int>(x), static_cast<int>(z), static_cast<float>(x));
    }
    SplatMap grass;
    ASSERT_TRUE(grass.createWorking(4, 4));
    for (uint32_t z = 0; z < 4; ++z)
    {
        for (uint32_t x = 0; x < 4; ++x)
            grass.setTexel(static_cast<int>(x), static_cast<int>(z), 0, 255, 0, 0);
    }
    FoliageSpawnIn in = baseIn(hm, grass, 1, 1, 3);
    in.density.grassTreesPerM2 = 1.0f;
    in.seaLevel                = -1.0f;

    FoliageSpawnOut out;
    ASSERT_TRUE(spawnFoliage(in, out));
    ASSERT_EQ(out.records.size(), 9u);
    for (const FoliageRecord& rec : out.records)
    {
        EXPECT_EQ(rec.kind, static_cast<uint8_t>(FoliageKind::Tree));
        EXPECT_FLOAT_EQ(rec.pitch, 0.0f);
        EXPECT_FLOAT_EQ(rec.tiltYaw, 0.0f);
        EXPECT_FLOAT_EQ(rec.y, hm.heightAtWorld(rec.x, rec.z));
        expectPadAndYaw(rec);
    }

    SplatMap rock;
    ASSERT_TRUE(rock.createWorking(4, 4));
    for (uint32_t z = 0; z < 4; ++z)
    {
        for (uint32_t x = 0; x < 4; ++x)
            rock.setTexel(static_cast<int>(x), static_cast<int>(z), 0, 0, 255, 0);
    }
    in = baseIn(hm, rock, 1, 1, 3);
    in.density.grassTreesPerM2   = 1.0f;
    in.density.grassFlowersPerM2 = 2.0f;
    in.density.rockPerM2         = 1.0f;
    ASSERT_TRUE(spawnFoliage(in, out));
    ASSERT_EQ(out.records.size(), 9u);
    for (const FoliageRecord& rec : out.records)
    {
        EXPECT_EQ(rec.kind, static_cast<uint8_t>(FoliageKind::Rock));
        const Dark::Math::Vector3f n = hm.normalAtWorld(rec.x, rec.z);
        ASSERT_FALSE(n.y > 0.999f);
        const float ny = Dark::Math::Clamp(n.y, -1.0f, 1.0f);
        EXPECT_NEAR(rec.pitch, std::acos(ny), 1.0e-5f);
        EXPECT_NEAR(rec.tiltYaw, std::atan2(n.x, n.z), 1.0e-5f);
        EXPECT_FLOAT_EQ(rec.y, hm.heightAtWorld(rec.x, rec.z));
        EXPECT_GE(rec.scale, 0.60f);
        EXPECT_LE(rec.scale, 1.80f);
        expectPadAndYaw(rec);
    }
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
    EXPECT_EQ(probe.countRows, 3);
    EXPECT_EQ(probe.keepRows, 3);
    EXPECT_FLOAT_EQ(probe.maxT, 1.0f);
    EXPECT_EQ(out.records.size(), 9u);

    probe                 = {};
    probe.cancelAfterKeep = 1;
    out.records.push_back(FoliageRecord{});
    EXPECT_FALSE(spawnFoliage(in, out));
    expectCleared(out);
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
    in.density.grassTreesPerM2   = 1.0f;
    in.density.grassFlowersPerM2 = 2.0f;
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
        else if (rec.kind != static_cast<uint8_t>(FoliageKind::Tree) && rec.kind != static_cast<uint8_t>(FoliageKind::Flower))
            ++other;
    }
    EXPECT_TRUE(low);
    EXPECT_TRUE(high);
    EXPECT_GT(maxZ, 2048.0f);
    EXPECT_EQ(rocks, 0u);
    EXPECT_EQ(other, 0u);
}
