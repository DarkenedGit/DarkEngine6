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
    // What the hosts still drive: their world draws, the shadow systems, the game passes (Phase 8) and the meter.
    template <class S> constexpr bool kHostSurface = requires(S& s) {
        s.meshPipeline();
        s.skinnedPipeline();
        s.skinRing();
        s.localLightGpu();
        s.shadows();
        s.localShadows();
        s.camouflage();
        s.echo();
        s.autoExposure();
        s.features();
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
    EXPECT_TRUE(kHostSurface<SceneRenderer>);
}

TEST(SceneRenderer, SsrReadyNeedsTheSsrPipelines)
{
    const SceneRenderer scene;
    EXPECT_FALSE(scene.isValid());
    EXPECT_FALSE(scene.ssrReady());
}
