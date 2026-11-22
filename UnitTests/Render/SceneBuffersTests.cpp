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
    EXPECT_EQ(SceneBuffers::kLightingSsr, 9u);
    EXPECT_EQ(SceneBuffers::kLightingLocalShadow, 10u);
    EXPECT_EQ(SceneBuffers::kLightingLocalShadowRecords, 11u);
    EXPECT_EQ(SceneBuffers::kLightingCount, 12u);
}

TEST(SceneBuffers, InvalidUntilCreated)
{
    // Render-graph plan Phase 5: the G-buffer, HDR, post and AO targets are graph targets (CPU views
    // from the pool), so there is no RTV heap layout left; only the clear contract stays.
    SceneBuffers buffers;
    EXPECT_FALSE(buffers.valid());
    EXPECT_EQ(buffers.width(), 0u);
    EXPECT_EQ(buffers.height(), 0u);
    EXPECT_EQ(buffers.lightingHeap(), nullptr);
    EXPECT_FLOAT_EQ(SceneBuffers::kAoClear[0], 1.0f);
}

TEST(SceneBuffers, LightingRootAoSlots)
{
    EXPECT_EQ(DeferredLightingPipeline::kRootAoSrv, 4u);
    EXPECT_EQ(LocalLightVolumePipeline::kRootAoSrv, 5u);
    EXPECT_EQ(DeferredLightingPipeline::kRootHeightSrv, 3u);
    EXPECT_EQ(LocalLightVolumePipeline::kRootHeightSrv, 4u);
    EXPECT_EQ(DeferredLightingPipeline::kRootIblSrv, 5u);
    EXPECT_EQ(DeferredLightingPipeline::kRootSsrSrv, 6u);
    EXPECT_EQ(LocalLightVolumePipeline::kRootShadowTable, 6u);
}
