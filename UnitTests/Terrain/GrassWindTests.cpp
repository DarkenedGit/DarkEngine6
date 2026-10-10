#include <gtest/gtest.h>

#include "Math/MathHelper.h"
#include "Terrain/GrassWind.h"

#include <cmath>
#include <cstdint>

using namespace Dark::Terrain;

namespace
{
    constexpr int   kPatchTiles = 32;
    constexpr float kTileMetres = 8.0f;

    GrassParams patchParams()
    {
        GrassParams params;
        params.windSpatialFreq = 0.004f;
        params.windDeflection  = 0.55f;
        params.windBaseYaw     = 1.37f;
        params.seed            = 1337u;
        return params;
    }

    float tileCenter(int tile)
    {
        return (static_cast<float>(tile) + 0.5f) * kTileMetres;
    }

    float wrapYaw(float yaw)
    {
        GrassParams params;
        params.windBaseYaw = yaw;
        clampGrassParams(params);
        return params.windBaseYaw;
    }
} // namespace

TEST(GrassWind, Perlin2_DeterministicFiniteInRange)
{
    float minN   = 1.0f;
    float maxN   = -1.0f;
    int   finite = 0;
    for (int z = -6; z <= 6; ++z)
    {
        for (int x = -6; x <= 6; ++x)
        {
            const float px = static_cast<float>(x) * 0.37f - 1.25f;
            const float pz = static_cast<float>(z) * 0.53f + 0.4f;
            const float a  = perlin2(px, pz, 1337u);
            const float b  = perlin2(px, pz, 1337u);
            EXPECT_FLOAT_EQ(a, b);
            EXPECT_TRUE(std::isfinite(a));
            EXPECT_GE(a, -1.0f);
            EXPECT_LE(a, 1.0f);
            if (a < minN)
                minN = a;
            if (a > maxN)
                maxN = a;
            ++finite;
        }
    }
    EXPECT_GT(finite, 0);
    EXPECT_GT(maxN - minN, 0.05f);

    const float neg = perlin2(-4.2f, -9.5f, 42u);
    EXPECT_FLOAT_EQ(neg, perlin2(-4.2f, -9.5f, 42u));
    EXPECT_TRUE(std::isfinite(neg));
    EXPECT_GE(neg, -1.0f);
    EXPECT_LE(neg, 1.0f);
    EXPECT_NE(perlin2(-4.2f, -9.5f, 42u), perlin2(-4.2f, -9.5f, 43u));

    EXPECT_FLOAT_EQ(perlin2(0.0f, 0.0f, 1337u), 0.0f);
    EXPECT_FLOAT_EQ(perlin2(-3.0f, 4.0f, 42u), 0.0f);
    EXPECT_FLOAT_EQ(perlin2(2.0f, -5.0f, 1u), 0.0f);
}

TEST(GrassWind, Patch_CoherenceRangeAndStrength)
{
    const GrassParams params = patchParams();
    GrassWindSample   samples[kPatchTiles * kPatchTiles];
    for (int z = 0; z < kPatchTiles; ++z)
    {
        for (int x = 0; x < kPatchTiles; ++x)
            samples[z * kPatchTiles + x] = sampleGrassWind(tileCenter(x), tileCenter(z), 1.25, params);
    }

    const float yawLo = params.windBaseYaw - params.windDeflection - 1.0e-3f;
    const float yawHi = params.windBaseYaw + params.windDeflection + 1.0e-3f;
    for (int z = 0; z < kPatchTiles; ++z)
    {
        for (int x = 0; x < kPatchTiles; ++x)
        {
            const GrassWindSample& s = samples[z * kPatchTiles + x];
            EXPECT_TRUE(std::isfinite(s.yaw));
            EXPECT_TRUE(std::isfinite(s.strength));
            EXPECT_TRUE(std::isfinite(s.dirX));
            EXPECT_TRUE(std::isfinite(s.dirZ));
            EXPECT_GE(s.yaw, yawLo);
            EXPECT_LE(s.yaw, yawHi);
            EXPECT_GE(s.strength, 0.45f);
            EXPECT_LE(s.strength, 1.0f);
            EXPECT_NEAR(s.dirX, std::sin(s.yaw), 1.0e-5f);
            EXPECT_NEAR(s.dirZ, std::cos(s.yaw), 1.0e-5f);
            EXPECT_NEAR(s.dirX * s.dirX + s.dirZ * s.dirZ, 1.0f, 1.0e-5f);

            if (x + 1 < kPatchTiles)
            {
                const float dy = std::fabs(s.yaw - samples[z * kPatchTiles + (x + 1)].yaw);
                EXPECT_LE(dy, 0.20f) << x << "," << z;
                EXPECT_LE(dy, Dark::Math::HalfPi) << x << "," << z;
            }
            if (z + 1 < kPatchTiles)
            {
                const float dy = std::fabs(s.yaw - samples[(z + 1) * kPatchTiles + x].yaw);
                EXPECT_LE(dy, 0.20f) << x << "," << z;
                EXPECT_LE(dy, Dark::Math::HalfPi) << x << "," << z;
            }
        }
    }

    EXPECT_FLOAT_EQ(tileCenter(0), 4.0f);
}

TEST(GrassWind, TimeStep_StaysSmall)
{
    const GrassParams params = patchParams();
    for (int z = 0; z < kPatchTiles; ++z)
    {
        for (int x = 0; x < kPatchTiles; ++x)
        {
            const float           cx = tileCenter(x);
            const float           cz = tileCenter(z);
            const GrassWindSample a  = sampleGrassWind(cx, cz, 3.0, params);
            const GrassWindSample b  = sampleGrassWind(cx, cz, 3.10, params);
            EXPECT_LT(std::fabs(a.yaw - b.yaw), 0.10f) << x << "," << z;
            EXPECT_GE(a.strength, 0.45f);
            EXPECT_LE(a.strength, 1.0f);
            EXPECT_GE(b.strength, 0.45f);
            EXPECT_LE(b.strength, 1.0f);
        }
    }
}

TEST(GrassWind, ZeroTemporalFreq_IgnoresTime)
{
    GrassParams params         = patchParams();
    params.windTemporalFreq    = 0.0f;
    const GrassWindSample at0  = sampleGrassWind(-20.0f, 36.0f, 0.0, params);
    const GrassWindSample at10 = sampleGrassWind(-20.0f, 36.0f, 10.0, params);
    EXPECT_FLOAT_EQ(at0.yaw, at10.yaw);
    EXPECT_FLOAT_EQ(at0.strength, at10.strength);
    EXPECT_FLOAT_EQ(at0.dirX, at10.dirX);
    EXPECT_FLOAT_EQ(at0.dirZ, at10.dirZ);
    EXPECT_GE(at0.strength, 0.45f);
    EXPECT_LE(at0.strength, 1.0f);

    GrassParams scrolling       = patchParams();
    scrolling.windTemporalFreq  = 0.10f;
    const GrassWindSample t0    = sampleGrassWind(4.0f, 12.0f, 0.0, scrolling);
    bool                  moved = false;
    for (int i = 1; i <= 40; ++i)
    {
        const GrassWindSample later = sampleGrassWind(4.0f, 12.0f, static_cast<double>(i) * 25.0, scrolling);
        if (later.yaw != t0.yaw || later.strength != t0.strength)
        {
            moved = true;
            break;
        }
    }
    EXPECT_TRUE(moved);
}

TEST(GrassWind, Sample_ClampsParams)
{
    GrassParams wild      = patchParams();
    wild.windBaseYaw      = 4.0f;
    wild.windDeflection   = 8.0f;
    wild.windSpatialFreq  = 1.0f;
    wild.windTemporalFreq = 4.0f;
    wild.windTipMetres    = 20.0f;
    wild.heightMetres     = -3.0f;

    GrassParams clamped = wild;
    clampGrassParams(clamped);
    const GrassWindSample fromWild    = sampleGrassWind(12.0f, 28.0f, 4.5, wild);
    const GrassWindSample fromClamped = sampleGrassWind(12.0f, 28.0f, 4.5, clamped);
    EXPECT_FLOAT_EQ(fromWild.yaw, fromClamped.yaw);
    EXPECT_FLOAT_EQ(fromWild.strength, fromClamped.strength);
    EXPECT_FLOAT_EQ(fromWild.dirX, fromClamped.dirX);
    EXPECT_FLOAT_EQ(fromWild.dirZ, fromClamped.dirZ);
    EXPECT_GE(fromWild.strength, 0.45f);
    EXPECT_LE(fromWild.strength, 1.0f);
    EXPECT_GE(fromWild.yaw, clamped.windBaseYaw - clamped.windDeflection - 1.0e-3f);
    EXPECT_LE(fromWild.yaw, clamped.windBaseYaw + clamped.windDeflection + 1.0e-3f);

    GrassParams shortTip    = patchParams();
    GrassParams longTip     = patchParams();
    shortTip.windTipMetres  = 0.0f;
    longTip.windTipMetres   = 1.50f;
    const GrassWindSample a = sampleGrassWind(12.0f, 20.0f, 1.5, shortTip);
    const GrassWindSample b = sampleGrassWind(12.0f, 20.0f, 1.5, longTip);
    EXPECT_FLOAT_EQ(a.yaw, b.yaw);
    EXPECT_FLOAT_EQ(a.strength, b.strength);
    EXPECT_FLOAT_EQ(a.dirX, b.dirX);
    EXPECT_FLOAT_EQ(a.dirZ, b.dirZ);
}

TEST(GrassWind, GrassParams_Clamp)
{
    GrassParams low;
    low.heightMetres     = -1.0f;
    low.flexibility      = -0.25f;
    low.densityScale     = 3.0f;
    low.windDeflection   = 2.0f;
    low.windTipMetres    = -0.5f;
    low.windSpatialFreq  = 0.0001f;
    low.windTemporalFreq = 2.0f;
    low.windBaseYaw      = Dark::Math::Pi + 0.5f;
    low.seed             = 0xFFFFFFFFu;
    low.enabled          = true;
    clampGrassParams(low);
    EXPECT_FLOAT_EQ(low.heightMetres, 0.05f);
    EXPECT_FLOAT_EQ(low.flexibility, 0.0f);
    EXPECT_FLOAT_EQ(low.densityScale, 1.5f);
    EXPECT_FLOAT_EQ(low.windDeflection, 1.20f);
    EXPECT_FLOAT_EQ(low.windTipMetres, 0.0f);
    EXPECT_FLOAT_EQ(low.windSpatialFreq, 0.0005f);
    EXPECT_FLOAT_EQ(low.windTemporalFreq, 1.0f);
    EXPECT_GT(low.windBaseYaw, -Dark::Math::Pi);
    EXPECT_LE(low.windBaseYaw, Dark::Math::Pi);
    EXPECT_NEAR(low.windBaseYaw, -Dark::Math::Pi + 0.5f, 1.0e-5f);
    EXPECT_EQ(low.seed, 0xFFFFFFFFu);
    EXPECT_TRUE(low.enabled);

    GrassParams high;
    high.heightMetres     = 4.0f;
    high.flexibility      = 1.5f;
    high.densityScale     = -1.0f;
    high.windDeflection   = -0.2f;
    high.windTipMetres    = 3.0f;
    high.windSpatialFreq  = 1.0f;
    high.windTemporalFreq = -0.2f;
    high.windBaseYaw      = -Dark::Math::Pi;
    clampGrassParams(high);
    EXPECT_FLOAT_EQ(high.heightMetres, 1.50f);
    EXPECT_FLOAT_EQ(high.flexibility, 1.0f);
    EXPECT_FLOAT_EQ(high.densityScale, 0.0f);
    EXPECT_FLOAT_EQ(high.windDeflection, 0.0f);
    EXPECT_FLOAT_EQ(high.windTipMetres, 1.50f);
    EXPECT_FLOAT_EQ(high.windSpatialFreq, 0.02f);
    EXPECT_FLOAT_EQ(high.windTemporalFreq, 0.0f);
    EXPECT_GT(high.windBaseYaw, -Dark::Math::Pi);
    EXPECT_LE(high.windBaseYaw, Dark::Math::Pi);
    EXPECT_FLOAT_EQ(high.windBaseYaw, Dark::Math::Pi);

    GrassParams edge;
    edge.heightMetres     = 0.05f;
    edge.flexibility      = 1.0f;
    edge.densityScale     = 1.5f;
    edge.windDeflection   = 1.20f;
    edge.windTipMetres    = 0.0f;
    edge.windSpatialFreq  = 0.02f;
    edge.windTemporalFreq = 0.0f;
    edge.windBaseYaw      = Dark::Math::Pi;
    clampGrassParams(edge);
    EXPECT_FLOAT_EQ(edge.heightMetres, 0.05f);
    EXPECT_FLOAT_EQ(edge.flexibility, 1.0f);
    EXPECT_FLOAT_EQ(edge.densityScale, 1.5f);
    EXPECT_FLOAT_EQ(edge.windDeflection, 1.20f);
    EXPECT_FLOAT_EQ(edge.windTipMetres, 0.0f);
    EXPECT_FLOAT_EQ(edge.windSpatialFreq, 0.02f);
    EXPECT_FLOAT_EQ(edge.windTemporalFreq, 0.0f);
    EXPECT_FLOAT_EQ(edge.windBaseYaw, Dark::Math::Pi);

    EXPECT_NEAR(wrapYaw(1.37f), 1.37f, 1.0e-6f);
    EXPECT_NEAR(wrapYaw(1.37f + Dark::Math::TwoPi), 1.37f, 1.0e-5f);
    EXPECT_NEAR(wrapYaw(1.37f - Dark::Math::TwoPi), 1.37f, 1.0e-5f);
    EXPECT_NEAR(wrapYaw(1.37f + 2.0f * Dark::Math::TwoPi), 1.37f, 1.0e-5f);
    EXPECT_GT(wrapYaw(-Dark::Math::Pi - 0.25f), -Dark::Math::Pi);
    EXPECT_NEAR(wrapYaw(-Dark::Math::Pi - 0.25f), Dark::Math::Pi - 0.25f, 1.0e-5f);
    EXPECT_NEAR(wrapYaw(Dark::Math::Pi + 0.25f), -Dark::Math::Pi + 0.25f, 1.0e-5f);

    GrassParams defaults;
    clampGrassParams(defaults);
    GrassParams again = defaults;
    clampGrassParams(again);
    EXPECT_FLOAT_EQ(defaults.heightMetres, 0.55f);
    EXPECT_FLOAT_EQ(defaults.flexibility, 0.65f);
    EXPECT_FLOAT_EQ(defaults.densityScale, 1.0f);
    EXPECT_FLOAT_EQ(defaults.windBaseYaw, 1.37f);
    EXPECT_FLOAT_EQ(defaults.windDeflection, 0.55f);
    EXPECT_FLOAT_EQ(defaults.windTipMetres, 0.40f);
    EXPECT_FLOAT_EQ(defaults.windSpatialFreq, 0.004f);
    EXPECT_FLOAT_EQ(defaults.windTemporalFreq, 0.015f);
    EXPECT_EQ(defaults.seed, 1337u);
    EXPECT_FALSE(defaults.enabled);
    EXPECT_FLOAT_EQ(defaults.windBaseYaw, again.windBaseYaw);
    EXPECT_FLOAT_EQ(defaults.windTemporalFreq, again.windTemporalFreq);
    EXPECT_FLOAT_EQ(defaults.heightMetres, again.heightMetres);
}
