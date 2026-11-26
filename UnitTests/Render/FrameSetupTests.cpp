#include <gtest/gtest.h>

#include "Core/Log.h"
#include "Render/FrameFeatures.h"
#include "Render/FrameSetup.h"
#include "Render/FrameView.h"
#include "Render/HdrDrawInputs.h"
#include "Render/LightingInputs.h"
#include "Render/ShadowInputs.h"
#include "RenderGraphTestUtil.h"

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
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

    constexpr uint32_t kDir   = EnginePassMask::bit(EnginePass::DirectionalShadows);
    constexpr uint32_t kLocal = EnginePassMask::bit(EnginePass::LocalShadows);
    constexpr uint32_t kGtao  = EnginePassMask::bit(EnginePass::Gtao);
    constexpr uint32_t kSsr   = EnginePassMask::bit(EnginePass::Ssr);
    constexpr uint32_t kBloom = EnginePassMask::bit(EnginePass::Bloom);
    constexpr uint32_t kHonoured = kDir | kLocal | kGtao | kSsr;
} // namespace

TEST(FrameSetup, NoHookKeepsTheViewSetup)
{
    FrameSetupHooks hooks;
    FrameView       view;
    EXPECT_EQ(hooks.resolve(view, 0.0), FrameSetup{});
    view.setup.disabled.set(EnginePass::Ssr);
    EXPECT_EQ(hooks.resolve(view, 0.1).disabled.bits, kSsr);
}

TEST(FrameSetup, EmptyHookTurnsNothingOff)
{
    FrameSetupHooks hooks;
    Hook            empty;
    ASSERT_TRUE(hooks.add(&empty));
    const FrameSetup s = hooks.resolve(FrameView{}, 0.0);
    EXPECT_EQ(empty.calls, 1);
    EXPECT_FALSE(s.disabled.any());
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

TEST(FrameSetup, PassesThisBuildDoesNotHonourStayOnAndLogOnce)
{
    LogCapture      logs;
    FrameSetupHooks hooks;
    Hook            h;
    h.disable = kBloom | kGtao;
    ASSERT_TRUE(hooks.add(&h));
    EXPECT_EQ(hooks.resolve(FrameView{}, 0.0).disabled.bits, kGtao);
    EXPECT_EQ(hooks.resolve(FrameView{}, 0.1).disabled.bits, kGtao);
    EXPECT_EQ(logsWith("turning Bloom off is not supported"), 1u);
}

TEST(FrameSetup, ShadowBitsAreHonouredAndUnwiredBitsStillLogOnce)
{
    LogCapture      logs;
    FrameSetupHooks hooks;
    Hook            h;
    h.disable = kDir | kLocal | kBloom;
    ASSERT_TRUE(hooks.add(&h));
    EXPECT_EQ(hooks.resolve(FrameView{}, 0.0).disabled.bits, kDir | kLocal);
    EXPECT_EQ(hooks.resolve(FrameView{}, 0.1).disabled.bits, kDir | kLocal);
    EXPECT_EQ(logsWith("turning Bloom off is not supported"), 1u);
    EXPECT_EQ(logsWith("DirectionalShadows"), 0u);
    EXPECT_EQ(logsWith("LocalShadows"), 0u);
    EXPECT_EQ(kEnginePassesHonoured, kHonoured);
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
        bool            litRan        = false;
        bool            gtaoPrev      = false; // the GTAO history had a valid prev this frame
        bool            ssrPrev       = false;
        bool            gtaoRequested   = false;
        bool            ssrRequested    = false;
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
            shadowRequested = !setup.disabled.has(EnginePass::DirectionalShadows);
            localRequested  = !setup.disabled.has(EnginePass::LocalShadows);
            gtaoPrev = ssrPrev = false;
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
            for (uint32_t i = 0; i < count; ++i)
            {
                const PassId id = list[i].id;
                if (id != PassId::Gtao && id != PassId::Ssr && id != PassId::Lighting && id != PassId::Shadows && id != PassId::LocalShadows)
                    features.set(id, [](Rg::PassContext&) {});
            }
            features.declare(PassId::Shadows, [this](FeatureBuilder& b) { declareShadows(b); });
            features.declare(PassId::LocalShadows, [this](FeatureBuilder& b) { declareLocalShadows(b); });
            features.declare(PassId::Gtao, [this](FeatureBuilder& b) { declareGtao(b); });
            features.declare(PassId::Ssr, [this](FeatureBuilder& b) { declareSsr(b); });
            features.declare(PassId::Lighting, [this](FeatureBuilder& b) { declareLighting(b); });
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

        void declareLighting(FeatureBuilder& b)
        {
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
    const uint32_t masks[]    = { 0, 0, kGtao, kGtao, 0, 0, 0, kSsr, 0, kGtao | kSsr, 0, 0 };
    const bool     gtaoPrev[] = { false, true, false, false, false, true, true, true, true, false, false, true };
    const bool     ssrPrev[]  = { false, true, true, true, true, true, true, false, false, false, false, true };
    for (size_t i = 0; i < std::size(masks); ++i)
    {
        SCOPED_TRACE(i);
        h.disable = masks[i];
        ASSERT_TRUE(f.run(static_cast<double>(i))) << errorText(f.graph);
        EXPECT_EQ(f.gtaoPrev, gtaoPrev[i]);
        EXPECT_EQ(f.ssrPrev, ssrPrev[i]);
    }
}

TEST(FrameSetup, RandomDisableMasksValidateBindFallbacksAndInvalidateHistoryOnce)
{
    LogCapture     logs;
    const uint32_t bitsPool[] = { 0, kGtao, kSsr, kBloom, kGtao | kSsr, kDir, kLocal, kDir | kLocal, kDir | kGtao, kLocal | kSsr,
                                  EnginePassMask::bit(EnginePass::Taa) | kSsr };
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
        bool gtaoWas = false, ssrWas = false;
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
            ASSERT_TRUE(f.litRan);
            const bool gtaoOn  = !(expect & kGtao);
            const bool ssrOn   = !(expect & kSsr);
            const bool dirOn   = !(expect & kDir);
            const bool localOn = !(expect & kLocal);
            EXPECT_EQ(f.resName(f.lit.ao), gtaoOn ? "DE.Gtao.AoCompose" : "Ao");
            EXPECT_EQ(f.resName(f.lit.ssr), ssrOn ? "DE.Ssr.Full" : "");
            EXPECT_EQ(f.graph.findPass("Shadows") != Rg::kNoPass, dirOn);
            EXPECT_EQ(f.graph.findPass("LocalShadows") != Rg::kNoPass, localOn);
            EXPECT_EQ(f.lit.shadow.valid(), dirOn);
            EXPECT_EQ(f.features.handle(RenderResource::Shadow).disabledOutput(), !dirOn);
            EXPECT_EQ(f.features.handle(RenderResource::LocalShadow).disabledOutput(), !localOn);
            // A history is valid exactly when its pass ran last frame and runs now: invalid once on return.
            EXPECT_EQ(f.gtaoPrev, gtaoOn && gtaoWas);
            EXPECT_EQ(f.ssrPrev, ssrOn && ssrWas);
            returns += (gtaoOn && frame > 0 && !gtaoWas) + (ssrOn && frame > 0 && !ssrWas);
            gtaoWas = gtaoOn;
            ssrWas  = ssrOn;
        }
    }
    EXPECT_GT(returns, 100);
}
