#include <gtest/gtest.h>

#include "Math/MathHelper.h"
#include "Terrain/GrassWind.h"
#include "Terrain/TerrainGen.h"
#include "Terrain/WindField.h"

#include <cmath>
#include <limits>

using namespace Dark::Terrain;

TEST(WindField, Hash21MatchesTerrainGen)
{
    const struct
    {
        int      x;
        int      z;
        uint32_t seed;
        float    expected;
    } cases[] = {
        { 0, 0, 1337u, 0.22453856f },
        { 10, 20, 1337u, 0.18630618f },
        { -5, 15, 1337u, 0.65315211f },
        { 100, -100, 1337u, 0.90149969f },
        { 42, 42, 42u, 0.42486668f },
        { -1, -1, 0u, 0.58032829f },
        { 7, 13, 9999u, 0.55397779f },
        { -50, 50, 12345u, 0.83529061f },
    };

    for (const auto& test : cases)
        EXPECT_NEAR(hash21(test.x, test.z, test.seed), test.expected, 1.0e-6f) << test.x << "," << test.z;
}

TEST(WindField, SampleWindMatchesGrassField)
{
    const WindSettings wind = defaultWindSettings();
    const GrassWindSample fromWind = sampleWind(12.0f, -4.0f, 3.5, wind);

    GrassParams params;
    params.windBaseYaw      = wind.headingRad;
    params.windDeflection   = wind.deflection;
    params.windSpatialFreq  = wind.spatialFreq;
    params.windTemporalFreq = wind.temporalFreq;
    params.seed             = wind.seed;
    params.windTipMetres    = 1.0f;
    const GrassWindSample fromGrass = sampleGrassWind(12.0f, -4.0f, 3.5, params);

    EXPECT_NEAR(fromWind.dirX, fromGrass.dirX, 1.0e-5f);
    EXPECT_NEAR(fromWind.dirZ, fromGrass.dirZ, 1.0e-5f);
    EXPECT_NEAR(fromWind.strength, fromGrass.strength, 1.0e-5f);
    EXPECT_NEAR(fromWind.yaw, fromGrass.yaw, 1.0e-5f);
}

TEST(WindField, GustStaysInRange)
{
    WindSettings wind = defaultWindSettings();
    EXPECT_FLOAT_EQ(gustAt(4.0, wind), gustAt(4.0, wind));
    wind.gustFreq = 0.0f;
    EXPECT_FLOAT_EQ(gustAt(4.0, wind), 1.0f);

    wind                 = defaultWindSettings();
    wind.gustStrength    = 1.0f;
    wind.gustFreq        = 0.5f;
    const float gustCeil = 1.0f + wind.gustStrength;
    for (double t = 0.0; t < 40.0; t += 0.5)
    {
        const float gust = gustAt(t, wind);
        EXPECT_GE(gust, 0.0f);
        EXPECT_LE(gust, gustCeil + 1.0e-4f);
    }
}

TEST(WindField, ClampRepairsNanBeforeFadeSpan)
{
    WindSettings wind          = defaultWindSettings();
    wind.headingRad            = std::numeric_limits<float>::quiet_NaN();
    wind.kind[0].bendFadeStart = 400.0f;
    wind.kind[0].bendFadeEnd   = std::numeric_limits<float>::quiet_NaN();
    clampWindSettings(wind);

    EXPECT_TRUE(std::isfinite(wind.headingRad));
    EXPECT_GT(wind.headingRad, -Dark::Math::Pi);
    EXPECT_LE(wind.headingRad, Dark::Math::Pi);
    EXPECT_GE(wind.kind[0].bendFadeEnd, wind.kind[0].bendFadeStart + 0.5f);
}

TEST(WindField, SwayFollowsBendAndDropsWhenStill)
{
    WindSettings wind = defaultWindSettings();
    clampWindSettings(wind);
    const int tree             = static_cast<int>(WindKind::Tree);
    const int rock             = static_cast<int>(WindKind::Rock);
    const float treeSway       = kindSwayMetres(wind, tree);
    EXPECT_GT(treeSway, 0.4f);
    EXPECT_LT(treeSway, 1.0f);
    EXPECT_FLOAT_EQ(kindSwayMetres(wind, rock), 0.0f);

    wind.kind[tree].influence = 0.0f;
    EXPECT_FLOAT_EQ(kindSwayMetres(wind, tree), 0.0f);

    wind                          = defaultWindSettings();
    wind.kind[tree].mainBendMetres = 1.0f;
    clampWindSettings(wind);
    EXPECT_GT(kindSwayMetres(wind, tree), treeSway);
}

TEST(WindField, GrassFollowsStrengthScalesTipOnce)
{
    WindSettings wind          = defaultWindSettings();
    wind.strength              = 2.0f;
    wind.grassFollowsStrength  = true;
    GrassParams params;
    params.windTipMetres = 0.40f;
    applyWindToGrass(wind, params);
    EXPECT_NEAR(params.windTipMetres, 0.80f, 1.0e-5f);
    EXPECT_NEAR(params.windBaseYaw, wind.headingRad, 1.0e-5f);

    wind.grassFollowsStrength = false;
    params.windTipMetres      = 0.40f;
    applyWindToGrass(wind, params);
    EXPECT_NEAR(params.windTipMetres, 0.40f, 1.0e-5f);
}
