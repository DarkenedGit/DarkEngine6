#include <gtest/gtest.h>

#include "Core/Log.h"
#include "Render/FrameFeatures.h"
#include "Render/DebugRenderState.h"
#include "Render/FrameSetup.h"
#include "Render/FrameView.h"
#include "Render/GBufferInputs.h"
#include "Render/HdrDrawInputs.h"
#include "Render/LightingInputs.h"
#include "Render/ShadowInputs.h"
#include "RenderGraphTestUtil.h"

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <tuple>
#include <vector>

// Frame setup (render-graph plan §3.4.2): FrameSetupHooks resolves the passes a frame turns off,
// disabled outputs and PassBuilder::tryRead give their readers a fallback, and a pass that comes back
// reads its history as invalid exactly once.

using namespace Dark;
using Rg::Test::absTex;
using Rg::Test::errorText;

namespace
{
    std::vector<std::string> g_logs;
    void                     captureLogs(LogLevel level, LogCategory, const char* message)
    {
        if (level == LogLevel::Error || level == LogLevel::Warn)
            g_logs.emplace_back(message);
    }
    struct LogCapture
    {
        LogCapture()
        {
            g_logs.clear();
            Log::addCapture(&captureLogs);
        }
        ~LogCapture()
        {
            Log::removeCapture(&captureLogs);
        }
    };
    size_t logsWith(const char* text)
    {
        return static_cast<size_t>(std::count_if(g_logs.begin(), g_logs.end(), [text](const std::string& m) { return m.find(text) != std::string::npos; }));
    }

    struct Hook : IFrameSetupHook
    {
        const char* hookName = "Test";
        uint32_t    disable  = 0;
        uint32_t    clear    = 0; // bits it tries to turn back on
        int         calls    = 0;
        const char* name() const override
        {
            return hookName;
        }
        void setupFrame(const FrameView&, FrameSetup& setup) override
        {
            ++calls;
            setup.disabled.bits = (setup.disabled.bits & ~clear) | disable;
        }
    };

    constexpr uint32_t kDir      = EnginePassMask::bit(EnginePass::DirectionalShadows);
    constexpr uint32_t kLocal    = EnginePassMask::bit(EnginePass::LocalShadows);
    constexpr uint32_t kDecals   = EnginePassMask::bit(EnginePass::Decals);
    constexpr uint32_t kGtao     = EnginePassMask::bit(EnginePass::Gtao);
    constexpr uint32_t kSsr      = EnginePassMask::bit(EnginePass::Ssr);
    constexpr uint32_t kLighting = EnginePassMask::bit(EnginePass::DeferredLighting);
    constexpr uint32_t kLights   = EnginePassMask::bit(EnginePass::LocalLights);
    constexpr uint32_t kSky      = EnginePassMask::bit(EnginePass::Sky);
    constexpr uint32_t kClouds   = EnginePassMask::bit(EnginePass::Clouds);
    constexpr uint32_t kCapture  = EnginePassMask::bit(EnginePass::SsrCapture);
    constexpr uint32_t kUnder    = EnginePassMask::bit(EnginePass::Underwater);
    constexpr uint32_t kBloom    = EnginePassMask::bit(EnginePass::Bloom);
    constexpr uint32_t kTaa      = EnginePassMask::bit(EnginePass::Taa);
    constexpr uint32_t kMb       = EnginePassMask::bit(EnginePass::MotionBlur);
    constexpr uint32_t kAe       = EnginePassMask::bit(EnginePass::AutoExposure);
    constexpr uint32_t kHonoured = kEnginePassesHonoured;
} // namespace

TEST(FrameSetup, NoHookKeepsTheViewSetup)
{
    FrameSetupHooks hooks;
    FrameView       view;
    EXPECT_EQ(hooks.resolve(view, 0.0), FrameSetup{});
    EXPECT_FLOAT_EQ(hooks.resolve(view, 0.0).shadowStrength, 1.0f);
    EXPECT_FLOAT_EQ(hooks.resolve(view, 0.0).aoStrength, 1.0f);
    EXPECT_FLOAT_EQ(hooks.resolve(view, 0.0).ssrStrength, 1.0f);
    EXPECT_FLOAT_EQ(hooks.resolve(view, 0.0).emissiveGain, 1.0f);
    view.setup.disabled.set(EnginePass::Ssr);
    EXPECT_EQ(hooks.resolve(view, 0.1).disabled.bits, kSsr);
    EXPECT_FLOAT_EQ(hooks.resolve(view, 0.1).shadowStrength, 1.0f);
}

TEST(FrameSetup, EmptyHookTurnsNothingOff)
{
    FrameSetupHooks hooks;
    Hook            empty;
    ASSERT_TRUE(hooks.add(&empty));
    const FrameSetup s = hooks.resolve(FrameView{}, 0.0);
    EXPECT_EQ(empty.calls, 1);
    EXPECT_FALSE(s.disabled.any());
    EXPECT_FLOAT_EQ(s.shadowStrength, 1.0f);
    EXPECT_FLOAT_EQ(s.aoStrength, 1.0f);
    EXPECT_FLOAT_EQ(s.ssrStrength, 1.0f);
    EXPECT_FLOAT_EQ(s.emissiveGain, 1.0f);
    EXPECT_EQ(s.albedoRemap, ColorRemap{});
    EXPECT_EQ(s.fogRemap, ColorRemap{});
    EXPECT_EQ(s.waterRemap, ColorRemap{});
    EXPECT_EQ(s.skyRemap, ColorRemap{});
    EXPECT_FALSE(s.writeSurfaceTag);
    EXPECT_EQ(s.surfaceTagLut, nullptr);
    EXPECT_EQ(s.surfaceTagLutSize, 0u);
    for (uint32_t i = 0; i < kEnginePassCount; ++i)
        EXPECT_FALSE(s.disabled.has(static_cast<EnginePass>(i))) << enginePassName(static_cast<EnginePass>(i));
}

TEST(FrameSetup, HooksRunInOrderAndCanOnlyTurnPassesOff)
{
    FrameSetupHooks hooks;
    Hook            a, b;
    a.disable = kGtao;
    b.disable = kSsr;
    b.clear   = kGtao;
    ASSERT_TRUE(hooks.add(&a));
    ASSERT_TRUE(hooks.add(&b));
    FrameView view;
    EXPECT_EQ(hooks.resolve(view, 0.0).disabled.bits, kGtao | kSsr);
    hooks.remove(&a);
    EXPECT_EQ(hooks.resolve(view, 0.1).disabled.bits, kSsr);
    view.setup.disabled.set(EnginePass::Gtao); // the host's own bit survives a hook clearing it
    EXPECT_EQ(hooks.resolve(view, 0.2).disabled.bits, kGtao | kSsr);
    hooks.clear();
    EXPECT_EQ(hooks.count(), 0u);
}

TEST(FrameSetup, AddRejectsNullAndDuplicates)
{
    LogCapture      logs;
    FrameSetupHooks hooks;
    Hook            a;
    EXPECT_FALSE(hooks.add(nullptr));
    EXPECT_TRUE(hooks.add(&a));
    EXPECT_FALSE(hooks.add(&a));
    EXPECT_EQ(hooks.count(), 1u);
    EXPECT_EQ(logsWith("null or already added"), 2u);
}

TEST(FrameSetup, UnnamedHookIsSkippedAndLoggedOnce)
{
    LogCapture      logs;
    FrameSetupHooks hooks;
    Hook            unnamed, empty, named;
    unnamed.hookName = nullptr;
    unnamed.disable  = kGtao;
    empty.hookName   = "";
    empty.disable    = kGtao;
    named.disable    = kSsr;
    ASSERT_TRUE(hooks.add(&unnamed));
    ASSERT_TRUE(hooks.add(&empty));
    ASSERT_TRUE(hooks.add(&named));
    for (int i = 0; i < 3; ++i)
        EXPECT_EQ(hooks.resolve(FrameView{}, i * 0.5).disabled.bits, kSsr);
    EXPECT_EQ(unnamed.calls, 0);
    EXPECT_EQ(empty.calls, 0);
    EXPECT_EQ(named.calls, 3);
    EXPECT_EQ(logsWith("has no name"), 2u); // once per hook
}

TEST(FrameSetup, EveryEnginePassBitIsHonoured)
{
    LogCapture      logs;
    FrameSetupHooks hooks;
    Hook            h;
    h.disable = kEnginePassesHonoured;
    ASSERT_TRUE(hooks.add(&h));
    const FrameSetup a = hooks.resolve(FrameView{}, 0.0);
    const FrameSetup b = hooks.resolve(FrameView{}, 0.1);
    EXPECT_EQ(a.disabled.bits, kEnginePassesHonoured);
    EXPECT_EQ(b.disabled.bits, kEnginePassesHonoured);
    EXPECT_EQ(logsWith("not supported yet"), 0u);
    for (uint32_t i = 0; i < kEnginePassCount; ++i)
        EXPECT_TRUE(a.disabled.has(static_cast<EnginePass>(i))) << enginePassName(static_cast<EnginePass>(i));
    EXPECT_EQ(kEnginePassesHonoured, (1u << kEnginePassCount) - 1u);
}

TEST(FrameSetup, BloomAndThePostBitsAreHonoured)
{
    LogCapture      logs;
    FrameSetupHooks hooks;
    Hook            h;
    h.disable = kDir | kLocal | kBloom | kTaa | kMb | kAe;
    ASSERT_TRUE(hooks.add(&h));
    EXPECT_EQ(hooks.resolve(FrameView{}, 0.0).disabled.bits, kDir | kLocal | kBloom | kTaa | kMb | kAe);
    EXPECT_EQ(hooks.resolve(FrameView{}, 0.1).disabled.bits, kDir | kLocal | kBloom | kTaa | kMb | kAe);
    EXPECT_EQ(logsWith("not supported yet"), 0u);
}

TEST(FrameSetup, MaskChangesAboveTheRateWarnOnce)
{
    constexpr uint32_t n = FrameSetupHooks::kMaxMaskChangesPerSecond;
    {
        // n changes inside a second, then the same rate for a while: no warning.
        LogCapture      logs;
        FrameSetupHooks hooks;
        Hook            h;
        ASSERT_TRUE(hooks.add(&h));
        for (uint32_t i = 0; i <= 4 * n; ++i)
        {
            h.disable = (i & 1) ? kGtao : 0;
            hooks.resolve(FrameView{}, i * (1.01 / n));
        }
        EXPECT_FALSE(hooks.maskChangeWarned());
        EXPECT_EQ(logsWith("changed more than"), 0u);
    }
    {
        // n + 1 changes inside a second: one warning, however long it goes on.
        LogCapture      logs;
        FrameSetupHooks hooks;
        Hook            h;
        ASSERT_TRUE(hooks.add(&h));
        for (uint32_t i = 0; i <= 3 * (n + 1); ++i)
        {
            h.disable = (i & 1) ? kSsr : 0;
            hooks.resolve(FrameView{}, i * (0.99 / (n + 1)));
            EXPECT_EQ(hooks.maskChangeWarned(), i >= n + 1) << i;
        }
        EXPECT_EQ(logsWith("changed more than"), 1u);
    }
    {
        // The same mask every frame never warns.
        FrameSetupHooks hooks;
        Hook            h;
        h.disable = kGtao;
        ASSERT_TRUE(hooks.add(&h));
        for (int i = 0; i < 200; ++i)
            hooks.resolve(FrameView{}, i * 0.001);
        EXPECT_FALSE(hooks.maskChangeWarned());
    }
}

TEST(FrameSetup, ReadingADisabledOutputIsAnError)
{
    for (int write = 0; write < 2; ++write)
    {
        SCOPED_TRACE(write);
        Rg::RenderGraph g;
        g.beginFrame(64, 64);
        const Rg::TextureHandle off = g.disabledOutput("Gtao.Compose");
        EXPECT_FALSE(off.valid());
        EXPECT_TRUE(off.disabledOutput());
        EXPECT_STREQ(g.disabledOutputName(off), "Gtao.Compose");
        EXPECT_EQ(g.disabledOutputName(Rg::TextureHandle{}), nullptr);
        Rg::TextureHandle out = g.importTexture("Out", absTex(64, 64), nullptr, Rg::Layout::Undefined, Rg::Layout::Undefined);
        g.addPass<Rg::NoData>(
            "Lighting",
            [&](Rg::PassBuilder& b, Rg::NoData&)
            {
                if (write)
                    b.write(off, Rg::Use::RenderTarget);
                else
                    b.read(off, Rg::Use::PixelSrv);
                b.write(out, Rg::Use::RenderTarget);
            },
            [](Rg::PassContext&, const Rg::NoData&) {});
        EXPECT_FALSE(g.compile());
        EXPECT_TRUE(g.hasError(Rg::ErrorCode::DisabledOutputUse));
        EXPECT_NE(errorText(g).find("'Gtao.Compose'"), std::string::npos) << errorText(g);
        EXPECT_STREQ(Rg::errorCodeName(Rg::ErrorCode::DisabledOutputUse), "DisabledOutputUse");
    }
}

TEST(FrameSetup, TryReadTakesTheFallbackForADisabledOrMissingOutput)
{
    Rg::RenderGraph g;
    g.beginFrame(64, 64);
    const Rg::TextureHandle authored = g.importTexture("Ao", absTex(64, 64), nullptr, Rg::Layout::ShaderResource, Rg::Layout::Undefined);
    const Rg::TextureHandle real     = g.importTexture("Compose", absTex(64, 64), nullptr, Rg::Layout::ShaderResource, Rg::Layout::Undefined);
    const Rg::TextureHandle off      = g.disabledOutput("Gtao.Compose");
    Rg::TextureHandle       out      = g.importTexture("Out", absTex(64, 64), nullptr, Rg::Layout::Undefined, Rg::Layout::Undefined);
    Rg::TextureHandle       got[4];
    g.addPass<Rg::NoData>(
        "Lighting",
        [&](Rg::PassBuilder& b, Rg::NoData&)
        {
            got[0] = b.tryRead(off, authored);
            got[1] = b.tryRead(Rg::TextureHandle{}, authored);
            got[2] = b.tryRead(real, authored);
            got[3] = b.tryRead(off, Rg::TextureHandle{});
            out    = b.write(out, Rg::Use::RenderTarget);
        },
        [](Rg::PassContext&, const Rg::NoData&) {});
    ASSERT_TRUE(g.compile()) << errorText(g);
    EXPECT_EQ(got[0], authored);
    EXPECT_EQ(got[1], authored);
    EXPECT_EQ(got[2], real);
    EXPECT_FALSE(got[3].valid());
    EXPECT_FALSE(got[3].disabledOutput());
}

TEST(FrameSetup, DisabledOutputsLastOneFrame)
{
    Rg::RenderGraph g;
    g.beginFrame(64, 64);
    const Rg::TextureHandle stale = g.disabledOutput("Ssr.Full");
    g.beginFrame(64, 64);
    EXPECT_EQ(g.disabledOutputName(stale), nullptr);
    Rg::TextureHandle out = g.importTexture("Out", absTex(64, 64), nullptr, Rg::Layout::Undefined, Rg::Layout::Undefined);
    g.addPass<Rg::NoData>(
        "Lighting",
        [&](Rg::PassBuilder& b, Rg::NoData&)
        {
            b.read(stale, Rg::Use::PixelSrv);
            b.write(out, Rg::Use::RenderTarget);
        },
        [](Rg::PassContext&, const Rg::NoData&) {});
    EXPECT_FALSE(g.compile());
    EXPECT_TRUE(g.hasError(Rg::ErrorCode::InvalidHandle));
    EXPECT_FALSE(g.hasError(Rg::ErrorCode::DisabledOutputUse));
}

namespace
{
    ID3D12Resource* fakeResource(uint32_t r)
    {
        return reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(0x100 + r * 0x10));
    }

    // The Sandbox list with GTAO, SSR and Lighting declared the way SceneRenderer does them, each frame
    // set up by FrameSetupHooks: a pass that is off requests no history, adds one opaque pass and
    // publishes disabled outputs; Lighting reads them through lightingReads / declareLightingUses.
    struct SetupFrame
    {
        Rg::RenderGraph graph;
        FeatureFrame    features;
        FrameSetupHooks hooks;
        FrameSetup      setup;
        LightingReads   lit{};
        bool            litRan          = false;
        bool            localRan        = false;
        bool            gtaoPrev        = false; // the GTAO history had a valid prev this frame
        bool            ssrPrev         = false;
        bool            taaPrev         = false;
        bool            gtaoRequested   = false;
        bool            ssrRequested    = false;
        bool            taaRequested    = false;
        bool            shadowRequested = false;
        bool            localRequested  = false;
        bool            strictReader    = false; // Lighting reads GTAO's compose with read(), not tryRead

        bool run(double seconds)
        {
            uint32_t           count = 0;
            const FeaturePass* list  = sandboxFeatures(count);
            Rg::RenderGraph&   g     = graph;
            lit                      = {};
            litRan                   = false;
            localRan                 = false;
            g.beginFrame(1280, 720);
            features.begin(list, count);
            setup = hooks.resolve(FrameView{}, seconds);
            for (uint32_t r = 0; r < kRenderResourceCount; ++r)
            {
                const auto res = static_cast<RenderResource>(r);
                features.importResource(g, res, fakeResource(r), absTex(1280, 720, res == RenderResource::Depth ? Rg::Test::kD32 : Rg::Test::kRgba16F), Rg::Layout::RenderTarget);
            }
            gtaoRequested   = !setup.disabled.has(EnginePass::Gtao);
            ssrRequested    = !setup.disabled.has(EnginePass::Ssr);
            taaRequested    = !setup.disabled.has(EnginePass::Taa);
            shadowRequested = !setup.disabled.has(EnginePass::DirectionalShadows);
            localRequested  = !setup.disabled.has(EnginePass::LocalShadows);
            gtaoPrev = ssrPrev = taaPrev = false;
            if (gtaoRequested)
            {
                const Rg::HistoryPair h = g.history("Gtao", absTex(1280, 720));
                gtaoPrev                = h.prev.valid();
                features.setHistory(RenderResource::GtaoHistory, h);
            }
            if (ssrRequested)
            {
                const Rg::HistoryPair h = g.history("Ssr", absTex(640, 360));
                ssrPrev                 = h.prev.valid();
                features.setHistory(RenderResource::SsrHistory, h);
            }
            if (taaRequested)
            {
                const Rg::HistoryPair h = g.history("Taa", absTex(1280, 720));
                taaPrev                 = h.prev.valid();
                features.setHistory(RenderResource::TaaHistory, h);
            }
            for (uint32_t i = 0; i < count; ++i)
            {
                const PassId id = list[i].id;
                switch (id)
                {
                case PassId::Gtao:
                case PassId::Ssr:
                case PassId::Lighting:
                case PassId::Shadows:
                case PassId::LocalShadows:
                case PassId::Decals:
                case PassId::LocalVolumes:
                case PassId::Sky:
                case PassId::Clouds:
                case PassId::SsrCapture:
                case PassId::Underwater:
                case PassId::Post:
                    break;
                default:
                    features.set(id, [](Rg::PassContext&) {});
                    break;
                }
            }
            features.declare(PassId::Shadows, [this](FeatureBuilder& b) { declareShadows(b); });
            features.declare(PassId::LocalShadows, [this](FeatureBuilder& b) { declareLocalShadows(b); });
            features.declare(PassId::Decals, [this](FeatureBuilder& b) { declareDecals(b); });
            features.declare(PassId::Gtao, [this](FeatureBuilder& b) { declareGtao(b); });
            features.declare(PassId::Ssr, [this](FeatureBuilder& b) { declareSsr(b); });
            features.declare(PassId::Lighting, [this](FeatureBuilder& b) { declareLighting(b); });
            features.declare(PassId::LocalVolumes, [this](FeatureBuilder& b) { declareLocalVolumes(b); });
            features.declare(PassId::Sky, [this](FeatureBuilder& b) { declareSky(b); });
            features.declare(PassId::Clouds, [this](FeatureBuilder& b) { declareClouds(b); });
            features.declare(PassId::SsrCapture, [this](FeatureBuilder& b) { declareSsrCapture(b); });
            features.declare(PassId::Underwater, [this](FeatureBuilder& b) { declareUnderwater(b); });
            features.declare(PassId::Post, [this](FeatureBuilder& b) { declarePost(b); });
            if (!features.compileFrame(g, "test"))
                return false;
            return g.execute();
        }

        void declareShadows(FeatureBuilder& b)
        {
            if (!shadowRequested)
            {
                b.set(RenderResource::Shadow, b.graph().disabledOutput("Shadows"));
                return;
            }
            addShadowPass(b, [](Rg::PassContext&, const ShadowTargets&) {});
        }

        void declareLocalShadows(FeatureBuilder& b)
        {
            if (!localRequested)
            {
                b.set(RenderResource::LocalShadow, b.graph().disabledOutput("LocalShadows"));
                return;
            }
            addLocalShadowPass(b, [](Rg::PassContext&, const ShadowTargets&) {});
        }

        void declareGtao(FeatureBuilder& b)
        {
            if (!gtaoRequested)
            {
                b.opaque("Gtao", {}, {}, [](Rg::PassContext&) {});
                b.graph().blackboard().set(GtaoOutput{ b.graph().disabledOutput("Gtao.Compose"), {} });
                return;
            }
            Rg::TextureHandle compose = b.graph().importTexture("DE.Gtao.AoCompose", absTex(1280, 720), fakeResource(51), Rg::Layout::ShaderResource, Rg::Layout::Undefined);
            b.addPass<Rg::NoData>(
                "Gtao.Upsample",
                [&](Rg::PassBuilder& pb, Rg::NoData&)
                {
                    pb.read(b.handle(RenderResource::Depth), Rg::Use::PixelSrv);
                    if (b.prev(RenderResource::GtaoHistory).valid())
                        pb.read(b.prev(RenderResource::GtaoHistory), Rg::Use::PixelSrv);
                    b.set(RenderResource::GtaoHistory, pb.write(b.handle(RenderResource::GtaoHistory), Rg::Use::RenderTarget));
                },
                [](Rg::PassContext&, const Rg::NoData&) {});
            b.addPass<Rg::NoData>(
                "Gtao.Compose",
                [&](Rg::PassBuilder& pb, Rg::NoData&)
                {
                    pb.read(b.handle(RenderResource::Ao), Rg::Use::PixelSrv);
                    pb.read(b.handle(RenderResource::GtaoHistory), Rg::Use::PixelSrv);
                    compose = pb.write(compose, Rg::Use::RenderTarget);
                },
                [](Rg::PassContext&, const Rg::NoData&) {});
            b.graph().blackboard().set(GtaoOutput{ compose, {} });
        }

        void declareSsr(FeatureBuilder& b)
        {
            if (!ssrRequested)
            {
                b.opaque("Ssr", {}, {}, [](Rg::PassContext&) {});
                b.graph().blackboard().set(SsrOutput{ b.graph().disabledOutput("Ssr.Full"), b.graph().disabledOutput("Ssr.Conf") });
                return;
            }
            Rg::TextureHandle full = b.graph().importTexture("DE.Ssr.Full", absTex(1280, 720), fakeResource(52), Rg::Layout::ShaderResource, Rg::Layout::Undefined);
            b.addPass<Rg::NoData>(
                "Ssr.Upsample",
                [&](Rg::PassBuilder& pb, Rg::NoData&)
                {
                    pb.read(b.handle(RenderResource::Depth), Rg::Use::PixelSrv);
                    if (b.prev(RenderResource::SsrHistory).valid())
                        pb.read(b.prev(RenderResource::SsrHistory), Rg::Use::PixelSrv);
                    b.set(RenderResource::SsrHistory, pb.write(b.handle(RenderResource::SsrHistory), Rg::Use::RenderTarget));
                    full = pb.write(full, Rg::Use::RenderTarget);
                },
                [](Rg::PassContext&, const Rg::NoData&) {});
            b.graph().blackboard().set(SsrOutput{ full, {} });
        }

        void declareDecals(FeatureBuilder& b)
        {
            // The Decals bit drops the G-buffer edit, not the HDR clear lighting draws onto.
            if (!setup.disabled.has(EnginePass::Decals))
                addDecalPass(b, [](Rg::PassContext&, const DecalTargets&) {});
            ASSERT_TRUE(addHdrClearPass(b, [](Rg::PassContext&, const HdrTarget&) {}));
        }

        void declareLighting(FeatureBuilder& b)
        {
            if (setup.disabled.has(EnginePass::DeferredLighting))
            {
                b.opaque("Lighting", {}, {}, [](Rg::PassContext&) {});
                return;
            }
            const LightingReads in = lightingReads(b);
            ASSERT_TRUE(in.valid());
            Rg::TextureHandle hdr;
            b.addPass<LightingReads>(
                "Lighting",
                [&](Rg::PassBuilder& pb, LightingReads& d)
                {
                    hdr = declareLightingUses(pb, in, LightingPass::Lighting, d);
                    if (strictReader)
                        pb.read(in.gtao, Rg::Use::PixelSrv);
                },
                [this](Rg::PassContext&, const LightingReads& d)
                {
                    litRan = true;
                    lit    = d;
                });
            b.set(RenderResource::Hdr, hdr);
        }

        void declareLocalVolumes(FeatureBuilder& b)
        {
            if (setup.disabled.has(EnginePass::LocalLights))
            {
                b.opaque("LocalVolumes", {}, {}, [](Rg::PassContext&) {});
                return;
            }
            Rg::TextureHandle hdr = b.handle(RenderResource::Hdr);
            b.addPass<Rg::NoData>(
                "LocalVolumes",
                [&](Rg::PassBuilder& pb, Rg::NoData&) { hdr = pb.readWrite(hdr, Rg::Use::RenderTarget); },
                [this](Rg::PassContext&, const Rg::NoData&) { localRan = true; });
            b.set(RenderResource::Hdr, hdr);
        }

        void declareSky(FeatureBuilder& b)
        {
            if (setup.disabled.has(EnginePass::Sky))
                return;
            addHdrDrawPass(b, "Sky", skyUses(), [](Rg::PassContext&, const HdrDrawTargets&) {});
        }

        void declareClouds(FeatureBuilder& b)
        {
            if (setup.disabled.has(EnginePass::Clouds))
                return;
            addHdrDrawPass(b, "Clouds", false, [](Rg::PassContext&, const HdrDrawTargets&) {});
        }

        void declareSsrCapture(FeatureBuilder& b)
        {
            // Match production: the Editor list marks this entry required, so a hook-off frame still
            // adds an empty placeholder and does not publish a capture.
            if (setup.disabled.has(EnginePass::SsrCapture))
            {
                b.opaque("SsrCapture", {}, {}, [](Rg::PassContext&) {});
                return;
            }
            Rg::TextureHandle hdr = b.handle(RenderResource::Hdr);
            Rg::TextureHandle dst = b.graph().importTexture("DE.Ssr.Capture", absTex(1280, 720), fakeResource(53), Rg::Layout::ShaderResource, Rg::Layout::Undefined);
            b.addPass<Rg::NoData>(
                "SsrCapture",
                [&](Rg::PassBuilder& pb, Rg::NoData&)
                {
                    pb.read(hdr, Rg::Use::PixelSrv);
                    dst = pb.write(dst, Rg::Use::RenderTarget);
                },
                [](Rg::PassContext&, const Rg::NoData&) {});
            b.graph().blackboard().set(SsrCaptureOutput{ dst });
        }

        void declareUnderwater(FeatureBuilder& b)
        {
            if (setup.disabled.has(EnginePass::Underwater))
                return;
            addHdrDrawPass(b, "Underwater", waterUses(), [](Rg::PassContext&, const HdrDrawTargets&) {});
        }

        void declarePost(FeatureBuilder& b)
        {
            Rg::TextureHandle hdr  = b.handle(RenderResource::Hdr);
            Rg::TextureHandle post = b.handle(RenderResource::Post);
            Rg::TextureHandle swap = b.handle(RenderResource::Swap);
            if (!setup.disabled.has(EnginePass::AutoExposure))
            {
                b.addPass<Rg::NoData>(
                    "Post.AutoExposure.Reduce",
                    [&](Rg::PassBuilder& pb, Rg::NoData&)
                    {
                        pb.flags(Rg::PassFlags::SideEffect);
                        pb.read(hdr, Rg::Use::PixelSrv);
                    },
                    [](Rg::PassContext&, const Rg::NoData&) {});
            }
            if (!setup.disabled.has(EnginePass::Bloom))
            {
                b.addPass<Rg::NoData>(
                    "Post.Bloom.Composite",
                    [&](Rg::PassBuilder& pb, Rg::NoData&) { hdr = pb.readWrite(hdr, Rg::Use::RenderTarget); },
                    [](Rg::PassContext&, const Rg::NoData&) {});
                b.set(RenderResource::Hdr, hdr);
            }
            if (taaRequested)
            {
                const Rg::TextureHandle histCurr = b.handle(RenderResource::TaaHistory);
                const Rg::TextureHandle histPrev = b.prev(RenderResource::TaaHistory);
                Rg::TextureHandle       histOut;
                b.addPass<Rg::NoData>(
                    "Post.Taa",
                    [&](Rg::PassBuilder& pb, Rg::NoData&)
                    {
                        pb.read(hdr, Rg::Use::PixelSrv);
                        if (histPrev.valid())
                            pb.read(histPrev, Rg::Use::PixelSrv);
                        post    = pb.write(post, Rg::Use::RenderTarget);
                        histOut = pb.write(histCurr, Rg::Use::RenderTarget);
                    },
                    [](Rg::PassContext&, const Rg::NoData&) {});
                b.set(RenderResource::Post, post);
                b.set(RenderResource::TaaHistory, histOut);
            }
            if (!setup.disabled.has(EnginePass::MotionBlur))
            {
                Rg::TextureHandle& src = taaRequested ? post : hdr;
                Rg::TextureHandle& dst = taaRequested ? hdr : post;
                b.addPass<Rg::NoData>(
                    "Post.MotionBlur",
                    [&](Rg::PassBuilder& pb, Rg::NoData&)
                    {
                        pb.read(src, Rg::Use::PixelSrv);
                        dst = pb.write(dst, Rg::Use::RenderTarget);
                    },
                    [](Rg::PassContext&, const Rg::NoData&) {});
                b.set(taaRequested ? RenderResource::Hdr : RenderResource::Post, dst);
            }
            // Tone reads Post only when exactly one of TAA and motion blur ran: TAA-then-blur writes back to HDR.
            const Rg::TextureHandle toneSrc = (taaRequested != !setup.disabled.has(EnginePass::MotionBlur)) ? b.handle(RenderResource::Post) : b.handle(RenderResource::Hdr);
            b.addPass<Rg::NoData>(
                "Post.Tonemap",
                [&](Rg::PassBuilder& pb, Rg::NoData&)
                {
                    pb.read(toneSrc.valid() ? toneSrc : hdr, Rg::Use::PixelSrv);
                    swap = pb.write(swap, Rg::Use::RenderTarget);
                },
                [](Rg::PassContext&, const Rg::NoData&) {});
            b.set(RenderResource::Swap, swap);
        }

        std::string resName(Rg::TextureHandle h) const
        {
            return h.valid() ? graph.resourceInfo(h.index).name : "";
        }
    };
} // namespace

TEST(FrameSetup, DisabledGtaoAndSsrBindTheFallbacks)
{
    SetupFrame f;
    Hook       h;
    ASSERT_TRUE(f.hooks.add(&h));
    ASSERT_TRUE(f.run(0.0)) << errorText(f.graph);
    EXPECT_EQ(f.resName(f.lit.ao), "DE.Gtao.AoCompose");
    EXPECT_EQ(f.resName(f.lit.ssr), "DE.Ssr.Full");
    h.disable = kGtao | kSsr;
    ASSERT_TRUE(f.run(1.0)) << errorText(f.graph);
    EXPECT_TRUE(f.litRan);
    EXPECT_EQ(f.resName(f.lit.ao), "Ao"); // the authored AO: GTAO's term is 1
    EXPECT_FALSE(f.lit.ssr.valid());      // the pass binds the SSR dummy
    EXPECT_EQ(f.graph.findResource("DE.Gtao.AoCompose"), Rg::kInvalidIndex);
    EXPECT_EQ(f.graph.findResource("DE.Ssr.Full"), Rg::kInvalidIndex);
    EXPECT_EQ(f.graph.findPass("Gtao.Upsample"), Rg::kNoPass);
    EXPECT_NE(f.graph.findPass("Gtao"), Rg::kNoPass); // the entry keeps its place
    EXPECT_TRUE(f.features.orderOk());
}

TEST(FrameSetup, DisabledShadowsAddNoPassAndLightingBindsTheFallback)
{
    SetupFrame f;
    Hook       h;
    ASSERT_TRUE(f.hooks.add(&h));
    ASSERT_TRUE(f.run(0.0)) << errorText(f.graph);
    EXPECT_NE(f.graph.findPass("Shadows"), Rg::kNoPass);
    EXPECT_NE(f.graph.findPass("LocalShadows"), Rg::kNoPass);
    EXPECT_TRUE(f.lit.shadow.valid());
    h.disable = kDir | kLocal;
    ASSERT_TRUE(f.run(1.0)) << errorText(f.graph);
    EXPECT_TRUE(f.litRan);
    EXPECT_EQ(f.graph.findPass("Shadows"), Rg::kNoPass);
    EXPECT_EQ(f.graph.findPass("LocalShadows"), Rg::kNoPass);
    EXPECT_FALSE(f.lit.shadow.valid());
    EXPECT_FALSE(f.lit.shadow.disabledOutput());
    EXPECT_TRUE(f.features.handle(RenderResource::Shadow).disabledOutput());
    EXPECT_TRUE(f.features.handle(RenderResource::LocalShadow).disabledOutput());
    EXPECT_STREQ(f.graph.disabledOutputName(f.features.handle(RenderResource::Shadow)), "Shadows");
    EXPECT_STREQ(f.graph.disabledOutputName(f.features.handle(RenderResource::LocalShadow)), "LocalShadows");
    EXPECT_TRUE(f.features.orderOk());
}

TEST(FrameSetup, EmptyHookLeavesShadowPassesOn)
{
    SetupFrame f;
    Hook       empty;
    ASSERT_TRUE(f.hooks.add(&empty));
    ASSERT_TRUE(f.run(0.0)) << errorText(f.graph);
    EXPECT_FALSE(f.setup.disabled.any());
    EXPECT_NE(f.graph.findPass("Shadows"), Rg::kNoPass);
    EXPECT_NE(f.graph.findPass("LocalShadows"), Rg::kNoPass);
    EXPECT_TRUE(f.lit.shadow.valid());
}

TEST(FrameSetup, FeatureBuilderStoresADisabledOutput)
{
    Rg::RenderGraph g;
    FeatureFrame    features;
    uint32_t        count = 0;
    const FeaturePass* list = sandboxFeatures(count);
    g.beginFrame(64, 64);
    features.begin(list, count);
    features.importResource(g, RenderResource::Shadow, fakeResource(2), absTex(64, 64), Rg::Layout::DepthStencilWrite);
    const Rg::TextureHandle imported = features.handle(RenderResource::Shadow);
    EXPECT_TRUE(imported.valid());
    for (uint32_t i = 0; i < count; ++i)
        if (list[i].id != PassId::Shadows)
            features.set(list[i].id, [](Rg::PassContext&) {});
    features.declare(PassId::Shadows, [imported](FeatureBuilder& b) {
        const Rg::TextureHandle off = b.graph().disabledOutput("Shadows");
        EXPECT_FALSE(off.valid());
        b.set(RenderResource::Shadow, off);
        EXPECT_TRUE(b.handle(RenderResource::Shadow).disabledOutput());
        EXPECT_STREQ(b.graph().disabledOutputName(b.handle(RenderResource::Shadow)), "Shadows");
        EXPECT_EQ(b.imported(RenderResource::Shadow), imported);
    });
    ASSERT_TRUE(features.compileFrame(g, "test")) << errorText(g);
    EXPECT_TRUE(features.orderOk());
    EXPECT_TRUE(features.handle(RenderResource::Shadow).disabledOutput());
    EXPECT_NE(features.handle(RenderResource::Shadow), imported);
    EXPECT_EQ(features.imported(RenderResource::Shadow), imported);
}

TEST(FrameSetup, DisabledDirectionalShadowStillReadsTheImportedMapForLayout)
{
    // Translucent materials keep the real cascade SRV on the material heap; the graph must still
    // declare a PixelSrv read of the imported map so it leaves depth-write, even though the exec
    // binds the null view (the published handle is a disabled output).
    Rg::RenderGraph    g;
    FeatureFrame       features;
    uint32_t           count = 0;
    const FeaturePass* list  = sandboxFeatures(count);
    g.beginFrame(64, 64);
    features.begin(list, count);
    features.importResource(g, RenderResource::Shadow, fakeResource(2), absTex(64, 64), Rg::Layout::DepthStencilWrite, {}, Rg::Layout::DepthStencilWrite);
    features.importResource(g, RenderResource::Depth, fakeResource(3), absTex(64, 64, Rg::Test::kD32), Rg::Layout::DepthStencilWrite);
    features.importResource(g, RenderResource::Hdr, fakeResource(4), absTex(64, 64), Rg::Layout::RenderTarget);
    const Rg::TextureHandle imported = features.imported(RenderResource::Shadow);
    ASSERT_TRUE(imported.valid());
    HdrDrawTargets got{};
    bool           ran = false;
    for (uint32_t i = 0; i < count; ++i)
        if (list[i].id != PassId::Shadows && list[i].id != PassId::Translucent)
            features.set(list[i].id, [](Rg::PassContext&) {});
    features.declare(PassId::Shadows, [](FeatureBuilder& b) { b.set(RenderResource::Shadow, b.graph().disabledOutput("Shadows")); });
    features.declare(PassId::Translucent,
                     [&](FeatureBuilder& b)
                     {
                         ASSERT_TRUE(addHdrDrawPass(b, "Translucent", translucentUses(),
                                                    [&](Rg::PassContext&, const HdrDrawTargets& d)
                                                    {
                                                        got = d;
                                                        ran = true;
                                                    }));
                     });
    ASSERT_TRUE(features.compileFrame(g, "test")) << errorText(g);
    EXPECT_TRUE(features.orderOk());
    std::string json;
    g.dumpJson(json);
    ASSERT_NE(json.find("\"name\":\"Translucent\""), std::string::npos) << json;
    const std::string access = "\"resource\":" + std::to_string(imported.index) + ",\"version\":" + std::to_string(imported.version) +
                               ",\"use\":\"PixelSrv\",\"read\":true";
    EXPECT_NE(json.find(access), std::string::npos) << "imported " << imported.index << "@" << imported.version << "\n" << json;
    EXPECT_TRUE(g.execute());
    EXPECT_TRUE(ran);
    EXPECT_FALSE(got.shadow.valid());
    EXPECT_FALSE(got.shadow.disabledOutput());
}

TEST(FrameSetup, ReaderWithoutTryReadFailsTheFrame)
{
    SetupFrame f;
    Hook       h;
    h.disable = kGtao;
    ASSERT_TRUE(f.hooks.add(&h));
    f.strictReader = true;
    EXPECT_FALSE(f.run(0.0));
    EXPECT_TRUE(f.graph.hasError(Rg::ErrorCode::DisabledOutputUse));
    h.disable = 0;
    EXPECT_TRUE(f.run(1.0)) << errorText(f.graph);
}

TEST(FrameSetup, HistoryIsInvalidOnceWhenAPassComesBack)
{
    SetupFrame f;
    Hook       h;
    ASSERT_TRUE(f.hooks.add(&h));
    const uint32_t masks[]    = { 0, 0, kGtao, kGtao, 0, 0, 0, kSsr, 0, kGtao | kSsr, 0, kTaa, kTaa, 0, 0 };
    const bool     gtaoPrev[] = { false, true, false, false, false, true, true, true, true, false, false, true, true, true, true };
    const bool     ssrPrev[]  = { false, true, true, true, true, true, true, false, false, false, false, true, true, true, true };
    const bool     taaPrev[]  = { false, true, true, true, true, true, true, true, true, true, true, false, false, false, true };
    for (size_t i = 0; i < std::size(masks); ++i)
    {
        SCOPED_TRACE(i);
        h.disable = masks[i];
        ASSERT_TRUE(f.run(static_cast<double>(i))) << errorText(f.graph);
        EXPECT_EQ(f.gtaoPrev, gtaoPrev[i]);
        EXPECT_EQ(f.ssrPrev, ssrPrev[i]);
        EXPECT_EQ(f.taaPrev, taaPrev[i]);
    }
}

TEST(FrameSetup, DisabledRemainingPassesDropWorkAndKeepTheHdrClear)
{
    SetupFrame f;
    Hook       h;
    ASSERT_TRUE(f.hooks.add(&h));
    ASSERT_TRUE(f.run(0.0)) << errorText(f.graph);
    EXPECT_TRUE(f.litRan);
    EXPECT_TRUE(f.localRan);
    EXPECT_NE(f.graph.findPass("Decals"), Rg::kNoPass);
    EXPECT_NE(f.graph.findPass("Decals.ClearHdr"), Rg::kNoPass);
    EXPECT_NE(f.graph.findPass("Sky"), Rg::kNoPass);
    EXPECT_NE(f.graph.findPass("Clouds"), Rg::kNoPass);
    EXPECT_NE(f.graph.findPass("SsrCapture"), Rg::kNoPass);
    EXPECT_NE(f.graph.findPass("Underwater"), Rg::kNoPass);
    EXPECT_NE(f.graph.findPass("Post.Bloom.Composite"), Rg::kNoPass);
    EXPECT_NE(f.graph.findPass("Post.Taa"), Rg::kNoPass);
    EXPECT_NE(f.graph.findPass("Post.MotionBlur"), Rg::kNoPass);
    EXPECT_NE(f.graph.findPass("Post.AutoExposure.Reduce"), Rg::kNoPass);
    EXPECT_NE(f.graph.findPass("Post.Tonemap"), Rg::kNoPass);

    h.disable = kDecals | kLighting | kLights | kSky | kClouds | kCapture | kUnder | kBloom | kTaa | kMb | kAe;
    ASSERT_TRUE(f.run(1.0)) << errorText(f.graph);
    EXPECT_FALSE(f.litRan);
    EXPECT_FALSE(f.localRan);
    EXPECT_EQ(f.graph.findPass("Decals"), Rg::kNoPass);
    EXPECT_NE(f.graph.findPass("Decals.ClearHdr"), Rg::kNoPass); // lighting still needs a cleared HDR
    EXPECT_NE(f.graph.findPass("Lighting"), Rg::kNoPass);        // required entry keeps its place
    EXPECT_NE(f.graph.findPass("LocalVolumes"), Rg::kNoPass);
    EXPECT_EQ(f.graph.findPass("Sky"), Rg::kNoPass);
    EXPECT_EQ(f.graph.findPass("Clouds"), Rg::kNoPass);
    EXPECT_NE(f.graph.findPass("SsrCapture"), Rg::kNoPass); // required Editor placeholder
    EXPECT_EQ(f.graph.blackboard().tryGet<SsrCaptureOutput>(), nullptr);
    EXPECT_EQ(f.graph.findPass("Underwater"), Rg::kNoPass);
    EXPECT_EQ(f.graph.findPass("Post.Bloom.Composite"), Rg::kNoPass);
    EXPECT_EQ(f.graph.findPass("Post.Taa"), Rg::kNoPass);
    EXPECT_EQ(f.graph.findPass("Post.MotionBlur"), Rg::kNoPass);
    EXPECT_EQ(f.graph.findPass("Post.AutoExposure.Reduce"), Rg::kNoPass);
    EXPECT_NE(f.graph.findPass("Post.Tonemap"), Rg::kNoPass);
    EXPECT_TRUE(f.features.orderOk());
}

TEST(FrameSetup, EmptyHookLeavesNewlyHonouredPassesOn)
{
    SetupFrame f;
    Hook       empty;
    ASSERT_TRUE(f.hooks.add(&empty));
    ASSERT_TRUE(f.run(0.0)) << errorText(f.graph);
    EXPECT_FALSE(f.setup.disabled.any());
    EXPECT_TRUE(f.litRan);
    EXPECT_TRUE(f.localRan);
    EXPECT_NE(f.graph.findPass("Decals"), Rg::kNoPass);
    EXPECT_NE(f.graph.findPass("Sky"), Rg::kNoPass);
    EXPECT_NE(f.graph.findPass("Post.Taa"), Rg::kNoPass);
    EXPECT_NE(f.graph.findPass("Post.Bloom.Composite"), Rg::kNoPass);
}

TEST(FrameSetup, RandomDisableMasksValidateBindFallbacksAndInvalidateHistoryOnce)
{
    LogCapture     logs;
    const uint32_t bitsPool[] = { 0, kGtao, kSsr, kBloom, kGtao | kSsr, kDir, kLocal, kDir | kLocal, kDir | kGtao, kLocal | kSsr,
                                  kTaa | kSsr, kLighting, kLights, kSky | kClouds, kCapture | kUnder, kDecals, kMb | kAe | kBloom,
                                  kLighting | kLights | kSky, kTaa | kMb | kAe, kEnginePassesHonoured };
    int            returns    = 0;
    for (uint32_t seed = 1; seed <= 24; ++seed)
    {
        SCOPED_TRACE(seed);
        std::mt19937 rng(seed);
        SetupFrame   f;
        Hook         hooks[3];
        hooks[1].hookName = (seed % 3 == 0) ? "" : "Second"; // sometimes unnamed: skipped
        for (Hook& h : hooks)
            ASSERT_TRUE(f.hooks.add(&h));
        bool gtaoWas = false, ssrWas = false, taaWas = false;
        for (int frame = 0; frame < 100; ++frame)
        {
            SCOPED_TRACE(frame);
            uint32_t expect = 0;
            for (int k = 0; k < 3; ++k)
            {
                hooks[k].disable = (rng() % 4 == 0) ? bitsPool[rng() % std::size(bitsPool)] : 0;
                if (!(k == 1 && seed % 3 == 0))
                    expect |= hooks[k].disable;
            }
            expect &= kEnginePassesHonoured;
            ASSERT_TRUE(f.run(frame / 60.0)) << errorText(f.graph);
            std::string why;
            ASSERT_TRUE(f.graph.validateCompiled(&why)) << why;
            ASSERT_EQ(f.setup.disabled.bits, expect);
            const bool gtaoOn   = !(expect & kGtao);
            const bool ssrOn    = !(expect & kSsr);
            const bool dirOn    = !(expect & kDir);
            const bool localOn  = !(expect & kLocal);
            const bool lightOn  = !(expect & kLighting);
            const bool volumesOn= !(expect & kLights);
            const bool decalsOn = !(expect & kDecals);
            const bool skyOn    = !(expect & kSky);
            const bool cloudsOn = !(expect & kClouds);
            const bool capOn    = !(expect & kCapture);
            const bool underOn  = !(expect & kUnder);
            const bool bloomOn  = !(expect & kBloom);
            const bool taaOn    = !(expect & kTaa);
            const bool mbOn     = !(expect & kMb);
            const bool aeOn     = !(expect & kAe);
            EXPECT_EQ(f.litRan, lightOn);
            EXPECT_EQ(f.localRan, volumesOn);
            if (lightOn)
            {
                EXPECT_EQ(f.resName(f.lit.ao), gtaoOn ? "DE.Gtao.AoCompose" : "Ao");
                EXPECT_EQ(f.resName(f.lit.ssr), ssrOn ? "DE.Ssr.Full" : "");
                EXPECT_EQ(f.lit.shadow.valid(), dirOn);
            }
            EXPECT_EQ(f.graph.findPass("Shadows") != Rg::kNoPass, dirOn);
            EXPECT_EQ(f.graph.findPass("LocalShadows") != Rg::kNoPass, localOn);
            EXPECT_EQ(f.features.handle(RenderResource::Shadow).disabledOutput(), !dirOn);
            EXPECT_EQ(f.features.handle(RenderResource::LocalShadow).disabledOutput(), !localOn);
            EXPECT_EQ(f.graph.findPass("Decals") != Rg::kNoPass, decalsOn);
            EXPECT_NE(f.graph.findPass("Decals.ClearHdr"), Rg::kNoPass);
            EXPECT_NE(f.graph.findPass("Lighting"), Rg::kNoPass);
            EXPECT_NE(f.graph.findPass("LocalVolumes"), Rg::kNoPass);
            EXPECT_EQ(f.graph.findPass("Sky") != Rg::kNoPass, skyOn);
            EXPECT_EQ(f.graph.findPass("Clouds") != Rg::kNoPass, cloudsOn);
            EXPECT_NE(f.graph.findPass("SsrCapture"), Rg::kNoPass);
            EXPECT_EQ(f.graph.blackboard().tryGet<SsrCaptureOutput>() != nullptr, capOn);
            EXPECT_EQ(f.graph.findPass("Underwater") != Rg::kNoPass, underOn);
            EXPECT_EQ(f.graph.findPass("Post.Bloom.Composite") != Rg::kNoPass, bloomOn);
            EXPECT_EQ(f.graph.findPass("Post.Taa") != Rg::kNoPass, taaOn);
            EXPECT_EQ(f.graph.findPass("Post.MotionBlur") != Rg::kNoPass, mbOn);
            EXPECT_EQ(f.graph.findPass("Post.AutoExposure.Reduce") != Rg::kNoPass, aeOn);
            EXPECT_NE(f.graph.findPass("Post.Tonemap"), Rg::kNoPass);
            // A history is valid exactly when its pass ran last frame and runs now: invalid once on return.
            EXPECT_EQ(f.gtaoPrev, gtaoOn && gtaoWas);
            EXPECT_EQ(f.ssrPrev, ssrOn && ssrWas);
            EXPECT_EQ(f.taaPrev, taaOn && taaWas);
            returns += (gtaoOn && frame > 0 && !gtaoWas) + (ssrOn && frame > 0 && !ssrWas) + (taaOn && frame > 0 && !taaWas);
            gtaoWas = gtaoOn;
            ssrWas  = ssrOn;
            taaWas  = taaOn;
        }
    }
    EXPECT_GT(returns, 100);
}

TEST(FrameSetup, DisabledSsrCaptureKeepsTheEditorPlaceholderAndPublishesNoCapture)
{
    // editorFeatures marks SsrCapture required: omitting the entry fails the order check; a hook-off
    // frame must still add the empty placeholder and must not publish a capture.
    auto compileEditor = [](bool declarePlaceholder) {
        Rg::RenderGraph    g;
        FeatureFrame       features;
        uint32_t           count = 0;
        const FeaturePass* list  = editorFeatures(count);
        g.beginFrame(64, 64);
        features.begin(list, count);
        for (uint32_t r = 0; r < kRenderResourceCount; ++r)
        {
            const auto res = static_cast<RenderResource>(r);
            features.importResource(g, res, fakeResource(r), absTex(64, 64, res == RenderResource::Depth ? Rg::Test::kD32 : Rg::Test::kRgba16F),
                                    Rg::Layout::RenderTarget);
        }
        for (uint32_t i = 0; i < count; ++i)
        {
            if (list[i].id == PassId::SsrCapture)
                continue;
            features.set(list[i].id, [](Rg::PassContext&) {});
        }
        if (declarePlaceholder)
            features.declare(PassId::SsrCapture, [](FeatureBuilder& b) { b.opaque("SsrCapture", {}, {}, [](Rg::PassContext&) {}); });
        const bool compiled = features.compileFrame(g, "test");
        return std::tuple<bool, bool, bool, bool>(compiled, features.orderOk(), g.findPass("SsrCapture") != Rg::kNoPass,
                                                  g.blackboard().tryGet<SsrCaptureOutput>() != nullptr);
    };
    const auto missing = compileEditor(false);
    EXPECT_TRUE(std::get<0>(missing));
    EXPECT_FALSE(std::get<1>(missing));
    EXPECT_FALSE(std::get<2>(missing));
    const auto kept = compileEditor(true);
    EXPECT_TRUE(std::get<0>(kept));
    EXPECT_TRUE(std::get<1>(kept));
    EXPECT_TRUE(std::get<2>(kept));
    EXPECT_FALSE(std::get<3>(kept));
}

TEST(FrameSetup, DefaultDebugTogglesFoldIntoTheDisabledMask)
{
    DebugRenderState debug; // ssao, decals, clouds default off; bloom/taa/mb/shadows/ssr on
    FrameSetup       setup;
    applyDebugToggles(setup, debug);
    EXPECT_TRUE(setup.disabled.has(EnginePass::Gtao));
    EXPECT_TRUE(setup.disabled.has(EnginePass::Decals));
    EXPECT_TRUE(setup.disabled.has(EnginePass::Clouds));
    EXPECT_FALSE(setup.disabled.has(EnginePass::DirectionalShadows));
    EXPECT_FALSE(setup.disabled.has(EnginePass::LocalShadows));
    EXPECT_FALSE(setup.disabled.has(EnginePass::Ssr));
    EXPECT_FALSE(setup.disabled.has(EnginePass::Bloom));
    EXPECT_FALSE(setup.disabled.has(EnginePass::Taa));
    EXPECT_FALSE(setup.disabled.has(EnginePass::MotionBlur));
    EXPECT_FALSE(setup.disabled.has(EnginePass::DeferredLighting));
    EXPECT_FALSE(setup.disabled.has(EnginePass::LocalLights));
    debug.shadows  = false;
    debug.bloom    = false;
    debug.lighting = false;
    debug.localLights = false;
    applyDebugToggles(setup, debug);
    EXPECT_TRUE(setup.disabled.has(EnginePass::DirectionalShadows));
    EXPECT_TRUE(setup.disabled.has(EnginePass::LocalShadows));
    EXPECT_TRUE(setup.disabled.has(EnginePass::Bloom));
    EXPECT_FALSE(setup.disabled.has(EnginePass::DeferredLighting));
    EXPECT_FALSE(setup.disabled.has(EnginePass::LocalLights));
}

TEST(FrameSetup, ResolveFoldsDebugThenHooksAndAHookCannotClearAToggle)
{
    DebugRenderState debug;
    debug.ssaoEnabled = false;
    debug.bloom       = false;
    FrameView        view;
    view.debug = &debug;
    struct On : IFrameSetupHook
    {
        const char* name() const override { return "On"; }
        void        setupFrame(const FrameView&, FrameSetup& setup) override
        {
            setup.disabled.bits = 0; // try to turn everything back on
            setup.disabled.set(EnginePass::Ssr);
        }
    } on;
    FrameSetupHooks hooks;
    ASSERT_TRUE(hooks.add(&on));
    const FrameSetup s = hooks.resolve(view, 0.0);
    EXPECT_TRUE(s.disabled.has(EnginePass::Gtao));
    EXPECT_TRUE(s.disabled.has(EnginePass::Bloom));
    EXPECT_TRUE(s.disabled.has(EnginePass::Decals));
    EXPECT_TRUE(s.disabled.has(EnginePass::Clouds));
    EXPECT_TRUE(s.disabled.has(EnginePass::Ssr));
}

TEST(FrameSetup, HooksCanOnlyLowerScalarsAndARaiseIsIgnored)
{
    struct Set : IFrameSetupHook
    {
        const char* hookName = "Set";
        float       shadow   = 1.0f;
        float       ao       = 1.0f;
        const char* name() const override { return hookName; }
        void        setupFrame(const FrameView&, FrameSetup& setup) override
        {
            setup.shadowStrength = shadow;
            setup.aoStrength     = ao;
        }
    };
    FrameSetupHooks hooks;
    Set             low, high;
    low.hookName = "Low";
    low.shadow   = 0.5f;
    low.ao       = 0.25f;
    high.hookName = "High";
    high.shadow   = 0.8f; // must not raise
    high.ao       = 1.5f; // clamp then ignore raise
    ASSERT_TRUE(hooks.add(&low));
    ASSERT_TRUE(hooks.add(&high));
    const FrameSetup s = hooks.resolve(FrameView{}, 0.0);
    EXPECT_FLOAT_EQ(s.shadowStrength, 0.5f);
    EXPECT_FLOAT_EQ(s.aoStrength, 0.25f);
    EXPECT_FLOAT_EQ(s.ssrStrength, 1.0f);
    EXPECT_FLOAT_EQ(s.emissiveGain, 1.0f);
}

TEST(FrameSetup, TwoHooksWritingTheSameScalarLogOnce)
{
    LogCapture logs;
    struct Set : IFrameSetupHook
    {
        const char* hookName = "A";
        float       value    = 0.5f;
        const char* name() const override { return hookName; }
        void        setupFrame(const FrameView&, FrameSetup& setup) override { setup.shadowStrength = value; }
    } a, b;
    b.hookName = "B";
    b.value    = 0.2f;
    FrameSetupHooks hooks;
    ASSERT_TRUE(hooks.add(&a));
    ASSERT_TRUE(hooks.add(&b));
    EXPECT_FLOAT_EQ(hooks.resolve(FrameView{}, 0.0).shadowStrength, 0.2f);
    EXPECT_EQ(logsWith("two hooks wrote FrameSetup::shadowStrength"), 1u);
    EXPECT_FLOAT_EQ(hooks.resolve(FrameView{}, 0.1).shadowStrength, 0.2f);
    EXPECT_EQ(logsWith("two hooks wrote FrameSetup::shadowStrength"), 1u); // once
}

TEST(FrameSetup, ColorRemapDefaultsAreIdentityAndLastWriterWins)
{
    EXPECT_EQ(ColorRemap{}, ColorRemap{});
    EXPECT_FLOAT_EQ(ColorRemap{}.strength, 0.0f);
    EXPECT_FLOAT_EQ(ColorRemap{}.m[0][0], 1.0f);
    struct Set : IFrameSetupHook
    {
        const char* hookName = "A";
        float       strength = 0.4f;
        const char* name() const override { return hookName; }
        void        setupFrame(const FrameView&, FrameSetup& setup) override
        {
            setup.albedoRemap.strength = strength;
            setup.albedoRemap.aux      = 1.0f;
        }
    } a, b;
    b.hookName = "B";
    b.strength = 0.7f;
    LogCapture      logs;
    FrameSetupHooks hooks;
    ASSERT_TRUE(hooks.add(&a));
    ASSERT_TRUE(hooks.add(&b));
    const FrameSetup s = hooks.resolve(FrameView{}, 0.0);
    EXPECT_FLOAT_EQ(s.albedoRemap.strength, 0.7f);
    EXPECT_FLOAT_EQ(s.albedoRemap.aux, 1.0f);
    EXPECT_EQ(logsWith("two hooks wrote FrameSetup::albedoRemap"), 1u);
}

TEST(FrameSetup, RandomScalarsAreClampedAndOnlyLowered)
{
    std::mt19937 rng(8 * 1000 + 2);
    std::uniform_real_distribution<float> dist(-0.25f, 1.75f);
    for (int trial = 0; trial < 100; ++trial)
    {
        struct Set : IFrameSetupHook
        {
            const char* hookName = "A";
            float       shadow = 1.0f, ao = 1.0f, ssr = 1.0f, emis = 1.0f;
            const char* name() const override { return hookName; }
            void        setupFrame(const FrameView&, FrameSetup& setup) override
            {
                setup.shadowStrength = shadow;
                setup.aoStrength     = ao;
                setup.ssrStrength    = ssr;
                setup.emissiveGain   = emis;
            }
        } a, b;
        b.hookName = "B";
        a.shadow = dist(rng);
        a.ao     = dist(rng);
        a.ssr    = dist(rng);
        a.emis   = dist(rng);
        b.shadow = dist(rng);
        b.ao     = dist(rng);
        b.ssr    = dist(rng);
        b.emis   = dist(rng);
        auto after = [](float dst, float proposed) {
            if (proposed == dst)
                return dst;
            const float c = proposed < 0.0f ? 0.0f : (proposed > 1.0f ? 1.0f : proposed);
            return c < dst ? c : dst;
        };
        auto expect = [&](float first, float second) { return after(after(1.0f, first), second); };
        FrameSetupHooks hooks;
        ASSERT_TRUE(hooks.add(&a));
        ASSERT_TRUE(hooks.add(&b));
        const FrameSetup s = hooks.resolve(FrameView{}, 0.0);
        EXPECT_FLOAT_EQ(s.shadowStrength, expect(a.shadow, b.shadow)) << trial;
        EXPECT_FLOAT_EQ(s.aoStrength, expect(a.ao, b.ao)) << trial;
        EXPECT_FLOAT_EQ(s.ssrStrength, expect(a.ssr, b.ssr)) << trial;
        EXPECT_FLOAT_EQ(s.emissiveGain, expect(a.emis, b.emis)) << trial;
    }
}

TEST(FrameSetup, WriteSurfaceTagIsOrAndLutTakesTheLastWriter)
{
    LogCapture logs;
    struct TagHook : Hook
    {
        bool         write = false;
        const float* lut   = nullptr;
        uint32_t     lutN  = 0;
        void setupFrame(const FrameView& view, FrameSetup& setup) override
        {
            Hook::setupFrame(view, setup);
            setup.writeSurfaceTag = write;
            setup.surfaceTagLut     = lut;
            setup.surfaceTagLutSize = lutN;
        }
    };
    float lutA[256]{};
    float lutB[256]{};
    lutA[0] = 1.0f;
    lutB[0] = 2.0f;
    FrameSetupHooks hooks;
    TagHook         a, b;
    a.hookName = "A";
    b.hookName = "B";
    a.write    = true;
    a.lut      = lutA;
    a.lutN     = 256;
    b.write    = false;
    b.lut      = lutB;
    b.lutN     = 256;
    ASSERT_TRUE(hooks.add(&a));
    ASSERT_TRUE(hooks.add(&b));
    const FrameSetup s = hooks.resolve(FrameView{}, 0.0);
    EXPECT_TRUE(s.writeSurfaceTag);
    EXPECT_EQ(s.surfaceTagLut, lutB);
    EXPECT_EQ(s.surfaceTagLutSize, 256u);
    EXPECT_EQ(logsWith("two hooks wrote FrameSetup::surfaceTagLut"), 1u);
}
