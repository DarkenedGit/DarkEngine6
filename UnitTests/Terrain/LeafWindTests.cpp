#include <gtest/gtest.h>

#include "Math/MathHelper.h"
#include "Terrain/GrassWind.h"
#include "Terrain/LeafWind.h"

#include <cmath>

using namespace Dark::Terrain;

namespace
{
    bool sampleDiffers(const LeafWindSample& a, const LeafWindSample& b)
    {
        return a.strength != b.strength || a.dirX != b.dirX || a.dirZ != b.dirZ || a.yaw != b.yaw;
    }

    void expectUnitDirection(const LeafWindSample& sample)
    {
        EXPECT_TRUE(std::isfinite(sample.dirX));
        EXPECT_TRUE(std::isfinite(sample.dirZ));
        EXPECT_TRUE(std::isfinite(sample.yaw));
        EXPECT_TRUE(std::isfinite(sample.strength));
        EXPECT_GE(sample.strength, 0.0f);
        EXPECT_LE(sample.strength, 1.0f);
        EXPECT_NEAR(sample.dirX, std::sin(sample.yaw), 1.0e-5f);
        EXPECT_NEAR(sample.dirZ, std::cos(sample.yaw), 1.0e-5f);
        EXPECT_NEAR(sample.dirX * sample.dirX + sample.dirZ * sample.dirZ, 1.0f, 1.0e-5f);
    }
} // namespace

TEST(LeafWind, StrengthFromNoise_CalmIsZeroStrongIsOne)
{
    EXPECT_FLOAT_EQ(leafWindStrengthFromNoise(-1.0f), 0.0f);
    EXPECT_FLOAT_EQ(leafWindStrengthFromNoise(1.0f), 1.0f);
    // A calm-side noise must stay below the grass floor of 0.45.
    EXPECT_LT(leafWindStrengthFromNoise(-0.2f), 0.45f);
    EXPECT_GT(leafWindStrengthFromNoise(-0.2f), 0.0f);
}

TEST(LeafWind, Sample_RisesOverSeveralSeconds)
{
    const LeafWindSample early = sampleLeafWind(12.0f, 40.0f, 0.0);
    const LeafWindSample later = sampleLeafWind(12.0f, 40.0f, 8.0);
    expectUnitDirection(early);
    expectUnitDirection(later);
    EXPECT_TRUE(sampleDiffers(early, later));
}

TEST(LeafWind, Sample_DiffersTensOfMetresApart)
{
    const LeafWindSample here  = sampleLeafWind(0.0f, 0.0f, 3.0);
    const LeafWindSample there = sampleLeafWind(80.0f, 20.0f, 3.0);
    expectUnitDirection(here);
    expectUnitDirection(there);
    EXPECT_TRUE(sampleDiffers(here, there));
}

TEST(LeafWind, Sample_CanSitBelowGrassFloor)
{
    float lowest = 1.0f;
    for (int z = 0; z < 16; ++z)
    {
        for (int x = 0; x < 16; ++x)
        {
            const float          wx     = static_cast<float>(x) * 80.0f;
            const float          wz     = static_cast<float>(z) * 80.0f;
            const LeafWindSample sample = sampleLeafWind(wx, wz, 1.5);
            EXPECT_GE(sample.strength, 0.0f);
            EXPECT_LE(sample.strength, 1.0f);
            if (sample.strength < lowest)
                lowest = sample.strength;
        }
    }
    EXPECT_LT(lowest, 0.45f);
}

TEST(LeafWind, Sample_LeavesGrassWindUnchanged)
{
    GrassParams params;
    params.windSpatialFreq = 0.004f;
    params.windDeflection  = 0.55f;
    params.windBaseYaw     = 1.37f;
    params.seed            = 1337u;

    constexpr int     kCount = 8;
    GrassWindSample   before[kCount * kCount];
    GrassWindSample   after[kCount * kCount];
    constexpr double  kTime  = 4.5;

    for (int z = 0; z < kCount; ++z)
    {
        for (int x = 0; x < kCount; ++x)
        {
            const float cx = (static_cast<float>(x) + 0.5f) * 8.0f;
            const float cz = (static_cast<float>(z) + 0.5f) * 8.0f;
            before[z * kCount + x] = sampleGrassWind(cx, cz, kTime, params);
        }
    }

    for (int z = 0; z < 12; ++z)
    {
        for (int x = 0; x < 12; ++x)
            (void)sampleLeafWind(static_cast<float>(x) * 40.0f, static_cast<float>(z) * 40.0f, 0.0);
    }
    (void)sampleLeafWind(3.0f, -12.0f, 20.0);

    for (int z = 0; z < kCount; ++z)
    {
        for (int x = 0; x < kCount; ++x)
        {
            const float cx = (static_cast<float>(x) + 0.5f) * 8.0f;
            const float cz = (static_cast<float>(z) + 0.5f) * 8.0f;
            after[z * kCount + x] = sampleGrassWind(cx, cz, kTime, params);
        }
    }

    for (int i = 0; i < kCount * kCount; ++i)
    {
        EXPECT_FLOAT_EQ(before[i].strength, after[i].strength);
        EXPECT_FLOAT_EQ(before[i].dirX, after[i].dirX);
        EXPECT_FLOAT_EQ(before[i].dirZ, after[i].dirZ);
        EXPECT_FLOAT_EQ(before[i].yaw, after[i].yaw);
    }
}
