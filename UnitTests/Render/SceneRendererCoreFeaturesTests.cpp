#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <gtest/gtest.h>

#include "Core/Log.h"
#include "ECS/World.h"
#include "Render/FrameFeatures.h"
#include "Render/Renderer.h"
#include "Render/SceneRenderer.h"
#include "Render/ShadowFeature.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

// registerCoreFeatures: the GBuffer, Decals, Gtao, Ssr, Lighting, LocalVolumes, SsrCapture, Translucent,
// Post and Overlay entries as render features declared on a host list from a SceneRenderer without
// pipelines.

using namespace Dark;

namespace
{
    std::vector<std::string> g_errors;
    void                     captureErrors(LogLevel level, LogCategory, const char* message)
    {
        if (level == LogLevel::Error)
            g_errors.emplace_back(message);
    }
    struct ErrorCapture
    {
        ErrorCapture()
        {
            g_errors.clear();
            Log::addCapture(&captureErrors);
        }
        ~ErrorCapture()
        {
            Log::removeCapture(&captureErrors);
        }
    };

    Rg::TextureDesc absDesc(uint32_t format)
    {
        Rg::TextureDesc d;
        d.sizeMode = Rg::SizeMode::Absolute;
        d.width    = 1280;
        d.height   = 720;
        d.format   = format;
        return d;
    }

    // Registry order: stage, then registration.
    constexpr PassId kCore[] = { PassId::GBuffer,      PassId::Decals,     PassId::Gtao,        PassId::Ssr,  PassId::Lighting,
                                 PassId::LocalVolumes, PassId::SsrCapture, PassId::Translucent, PassId::Post, PassId::Overlay };

    bool isCore(PassId id)
    {
        return std::find(std::begin(kCore), std::end(kCore), id) != std::end(kCore);
    }

    std::vector<PassId> ids(std::initializer_list<PassId> l)
    {
        return std::vector<PassId>(l);
    }

    // A registered and created core bundle with every frame input set.
    struct Bundle
    {
        SceneRenderer                         scene;
        RenderFeatureRegistry                 registry;
        Renderer                              renderer{ RendererTestDevice{} };
        World                                 world;
        Camera3D                              camera;
        GtaoSettings                          gtao;
        SsrSettings                           ssr;
        DeferredResolveInput                  resolve;
        DebugTileRequest                      tiles;
        TonemapSettings                       post;
        Math::Matrix4f                        viewProj;
        AutoExposureState                     exposureState;
        AutoExposureResult                    exposureResult;
        AutoExposureSettings                  exposureSettings;
        CoreAutoExposure                      exposure;
        int                                   gbufferRuns     = 0;
        int                                   translucentRuns = 0;
        int                                   beforeRuns      = 0;
        int                                   beforePostRuns  = 0;
        std::function<void(Rg::PassContext&, const WorldDrawContext&)> gbufferDraw     = [this](Rg::PassContext&, const WorldDrawContext&) { ++gbufferRuns; };
        std::function<void(Rg::PassContext&, const WorldDrawContext&)> translucentDraw = [this](Rg::PassContext&, const WorldDrawContext&) { ++translucentRuns; };
        std::function<void(Rg::PassContext&)> beforeCapture   = [this](Rg::PassContext&) { ++beforeRuns; };
        std::function<void(Rg::PassContext&)> beforePostDraw  = [this](Rg::PassContext&) { ++beforePostRuns; };

        Bundle()
        {
            registerCoreFeatures(registry, scene);
            FeatureCreateInfo info;
            info.renderer = &renderer;
            created       = registry.createAll(info);
            exposure.state       = &exposureState;
            exposure.result      = &exposureResult;
            exposure.settings    = &exposureSettings;
            exposure.envExposure = 2.0f;
            exposure.dt          = 1.0f / 60.0f;
        }
        CoreFrameInputs inputs()
        {
            CoreFrameInputs in;
            in.world                = &world;
            in.camera               = &camera;
            in.gtao                 = &gtao;
            in.ssr                  = &ssr;
            in.resolve              = &resolve;
            in.gbuffer              = CoreDrawHook::of(gbufferDraw);
            in.translucent          = CoreDrawHook::of(translucentDraw);
            in.beforeSsrCapture     = CorePassHook::of(beforeCapture);
            in.beforeSsrCaptureName = "SsrCapture.Test";
            in.debugTiles           = &tiles;
            in.post                 = &post;
            in.viewProj             = &viewProj;
            in.autoExposure         = &exposure;
            in.beforePost           = CorePassHook::of(beforePostDraw);
            in.beforePostName       = "Post.Test";
            return in;
        }
        bool created = false;
    };

    // The compiled Sandbox order of the core entries. Every other list entry is an opaque host pass.
    // `imports`: the list resources are imported (otherwise the scene-target passes log and skip).
    std::vector<PassId> coreOrder(const RenderFeatureRegistry& registry, Rg::RenderGraph& graph, FeatureFrame& features, bool imports = true)
    {
        uint32_t           count = 0;
        const FeaturePass* list  = sandboxFeatures(count);
        graph.beginFrame(1280, 720);
        features.begin(list, count);
        for (uint32_t r = 0; imports && r < kRenderResourceCount; ++r)
        {
            const auto res    = static_cast<RenderResource>(r);
            Rg::Layout layout = Rg::Layout::ShaderResource;
            if (res == RenderResource::Swap || res == RenderResource::Hdr)
                layout = Rg::Layout::RenderTarget;
            features.importResource(graph, res, reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(0x100 + r * 0x10)), absDesc(res == RenderResource::Depth ? 40 : 10), layout);
        }
        for (uint32_t i = 0; i < count; ++i)
            if (!isCore(list[i].id))
                features.set(list[i].id, [](Rg::PassContext&) {});
        registry.declare(features, FrameView{});
        std::vector<PassId> out;
        if (!features.compileFrame(graph, "test"))
            return out;
        for (PassId id : features.compiledOrder())
            if (isCore(id) && (out.empty() || out.back() != id))
                out.push_back(id);
        return out;
    }

    bool hasPass(const Rg::RenderGraph& graph, const char* name)
    {
        for (uint32_t p = 0; p < graph.passCount(); ++p)
            if (std::string(graph.passName(p)) == name)
                return true;
        return false;
    }

    size_t errorsWith(const char* text)
    {
        return static_cast<size_t>(std::count_if(g_errors.begin(), g_errors.end(), [text](const std::string& e) { return e.find(text) != std::string::npos; }));
    }

    std::vector<std::string> missingInputs()
    {
        std::vector<std::string> out;
        const std::string        prefix = "SceneRenderer: ";
        for (const std::string& e : g_errors)
        {
            const size_t at = e.find(" is missing a frame input");
            if (at != std::string::npos)
                out.push_back(e.substr(prefix.size(), at - prefix.size()));
        }
        return out;
    }
} // namespace

TEST(SceneRendererCoreFeatures, RegistersTheTenEntriesByStage)
{
    SceneRenderer         scene;
    RenderFeatureRegistry registry;
    registerCoreFeatures(registry, scene);
    ASSERT_EQ(registry.count(), 10u);
    const FeatureStage stages[] = { FeatureStage::GBuffer,  FeatureStage::PostGBuffer, FeatureStage::PostGBuffer, FeatureStage::PostGBuffer, FeatureStage::Lighting,
                                    FeatureStage::Lighting, FeatureStage::Transparent, FeatureStage::Transparent, FeatureStage::Post,        FeatureStage::Overlay };
    for (uint32_t i = 0; i < 10; ++i)
    {
        EXPECT_EQ(registry.feature(i)->pass(), kCore[i]) << i;
        EXPECT_EQ(registry.feature(i)->stage(), stages[i]) << i;
        EXPECT_STREQ(registry.feature(i)->name(), passName(kCore[i])) << i;
    }

    ErrorCapture errors;
    registerCoreFeatures(registry, scene);
    EXPECT_EQ(registry.count(), 10u);
    EXPECT_EQ(g_errors.size(), 10u);
}

TEST(SceneRendererCoreFeatures, CreateNeedsARenderer)
{
    ErrorCapture          errors;
    SceneRenderer         scene;
    RenderFeatureRegistry registry;
    registerCoreFeatures(registry, scene);
    EXPECT_FALSE(registry.createAll(FeatureCreateInfo{}));
    for (uint32_t i = 0; i < registry.count(); ++i)
        EXPECT_FALSE(registry.isCreated(i)) << i;

    Renderer          renderer(RendererTestDevice{});
    FeatureCreateInfo info;
    info.renderer = &renderer;
    EXPECT_TRUE(registry.createAll(info));
    for (uint32_t i = 0; i < registry.count(); ++i)
        EXPECT_TRUE(registry.isCreated(i)) << i;
}

TEST(SceneRendererCoreFeatures, DeclaredEntriesRunInListOrder)
{
    ErrorCapture errors;
    Bundle       bundle;
    ASSERT_TRUE(bundle.created);
    bundle.scene.setCoreFrame(bundle.inputs());

    Rg::RenderGraph graph;
    FeatureFrame    features;
    // Without the SSR pipelines SsrCapture is only the host's pass ahead of it; without the overlay
    // pipeline Overlay adds nothing.
    EXPECT_EQ(coreOrder(bundle.registry, graph, features),
              ids({ PassId::GBuffer, PassId::Decals, PassId::Gtao, PassId::Ssr, PassId::Lighting, PassId::LocalVolumes, PassId::SsrCapture, PassId::Translucent, PassId::Post }));
    EXPECT_TRUE(features.orderOk());
    EXPECT_TRUE(missingInputs().empty());
}

TEST(SceneRendererCoreFeatures, HooksRunAtExecute)
{
    ErrorCapture errors;
    Bundle       bundle;
    ASSERT_TRUE(bundle.created);
    bundle.scene.setCoreFrame(bundle.inputs());

    // No scene targets: GBuffer and Translucent log and skip their draws; the pass ahead of the
    // capture still runs, by its own name.
    Rg::RenderGraph graph;
    FeatureFrame    features;
    (void)coreOrder(bundle.registry, graph, features, false);
    EXPECT_TRUE(hasPass(graph, "SsrCapture.Test"));
    EXPECT_TRUE(hasPass(graph, "Post.Test"));
    EXPECT_EQ(bundle.beforeRuns, 0);
    EXPECT_EQ(bundle.beforePostRuns, 0);
    ASSERT_TRUE(graph.execute());
    EXPECT_EQ(bundle.beforeRuns, 1);
    EXPECT_EQ(bundle.beforePostRuns, 1);
    EXPECT_EQ(bundle.gbufferRuns, 0);
    EXPECT_EQ(bundle.translucentRuns, 0);
}

TEST(SceneRendererCoreFeatures, UnnamedBeforeCaptureHookKeepsTheCapture)
{
    ErrorCapture errors;
    Bundle       bundle;
    ASSERT_TRUE(bundle.created);
    for (int frame = 0; frame < 2; ++frame)
    {
        g_errors.clear();
        CoreFrameInputs in      = bundle.inputs();
        in.beforeSsrCaptureName = nullptr;
        bundle.scene.setCoreFrame(in);
        Rg::RenderGraph graph;
        FeatureFrame    features;
        // Without the SSR pipelines the capture adds nothing: no empty stand-in, no hook pass.
        EXPECT_EQ(coreOrder(bundle.registry, graph, features),
                  ids({ PassId::GBuffer, PassId::Decals, PassId::Gtao, PassId::Ssr, PassId::Lighting, PassId::LocalVolumes, PassId::Translucent, PassId::Post }))
            << frame;
        EXPECT_TRUE(features.orderOk()) << frame;
        for (uint32_t p = 0; p < graph.passCount(); ++p)
            EXPECT_STRNE(graph.passName(p), "SsrCapture") << frame;
        EXPECT_TRUE(missingInputs().empty()) << frame;
        EXPECT_EQ(errorsWith("beforeSsrCapture hook has no name"), frame == 0 ? 1u : 0u) << frame;
    }
    EXPECT_EQ(bundle.beforeRuns, 0);
}

TEST(SceneRendererCoreFeatures, UnnamedBeforePostHookKeepsPost)
{
    ErrorCapture errors;
    Bundle       bundle;
    ASSERT_TRUE(bundle.created);
    for (int frame = 0; frame < 2; ++frame)
    {
        g_errors.clear();
        CoreFrameInputs in = bundle.inputs();
        in.beforePostName  = nullptr;
        bundle.scene.setCoreFrame(in);
        Rg::RenderGraph graph;
        FeatureFrame    features;
        EXPECT_EQ(coreOrder(bundle.registry, graph, features),
                  ids({ PassId::GBuffer, PassId::Decals, PassId::Gtao, PassId::Ssr, PassId::Lighting, PassId::LocalVolumes, PassId::SsrCapture, PassId::Translucent, PassId::Post }))
            << frame;
        EXPECT_TRUE(features.orderOk()) << frame;
        EXPECT_TRUE(hasPass(graph, "Post.Tonemap")) << frame;
        EXPECT_FALSE(hasPass(graph, "Post.Test")) << frame;
        EXPECT_TRUE(missingInputs().empty()) << frame;
        EXPECT_EQ(errorsWith("beforePost hook has no name"), frame == 0 ? 1u : 0u) << frame;
    }
    EXPECT_EQ(bundle.beforePostRuns, 0);
}

TEST(SceneRendererCoreFeatures, PostEntryRunsTheAutoExposureInput)
{
    ErrorCapture errors;
    Bundle       bundle;
    ASSERT_TRUE(bundle.created);
    // Without the metering pipeline the entry applies the artistic exposure: no sample, no correction,
    // the state kept for the frame the meter comes back.
    bundle.exposureState.ev = 0.75f;
    bundle.exposureResult   = { 0.5f, 0.25f, 9.0f };
    bundle.scene.setCoreFrame(bundle.inputs());
    Rg::RenderGraph graph;
    FeatureFrame    features;
    (void)coreOrder(bundle.registry, graph, features);
    EXPECT_EQ(bundle.exposureResult.measuredLuma, 0.0f);
    EXPECT_EQ(bundle.exposureResult.evCorrection, 0.0f);
    EXPECT_EQ(bundle.exposureResult.finalExposure, 2.0f);
    EXPECT_EQ(bundle.exposureState.ev, 0.75f);

    // Without the input the host's result is not touched.
    bundle.exposureResult = { 0.5f, 0.25f, 9.0f };
    CoreFrameInputs in    = bundle.inputs();
    in.autoExposure       = nullptr;
    bundle.scene.setCoreFrame(in);
    Rg::RenderGraph graph2;
    FeatureFrame    features2;
    (void)coreOrder(bundle.registry, graph2, features2);
    EXPECT_EQ(bundle.exposureResult.measuredLuma, 0.5f);
    EXPECT_EQ(bundle.exposureResult.finalExposure, 9.0f);
    EXPECT_TRUE(hasPass(graph2, "Post.Tonemap"));
}

TEST(SceneRendererCoreFeatures, MissingFrameInputsKeepEmptyEntriesAndLogOnce)
{
    ErrorCapture errors;
    Bundle       bundle;
    ASSERT_TRUE(bundle.created);
    const auto needInputs = ids({ PassId::GBuffer, PassId::Decals, PassId::Gtao, PassId::Ssr, PassId::Lighting, PassId::LocalVolumes, PassId::Translucent, PassId::Post });
    for (int frame = 0; frame < 2; ++frame)
    {
        g_errors.clear();
        Rg::RenderGraph graph;
        FeatureFrame    features;
        EXPECT_EQ(coreOrder(bundle.registry, graph, features), needInputs) << frame;
        EXPECT_TRUE(features.orderOk()) << frame;
        EXPECT_EQ(graph.passCount(), features.compiledOrder().size()) << frame;
        EXPECT_EQ(missingInputs().size(), frame == 0 ? needInputs.size() : 0u) << frame;
    }
}

TEST(SceneRendererCoreFeatures, EachEntryNeedsOnlyItsOwnInputs)
{
    struct Case
    {
        const char*              cleared;
        std::vector<std::string> missing;
    };
    const Case cases[] = {
        { "world", { "LocalVolumes" } },
        { "camera", { "Decals", "GTAO", "SSR", "LocalVolumes" } },
        { "gtao", { "GTAO" } },
        { "ssr", { "SSR" } },
        { "resolve", { "Decals", "Lighting", "LocalVolumes" } },
        { "gbuffer", { "GBuffer" } },
        { "translucent", { "Translucent" } },
        { "beforeSsrCaptureName", {} },
        { "beforeSsrCapture", {} },
        { "ssrEnv", {} },
        { "debugTiles", {} },
        { "post", { "Post" } },
        { "viewProj", { "Post" } },
        { "autoExposure", {} },
        { "beforePostName", {} },
        { "beforePost", {} },
    };
    for (const Case& c : cases)
    {
        ErrorCapture errors;
        Bundle       bundle;
        ASSERT_TRUE(bundle.created);
        CoreFrameInputs   in      = bundle.inputs();
        const std::string cleared = c.cleared;
        if (cleared == "world")
            in.world = nullptr;
        else if (cleared == "camera")
            in.camera = nullptr;
        else if (cleared == "gtao")
            in.gtao = nullptr;
        else if (cleared == "ssr")
            in.ssr = nullptr;
        else if (cleared == "resolve")
            in.resolve = nullptr;
        else if (cleared == "gbuffer")
            in.gbuffer = {};
        else if (cleared == "translucent")
            in.translucent = {};
        else if (cleared == "beforeSsrCaptureName")
            in.beforeSsrCaptureName = nullptr;
        else if (cleared == "beforeSsrCapture")
            in.beforeSsrCapture = {};
        else if (cleared == "debugTiles")
            in.debugTiles = nullptr;
        else if (cleared == "post")
            in.post = nullptr;
        else if (cleared == "viewProj")
            in.viewProj = nullptr;
        else if (cleared == "autoExposure")
            in.autoExposure = nullptr;
        else if (cleared == "beforePostName")
            in.beforePostName = nullptr;
        else if (cleared == "beforePost")
            in.beforePost = {};
        bundle.scene.setCoreFrame(in);

        Rg::RenderGraph     graph;
        FeatureFrame        features;
        std::vector<PassId> expected =
            ids({ PassId::GBuffer, PassId::Decals, PassId::Gtao, PassId::Ssr, PassId::Lighting, PassId::LocalVolumes, PassId::SsrCapture, PassId::Translucent, PassId::Post });
        // Without the SSR pipelines, SsrCapture has a pass only for the hook (or its empty pass).
        if (cleared == "beforeSsrCapture" || cleared == "beforeSsrCaptureName")
            expected.erase(std::find(expected.begin(), expected.end(), PassId::SsrCapture));
        EXPECT_EQ(coreOrder(bundle.registry, graph, features), expected) << c.cleared;
        EXPECT_EQ(missingInputs(), c.missing) << c.cleared;
        // An absent optional input never drops the core pass; only a required one leaves the empty pass.
        const bool required = cleared == "post" || cleared == "viewProj";
        EXPECT_EQ(hasPass(graph, "Post.Tonemap"), !required) << c.cleared;
        EXPECT_EQ(hasPass(graph, "Post"), required) << c.cleared;
        EXPECT_EQ(hasPass(graph, "Post.Test"), !required && cleared != "beforePost" && cleared != "beforePostName") << c.cleared;
    }
}

TEST(SceneRendererCoreFeatures, RegisterShadowFeatureBindsTheSceneMaps)
{
    SceneRenderer scene;
    ShadowFeature feature;
    registerShadowFeature(scene, feature);
    ASSERT_EQ(scene.features().count(), 3u);
    EXPECT_EQ(scene.features().feature(0), &feature.cull());
    EXPECT_EQ(scene.features().feature(1), &feature.directional());
    EXPECT_EQ(scene.features().feature(2), &feature.local());
    EXPECT_NE(feature.skinnedShadowPipeline(), nullptr);
    EXPECT_NE(feature.skinRing(), nullptr);
    // The scene's (uncreated) maps: the debug toggle reaches both, and there is no depth view.
    feature.setDebugEnabled(false);
    EXPECT_FALSE(feature.shadowMaps().debugEnabled());
    EXPECT_FALSE(feature.localShadowMaps().debugEnabled());
    int views = 0;
    feature.forEachDepthView([&views](const Math::Matrix4f&, const Frustum3f&) { ++views; });
    EXPECT_EQ(views, 0);
}

TEST(SceneRendererCoreFeatures, DrawHookPassesTheWorldDrawsThrough)
{
    MeshPipeline            mesh;
    MeshPipeline            meshTransparent;
    SkinnedMeshPipeline     skinned;
    SkinnedMeshPipeline     skinnedTransparent;
    SkinningUploadRing      ring;
    const WorldDrawContext  draws{ mesh, meshTransparent, skinned, skinnedTransparent, ring };
    const WorldDrawContext* seen  = nullptr;
    int                     calls = 0;
    auto                    draw  = [&](Rg::PassContext&, const WorldDrawContext& d)
    {
        seen = &d;
        ++calls;
    };
    const CoreDrawHook hook = CoreDrawHook::of(draw);
    ASSERT_NE(hook.fn, nullptr);
    struct Data
    {
        Rg::TextureHandle out;
    };
    Rg::RenderGraph graph;
    graph.beginFrame(4, 4);
    const Rg::TextureHandle back = graph.importBackBuffer(reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(0xB0)), 4, 4, DXGI_FORMAT_B8G8R8A8_UNORM);
    graph.addPass<Data>("Hook.Test", [&](Rg::PassBuilder& b, Data& d) { d.out = b.write(back); }, [&](Rg::PassContext& c, const Data&) { hook(c, draws); });
    ASSERT_TRUE(graph.compile());
    ASSERT_TRUE(graph.execute());
    EXPECT_EQ(calls, 1);
    ASSERT_EQ(seen, &draws);
    EXPECT_EQ(&seen->mesh, &mesh);
    EXPECT_EQ(&seen->skinRing, &ring);
}
