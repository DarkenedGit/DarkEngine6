#include <gtest/gtest.h>

#include "Math/Vector3f.h"
#include "Render/DeferredLightingPipeline.h"
#include "Render/Fog.h"
#include "Sky/Environment.h"
#include "Terrain/HeightMap.h"

#include <cstddef>

using Dark::FogGpu;
using Dark::applyFogToLighting;
using Dark::exponentialHeightOpticalDepth;
using Dark::fillFogHeightMap;
using Dark::heightFogAmount;
using Dark::makeFogGpu;
using Dark::valleyFogDensity;
using Dark::LightingConstants;
using Dark::Sky::Environment;
using Dark::Terrain::HeightMap;
using Dark::Terrain::kMaxHeightMapSize;

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
    EXPECT_EQ(sizeof(LightingConstants), 56u * sizeof(float));
    EXPECT_EQ(offsetof(LightingConstants, pbrLightColor), 48u * sizeof(float));
    EXPECT_EQ(offsetof(LightingConstants, iblIntensity), 51u * sizeof(float));
}

TEST(Fog, FillFogHeightMap_CoarseAndDummy)
{
    FogGpu dummy{};
    dummy.heightOriginX    = 9.0f;
    dummy.heightCellSize   = 9.0f;
    dummy.heightWorldSizeX = 9.0f;
    fillFogHeightMap(dummy, nullptr);
    EXPECT_FLOAT_EQ(dummy.heightCellSize, 0.0f);

    HeightMap invalid;
    dummy.heightCellSize = 9.0f;
    fillFogHeightMap(dummy, &invalid);
    EXPECT_FLOAT_EQ(dummy.heightCellSize, 0.0f);

    HeightMap coarse;
    ASSERT_TRUE(coarse.create(kMaxHeightMapSize, kMaxHeightMapSize, 2.0f, 1.0f));
    coarse.setOrigin(Dark::Math::Vector3f{ -1024.0f, 0.0f, -1024.0f });

    FogGpu fog{};
    fillFogHeightMap(fog, &coarse);
    EXPECT_FLOAT_EQ(fog.heightOriginX, -1024.0f);
    EXPECT_FLOAT_EQ(fog.heightOriginZ, -1024.0f);
    EXPECT_FLOAT_EQ(fog.heightCellSize, 2.0f);
    EXPECT_FLOAT_EQ(fog.heightWorldSizeX, 2048.0f);
    EXPECT_FLOAT_EQ(fog.heightWorldSizeZ, 2048.0f);

    LightingConstants lc{};
    applyFogToLighting(lc, fog);
    EXPECT_EQ(sizeof(LightingConstants), 56u * sizeof(float));
    EXPECT_FLOAT_EQ(lc.heightOriginX, -1024.0f);
    EXPECT_FLOAT_EQ(lc.heightCellSize, 2.0f);
    EXPECT_FLOAT_EQ(lc.heightWorldSizeX, 2048.0f);
}
