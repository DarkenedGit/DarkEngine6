#include <gtest/gtest.h>

#include "Render/DeferredLightingPipeline.h"
#include "Render/Fog.h"
#include "Sky/Environment.h"

using Dark::FogGpu;
using Dark::applyFogToLighting;
using Dark::exponentialHeightOpticalDepth;
using Dark::heightFogAmount;
using Dark::makeFogGpu;
using Dark::valleyFogDensity;
using Dark::LightingConstants;
using Dark::Sky::Environment;

TEST(Fog, HeightFogAmountPeaksOnTheHorizon)
{
    EXPECT_NEAR(heightFogAmount(0.0f), 1.0f, 1.0e-3f);
    EXPECT_LT(heightFogAmount(1.1f), 0.02f);
    EXPECT_LT(heightFogAmount(-0.40f), 0.02f);
    EXPECT_GT(heightFogAmount(0.08f), 0.7f);
    EXPECT_GT(heightFogAmount(-0.06f), 0.7f);
}

TEST(Fog, HeightOpticalDepthThickerAtLowAltitude)
{
    const float dLow  = exponentialHeightOpticalDepth(2.0f, 40.0f, 0.0f, 0.05f, 0.085f, 0.0f);
    const float dHigh = exponentialHeightOpticalDepth(40.0f, 40.0f, 0.0f, 0.05f, 0.085f, 0.0f);
    EXPECT_GT(dLow, dHigh * 2.0f);
    EXPECT_GT(dLow, 0.5f);
}

TEST(Fog, ValleyDensityOnlyWhereTerrainMeetsWater)
{
    const float water = 10.0f;
    const float slab  = 6.5f;
    const float dens  = 0.1f;
    EXPECT_GT(valleyFogDensity(water, water - 2.0f, water, slab, dens), 0.05f);
    EXPECT_NEAR(valleyFogDensity(water + 20.0f, water - 2.0f, water, slab, dens), 0.0f, 1.0e-3f);
    EXPECT_NEAR(valleyFogDensity(water, water + 20.0f, water, slab, dens), 0.0f, 1.0e-3f);
}

TEST(Fog, MakeFogGpuZerosWhenUnlit)
{
    Environment env;
    env.timeOfDay = 19.6f;
    env.evaluate();
    const FogGpu off = makeFogGpu(&env, 8.0f, false);
    EXPECT_EQ(off.fogDensity, 0.0f);
    EXPECT_EQ(off.heightFogDensity, 0.0f);
    EXPECT_EQ(off.volumetricFogDensity, 0.0f);

    const FogGpu on = makeFogGpu(&env, 8.0f, true);
    EXPECT_GT(on.heightFogDensity, 0.0f);
    EXPECT_FLOAT_EQ(on.waterLevel, 8.0f);
    EXPECT_FLOAT_EQ(on.heightFogHeight, 8.0f);
}

TEST(Fog, ApplyFogToLightingCopiesFields)
{
    FogGpu fog      = makeFogGpu(nullptr, 4.0f, true);
    fog.fogDensity  = 0.01f;
    fog.fogColor[0] = 0.2f;
    LightingConstants lc{};
    applyFogToLighting(lc, fog);
    EXPECT_FLOAT_EQ(lc.fogDensity, 0.01f);
    EXPECT_FLOAT_EQ(lc.fogColor[0], 0.2f);
    EXPECT_FLOAT_EQ(lc.waterLevel, 4.0f);
    EXPECT_EQ(sizeof(LightingConstants), 46u * sizeof(float));
}
