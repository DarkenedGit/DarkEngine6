#include <gtest/gtest.h>

#include "Render/DeferredLightingPipeline.h"
#include "Render/GraphTargets.h"
#include "Render/LocalLightVolumePipeline.h"
#include "Render/RhiDevice.h"
#include "Render/SceneBuffers.h"

#include <d3d12.h>
#include <wrl/client.h>

using Dark::DeferredLightingPipeline;
using Dark::LightingViews;
using Dark::LocalLightVolumePipeline;
using Dark::SceneBuffers;
using Microsoft::WRL::ComPtr;

TEST(SceneBuffers, LightingCount)
{
    // The deferred lighting pass's ring table, in register order (render-graph plan Phase 6 part 4).
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
    EXPECT_EQ(SceneBuffers::kLightingColorRemap, 10u);
    EXPECT_EQ(SceneBuffers::kLightingCount, 11u);
}

TEST(SceneBuffers, LocalLightTableMatchesItsRootRanges)
{
    // t0..t2 one range, then AO (t3), height (t6), local shadow array + records (t7..t8).
    EXPECT_EQ(LocalLightVolumePipeline::kTableAttrib, LocalLightVolumePipeline::kTableAlbedo + 1u);
    EXPECT_EQ(LocalLightVolumePipeline::kTableDepth, LocalLightVolumePipeline::kTableAlbedo + 2u);
    EXPECT_EQ(LocalLightVolumePipeline::kTableLocalShadowRecords, LocalLightVolumePipeline::kTableLocalShadow + 1u);
    EXPECT_EQ(LocalLightVolumePipeline::kTableColorRemap, 7u);
    EXPECT_EQ(LocalLightVolumePipeline::kTableCount, 8u);
}

TEST(SceneBuffers, InvalidUntilCreated)
{
    // Render-graph plan Phase 5: the G-buffer, HDR, post and AO targets are graph targets (CPU views
    // from the pool). Phase 6 part 4: no shader-visible heap; without views every lighting view is 0.
    SceneBuffers buffers;
    EXPECT_FALSE(buffers.valid());
    EXPECT_EQ(buffers.width(), 0u);
    EXPECT_EQ(buffers.height(), 0u);
    const LightingViews v = buffers.lightingViews();
    EXPECT_EQ(v.albedo.ptr, 0u);
    EXPECT_EQ(v.shadow.ptr, 0u);
    EXPECT_EQ(v.localShadowRecords.ptr, 0u);
    EXPECT_FLOAT_EQ(SceneBuffers::kAoClear[0], 1.0f);
    EXPECT_FLOAT_EQ(SceneBuffers::kSurfaceTagClear[0], 0.0f);
    EXPECT_FLOAT_EQ(SceneBuffers::kSurfaceTagClear[1], 0.0f);
}

TEST(SceneBuffers, HostViewsAreStoredAndSurviveAFailedCreate)
{
    SceneBuffers                      buffers;
    const D3D12_CPU_DESCRIPTOR_HANDLE height{ 0x2000 }, irr{ 0x3000 }, pref{ 0x4000 }, lut{ 0x5000 }, arr{ 0x6000 }, rec{ 0x7000 };
    buffers.setHeightSrv(height);
    buffers.setIblSrvs(irr, pref, lut);
    buffers.setLocalShadowSrvs(arr, rec);
    const auto check = [&]()
    {
        const LightingViews v = buffers.lightingViews();
        EXPECT_EQ(v.shadow.ptr, 0u); // no heap: the null view does not exist yet
        EXPECT_EQ(v.height.ptr, height.ptr);
        EXPECT_EQ(v.iblIrradiance.ptr, irr.ptr);
        EXPECT_EQ(v.iblPrefilter.ptr, pref.ptr);
        EXPECT_EQ(v.iblBrdfLut.ptr, lut.ptr);
        EXPECT_EQ(v.localShadow.ptr, arr.ptr);
        EXPECT_EQ(v.localShadowRecords.ptr, rec.ptr);
    };
    check();
    Dark::GraphTargets targets; // no device
    const float        hdrClear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    EXPECT_FALSE(buffers.create(targets, hdrClear));
    // create keeps the views the host systems set (they outlive a resize or a failed create).
    check();
}

TEST(SceneBuffers, UnsetLightingViewsAreNullSrvsWithDevice)
{
    ComPtr<ID3D12Device> device = Dark::createWarpTestDevice();
    if (!device)
        GTEST_SKIP() << "no D3D12 device (WARP or hardware)";
    Dark::GraphTargets targets; // declared first: owners unregister before it goes away
    ASSERT_TRUE(targets.init(device.Get(), 64, 64));
    SceneBuffers buffers;
    const float  hdrClear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    ASSERT_TRUE(buffers.create(targets, hdrClear));

    // Nothing set: every view is still a real (null) descriptor, so a ring table never copies 0.
    LightingViews v = buffers.lightingViews();
    EXPECT_NE(v.albedo.ptr, 0u);
    EXPECT_EQ(v.attrib.ptr, buffers.attribSrvCpu().ptr);
    EXPECT_NE(v.shadow.ptr, 0u);
    EXPECT_NE(v.height.ptr, 0u);
    EXPECT_NE(v.iblIrradiance.ptr, 0u);
    EXPECT_NE(v.iblPrefilter.ptr, 0u);
    EXPECT_NE(v.iblBrdfLut.ptr, 0u);
    EXPECT_NE(v.localShadow.ptr, 0u);
    EXPECT_NE(v.localShadowRecords.ptr, 0u);
    EXPECT_NE(v.albedo.ptr, buffers.albedoSrvCpu().ptr) << "lighting samples albedo through its own sRGB view";

    // The directional shadow map is bound from the frame graph's import; this is only its fallback.
    EXPECT_EQ(v.shadow.ptr, v.localShadow.ptr) << "both fall back to the null array view";

    // The raw-albedo debug view rewrites the same CPU slot; resize keeps it.
    buffers.setLightingAlbedoRaw(true);
    EXPECT_TRUE(buffers.lightingAlbedoRaw());
    EXPECT_EQ(buffers.lightingViews().albedo.ptr, v.albedo.ptr);
    ASSERT_TRUE(targets.resizeAfterDrain(128, 128));
    ASSERT_TRUE(buffers.rebuildViews());
    EXPECT_TRUE(buffers.lightingAlbedoRaw());
    EXPECT_NE(buffers.lightingViews().albedo.ptr, 0u);
}
