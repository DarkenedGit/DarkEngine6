#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <gtest/gtest.h>

#include "Render/SceneRenderer.h"

using namespace Dark;

namespace
{
    // The core pipelines are SceneRenderer's own: hosts declare their passes and never reach them.
    template <class S> constexpr bool kReachesTonemap    = requires(S& s) { s.tonemap(); };
    template <class S> constexpr bool kReachesLighting   = requires(S& s) { s.lighting(); };
    template <class S> constexpr bool kReachesLocalLight = requires(S& s) { s.localLightVolumes(); };
    template <class S> constexpr bool kReachesVolumes    = requires(S& s) { s.pointVolumeMesh(); } || requires(S& s) { s.spotVolumeMesh(); };
    template <class S> constexpr bool kReachesBloom      = requires(S& s) { s.bloom(); };
    template <class S> constexpr bool kReachesGtao       = requires(S& s) { s.gtao(); };
    template <class S> constexpr bool kReachesSsr        = requires(S& s) { s.ssr(); };
    template <class S> constexpr bool kReachesMotionBlur = requires(S& s) { s.motionBlur(); };
    template <class S> constexpr bool kReachesTaa        = requires(S& s) { s.taa(); };
    template <class S> constexpr bool kReachesTiles      = requires(S& s) { s.debugOverlay(); };
    template <class S> constexpr bool kReachesWantsTaa   = requires(S& s, const Renderer& r) { s.wantsTaa(r); };
    template <class S> constexpr bool kReachesLightDraws =
        requires(S& s, ID3D12GraphicsCommandList* cmd, const LightingConstants& lc) { s.drawDeferredLighting(cmd, D3D12_GPU_DESCRIPTOR_HANDLE{}, lc); };
    // The core entries' passes come from registerCoreFeatures, not from host declare callbacks.
    template <class S> constexpr bool kReachesResolvePasses =
        requires(S& s, FeatureBuilder& b, Renderer& r, const Camera3D& c, const DeferredResolveInput& in) { s.addDecalsPasses(b, r, c, in); } ||
        requires(S& s, FeatureBuilder& b, Renderer& r, const Camera3D& c, const GtaoSettings& g) { s.addGtaoPasses(b, r, c, g); } ||
        requires(S& s, FeatureBuilder& b, Renderer& r, const Camera3D& c, const SsrSettings& g) { s.addSsrPasses(b, r, c, g); } ||
        requires(S& s, FeatureBuilder& b, Renderer& r, const DeferredResolveInput& in) { s.addLightingPasses(b, r, in); } ||
        requires(S& s, FeatureBuilder& b, Renderer& r, World& w, const Camera3D& c, const DeferredResolveInput& in) { s.addLocalVolumesPasses(b, r, w, c, in); };
    template <class S> constexpr bool kReachesFramePasses =
        requires(S& s, FeatureBuilder& b, Renderer& r, const CorePassHook& h) { s.addGBufferPasses(b, r, h); } ||
        requires(S& s, FeatureBuilder& b, Renderer& r, const CorePassHook& h) { s.addTranslucentPasses(b, r, h); } ||
        requires(S& s, FeatureBuilder& b, Renderer& r) { s.addSsrCapturePasses(b, r); } ||
        requires(S& s, FeatureBuilder& b, Renderer& r, const DebugTileRequest& t) { s.addDebugTilePasses(b, r, t, 0); };
    template <class S> constexpr bool kReachesPostPasses = requires(S& s, FeatureBuilder& b, Renderer& r, const Math::Matrix4f& vp, const TonemapSettings& t) {
        s.addPostPasses(b, r, vp, t, false);
    } || requires(S& s, Renderer& r) { s.syncAutoExposureSize(r); };
    // The shadow systems go through ShadowFeature (registerShadowFeature(SceneRenderer&, ...)); the
    // local-light list and the meter through narrow calls.
    template <class S> constexpr bool kReachesShadowSystems     = requires(S& s) { s.shadows(); } || requires(S& s) { s.localShadows(); } || requires(S& s) { s.skinnedShadowPipeline(); };
    template <class S> constexpr bool kReachesLightListAndMeter = requires(S& s) { s.localLightGpu(); } || requires(S& s) { s.autoExposure(); };
    // The world-draw pipelines and the skinning ring reach the hosts only as the WorldDrawContext
    // their GBuffer/Translucent hooks and camouflage draw receive.
    template <class S> constexpr bool kReachesWorldDraws = requires(S& s) { s.meshPipeline(); } || requires(S& s) { s.meshTransparentPipeline(); } || requires(S& s) { s.skinnedPipeline(); } ||
                                                           requires(S& s) { s.skinnedTransparentPipeline(); } || requires(S& s) { s.skinRing(); } || requires(S& s) { s.worldDraws(); };
    // What the hosts still drive: the skinning ring's frame, the game passes, the water's local
    // lights and the meter's samples.
    template <class S> constexpr bool kHostSurface = requires(S& s, const LocalLightDrawLists& lists) {
        s.beginSkinningFrame(0u);
        s.camouflage();
        s.echo();
        s.localLightsReady();
        s.localLightsDummyVa();
        s.uploadLocalLights(0u, lists);
        s.invalidateAutoExposureSamples();
        s.features();
        s.setCoreFrame(CoreFrameInputs{});
    };
} // namespace

TEST(SceneRenderer, HostsDoNotReachTheCorePipelines)
{
    EXPECT_FALSE(kReachesTonemap<SceneRenderer>);
    EXPECT_FALSE(kReachesLighting<SceneRenderer>);
    EXPECT_FALSE(kReachesLocalLight<SceneRenderer>);
    EXPECT_FALSE(kReachesVolumes<SceneRenderer>);
    EXPECT_FALSE(kReachesBloom<SceneRenderer>);
    EXPECT_FALSE(kReachesGtao<SceneRenderer>);
    EXPECT_FALSE(kReachesSsr<SceneRenderer>);
    EXPECT_FALSE(kReachesMotionBlur<SceneRenderer>);
    EXPECT_FALSE(kReachesTaa<SceneRenderer>);
    EXPECT_FALSE(kReachesTiles<SceneRenderer>);
    EXPECT_FALSE(kReachesWantsTaa<SceneRenderer>);
    EXPECT_FALSE(kReachesLightDraws<SceneRenderer>);
    EXPECT_FALSE(kReachesResolvePasses<SceneRenderer>);
    EXPECT_FALSE(kReachesFramePasses<SceneRenderer>);
    EXPECT_FALSE(kReachesPostPasses<SceneRenderer>);
    EXPECT_FALSE(kReachesShadowSystems<SceneRenderer>);
    EXPECT_FALSE(kReachesLightListAndMeter<SceneRenderer>);
    EXPECT_FALSE(kReachesWorldDraws<SceneRenderer>);
    EXPECT_TRUE(kHostSurface<SceneRenderer>);
}

TEST(SceneRenderer, SsrReadyNeedsTheSsrPipelines)
{
    const SceneRenderer scene;
    EXPECT_FALSE(scene.isValid());
    EXPECT_FALSE(scene.ssrReady());
}

TEST(SceneRenderer, UncreatedLocalLightListIsNotReadyAndHasNoAddress)
{
    SceneRenderer scene;
    EXPECT_FALSE(scene.localLightsReady());
    EXPECT_EQ(scene.localLightsDummyVa(), 0u);
    LocalLightDrawLists lists{};
    lists.count = 1;
    EXPECT_EQ(scene.uploadLocalLights(0, lists), 0u);
    scene.invalidateAutoExposureSamples();
}

TEST(SceneRenderer, UncreatedSkinningRingIgnoresTheFrameStart)
{
    SceneRenderer scene;
    scene.beginSkinningFrame(0);
    scene.beginSkinningFrame(1);
    EXPECT_FALSE(scene.isValid());
}
