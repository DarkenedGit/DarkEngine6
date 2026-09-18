#include <gtest/gtest.h>

#include "Render/DeferredLightingPipeline.h"
#include "Render/LocalLightVolumePipeline.h"
#include "Render/SceneBuffers.h"

using Dark::DeferredLightingPipeline;
using Dark::LocalLightVolumePipeline;
using Dark::SceneBuffers;

TEST(SceneBuffers, LightingCount)
{
    EXPECT_EQ(SceneBuffers::kLightingAlbedo, 0u);
    EXPECT_EQ(SceneBuffers::kLightingAttrib, 1u);
    EXPECT_EQ(SceneBuffers::kLightingDepth, 2u);
    EXPECT_EQ(SceneBuffers::kLightingShadow, 3u);
    EXPECT_EQ(SceneBuffers::kLightingHeight, 4u);
    EXPECT_EQ(SceneBuffers::kLightingAo, 5u);
    EXPECT_EQ(SceneBuffers::kLightingIblIrradiance, 6u);
    EXPECT_EQ(SceneBuffers::kLightingIblPrefilter, 7u);
    EXPECT_EQ(SceneBuffers::kLightingIblBrdfLut, 8u);
    EXPECT_EQ(SceneBuffers::kLightingCount, 9u);
}

TEST(SceneBuffers, GBufferRtvLayout)
{
    EXPECT_EQ(SceneBuffers::kRtvHdr, 0u);
    EXPECT_EQ(SceneBuffers::kRtvAlbedo, 1u);
    EXPECT_EQ(SceneBuffers::kRtvAttrib, 2u);
    EXPECT_EQ(SceneBuffers::kRtvVelocity, 3u);
    EXPECT_EQ(SceneBuffers::kRtvPost, 4u);
    EXPECT_EQ(SceneBuffers::kRtvHistory, 5u);
    EXPECT_EQ(SceneBuffers::kRtvAo, 6u);
    EXPECT_EQ(SceneBuffers::kRtvCountGBuffer, 7u);
    EXPECT_FLOAT_EQ(SceneBuffers::kAoClear[0], 1.0f);
}

TEST(SceneBuffers, LightingRootAoSlots)
{
    EXPECT_EQ(DeferredLightingPipeline::kRootAoSrv, 4u);
    EXPECT_EQ(LocalLightVolumePipeline::kRootAoSrv, 5u);
    EXPECT_EQ(DeferredLightingPipeline::kRootHeightSrv, 3u);
    EXPECT_EQ(LocalLightVolumePipeline::kRootHeightSrv, 4u);
    EXPECT_EQ(DeferredLightingPipeline::kRootIblSrv, 5u);
}
