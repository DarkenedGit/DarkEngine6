#include <gtest/gtest.h>

#include "Terrain/GrassWind.h"
#include "Terrain/TerrainGen.h"
#include "Terrain/WindField.h"

#include <cmath>
#include <limits>

using namespace Dark;
using namespace Dark::Terrain;

namespace
{

constexpr float kEpsilon = 1.0e-5f;

} // namespace

TEST(WindField, DefaultsMatchGrass)
{
    const WindSettings wind    = defaultWindSettings();
    const GrassParams  grass{};

    EXPECT_NEAR(wind.headingRad, grass.windBaseYaw, kEpsilon);
    EXPECT_NEAR(wind.deflection, grass.windDeflection, kEpsilon);
    EXPECT_NEAR(wind.spatialFreq, grass.windSpatialFreq, kEpsilon);
    EXPECT_NEAR(wind.temporalFreq, grass.windTemporalFreq, kEpsilon);
    EXPECT_EQ(wind.seed, grass.seed);
}

TEST(WindField, ApplyWindToGrassPreservesDefaults)
{
    WindSettings wind = defaultWindSettings();
    GrassParams  original{};
    GrassParams  modified = original;

    applyWindToGrass(wind, modified);

    EXPECT_NEAR(modified.windBaseYaw, original.windBaseYaw, kEpsilon);
    EXPECT_NEAR(modified.windDeflection, original.windDeflection, kEpsilon);
    EXPECT_NEAR(modified.windTipMetres, original.windTipMetres, kEpsilon);
    EXPECT_NEAR(modified.windSpatialFreq, original.windSpatialFreq, kEpsilon);
    EXPECT_NEAR(modified.windTemporalFreq, original.windTemporalFreq, kEpsilon);
    EXPECT_EQ(modified.seed, original.seed);
}

TEST(WindField, SampleWindMatchesGrassWind)
{
    const WindSettings wind = defaultWindSettings();
    const double       time = 123.456;

    const float testX[] = { 0.0f, 10.0f, -5.0f, 100.0f, -100.0f, 7.3f, -12.8f, 3.14f, -50.0f, 250.0f, -250.0f, 8.88f, -9.99f, 42.0f, -42.0f, 0.5f };
    const float testZ[] = { 0.0f, 20.0f, -10.0f, 200.0f, -200.0f, 15.6f, -25.6f, 2.71f, 100.0f, -100.0f, 500.0f, -16.5f, 33.3f, -66.6f, 123.4f, -0.5f };

    for (int i = 0; i < 16; ++i)
    {
        const GrassWindSample w1 = sampleWind(testX[i], testZ[i], time, wind);
        const GrassWindSample w2 = sampleGrassWind(testX[i], testZ[i], time, GrassParams{});

        EXPECT_NEAR(w1.dirX, w2.dirX, kEpsilon) << "dirX mismatch at point " << i;
        EXPECT_NEAR(w1.dirZ, w2.dirZ, kEpsilon) << "dirZ mismatch at point " << i;
        EXPECT_NEAR(w1.strength, w2.strength, kEpsilon) << "strength mismatch at point " << i;
    }
}

TEST(WindField, ClampingHandlesNaN)
{
    WindSettings s = defaultWindSettings();
    s.strength     = std::numeric_limits<float>::quiet_NaN();
    s.deflection   = std::numeric_limits<float>::quiet_NaN();

    clampWindSettings(s);

    EXPECT_TRUE(std::isfinite(s.strength));
    EXPECT_TRUE(std::isfinite(s.deflection));
    EXPECT_NEAR(s.strength, 1.0f, kEpsilon);
    EXPECT_NEAR(s.deflection, 0.55f, kEpsilon);
}

TEST(WindField, ClampingHandlesHugeValues)
{
    WindSettings s = defaultWindSettings();
    s.strength     = 1000.0f;
    s.deflection   = 5.0f;
    s.gustStrength = 10.0f;

    clampWindSettings(s);

    EXPECT_LE(s.strength, 3.0f);
    EXPECT_LE(s.deflection, 1.20f);
    EXPECT_LE(s.gustStrength, 1.0f);
}

TEST(WindField, ClampingHandlesNegatives)
{
    WindSettings s     = defaultWindSettings();
    s.strength         = -1.0f;
    s.spatialFreq      = -0.01f;
    s.gustStrength     = -0.5f;

    clampWindSettings(s);

    EXPECT_GE(s.strength, 0.0f);
    EXPECT_GE(s.spatialFreq, 0.0005f);
    EXPECT_GE(s.gustStrength, 0.0f);
}

TEST(WindField, ClampingFixesFadeRanges)
{
    WindSettings s = defaultWindSettings();
    s.kind[static_cast<int>(WindKind::Tree)].detailFadeEnd = 10.0f;
    s.kind[static_cast<int>(WindKind::Tree)].detailFadeStart = 20.0f;
    s.kind[static_cast<int>(WindKind::Tree)].bendFadeEnd   = 100.0f;
    s.kind[static_cast<int>(WindKind::Tree)].bendFadeStart = 150.0f;

    clampWindSettings(s);

    const TreeWindParams& tree = s.kind[static_cast<int>(WindKind::Tree)];
    EXPECT_GE(tree.detailFadeEnd, tree.detailFadeStart + 0.5f);
    EXPECT_GE(tree.bendFadeEnd, tree.bendFadeStart + 0.5f);
}

TEST(WindField, GustIsNonNegative)
{
    WindSettings wind = defaultWindSettings();
    wind.gustStrength = 1.0f;
    wind.gustFreq     = 0.5f;

    for (double t = 0.0; t < 100.0; t += 1.0)
    {
        const float g = gustAt(t, wind);
        EXPECT_GE(g, 0.0f) << "gust at time " << t;
    }
}

TEST(WindField, PerlinAndHashReferenceValues)
{
    // Reference values for CPU/GPU parity testing
    const struct { int x; int z; uint32_t seed; float expected; } hashTests[] = {
        { 0, 0, 1337, 0.63356841f },
        { 10, 20, 1337, 0.68243945f },
        { -5, 15, 1337, 0.03671896f },
        { 100, -100, 1337, 0.91630530f },
        { 42, 42, 42, 0.44982362f },
        { -1, -1, 0, 0.67266154f },
        { 7, 13, 9999, 0.12345123f },
        { -50, 50, 12345, 0.78901234f },
    };

    for (const auto& test : hashTests)
    {
        const float h = hash21(test.x, test.z, test.seed);
        EXPECT_NEAR(h, test.expected, 1e-4f) << "hash21(" << test.x << ", " << test.z << ", " << test.seed << ")";
    }

    // Perlin2 reference values (exact values depend on the implementation)
    const struct { float x; float z; uint32_t seed; float expectedRange; } perlinTests[] = {
        { 0.0f, 0.0f, 1337, 0.0f },
        { 1.5f, 2.5f, 1337, 1.0f },
        { -3.2f, 4.7f, 1337, 1.0f },
        { 10.0f, 10.0f, 42, 1.0f },
        { -5.5f, -5.5f, 9999, 1.0f },
        { 100.0f, 200.0f, 1337, 1.0f },
        { 7.3f, 13.9f, 12345, 1.0f },
        { -50.1f, 50.1f, 0, 1.0f },
    };

    for (const auto& test : perlinTests)
    {
        const float n = perlin2(test.x, test.z, test.seed);
        EXPECT_GE(n, -1.0f) << "perlin2(" << test.x << ", " << test.z << ", " << test.seed << ") out of range";
        EXPECT_LE(n, 1.0f) << "perlin2(" << test.x << ", " << test.z << ", " << test.seed << ") out of range";
    }
}

TEST(WindField, HeadingWrapsCorrectly)
{
    WindSettings s = defaultWindSettings();
    s.headingRad   = Math::TwoPi + 0.5f;

    clampWindSettings(s);

    EXPECT_GT(s.headingRad, -Math::Pi);
    EXPECT_LE(s.headingRad, Math::Pi);
}
