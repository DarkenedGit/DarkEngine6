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

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

// registerCoreFeatures: the GBuffer, Decals, Gtao, Ssr, Lighting, LocalVolumes, SsrCapture, Translucent
// and Overlay entries as render features declared on a host list from a SceneRenderer without
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
    constexpr PassId kCore[] = { PassId::GBuffer, PassId::Decals, PassId::Gtao, PassId::Ssr, PassId::Lighting, PassId::LocalVolumes, PassId::SsrCapture, PassId::Translucent, PassId::Overlay };

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
        int                                   gbufferRuns     = 0;
        int                                   translucentRuns = 0;
        int                                   beforeRuns      = 0;
        std::function<void(Rg::PassContext&)> gbufferDraw     = [this](Rg::PassContext&) { ++gbufferRuns; };
        std::function<void(Rg::PassContext&)> translucentDraw = [this](Rg::PassContext&) { ++translucentRuns; };
        std::function<void(Rg::PassContext&)> beforeCapture   = [this](Rg::PassContext&) { ++beforeRuns; };

        Bundle()
        {
            registerCoreFeatures(registry, scene);
            FeatureCreateInfo info;
            info.renderer = &renderer;
            created       = registry.createAll(info);
        }
        CoreFrameInputs inputs()
        {
            CoreFrameInputs in;
            in.world                = &world;
            in.camera               = &camera;
            in.gtao                 = &gtao;
            in.ssr                  = &ssr;
            in.resolve              = &resolve;
            in.gbuffer              = CorePassHook::of(gbufferDraw);
            in.translucent          = CorePassHook::of(translucentDraw);
            in.beforeSsrCapture     = CorePassHook::of(beforeCapture);
            in.beforeSsrCaptureName = "SsrCapture.Test";
            in.debugTiles           = &tiles;
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

TEST(SceneRendererCoreFeatures, RegistersTheNineEntriesByStage)
{
    SceneRenderer         scene;
    RenderFeatureRegistry registry;
    registerCoreFeatures(registry, scene);
    ASSERT_EQ(registry.count(), 9u);
    const FeatureStage stages[] = { FeatureStage::GBuffer,  FeatureStage::PostGBuffer, FeatureStage::PostGBuffer, FeatureStage::PostGBuffer, FeatureStage::Lighting,
                                    FeatureStage::Lighting, FeatureStage::Transparent, FeatureStage::Transparent, FeatureStage::Overlay };
    for (uint32_t i = 0; i < 9; ++i)
    {
        EXPECT_EQ(registry.feature(i)->pass(), kCore[i]) << i;
        EXPECT_EQ(registry.feature(i)->stage(), stages[i]) << i;
        EXPECT_STREQ(registry.feature(i)->name(), passName(kCore[i])) << i;
    }

    ErrorCapture errors;
    registerCoreFeatures(registry, scene);
    EXPECT_EQ(registry.count(), 9u);
    EXPECT_EQ(g_errors.size(), 9u);
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
              ids({ PassId::GBuffer, PassId::Decals, PassId::Gtao, PassId::Ssr, PassId::Lighting, PassId::LocalVolumes, PassId::SsrCapture, PassId::Translucent }));
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
    bool named = false;
    for (uint32_t p = 0; p < graph.passCount(); ++p)
        named = named || std::string(graph.passName(p)) == "SsrCapture.Test";
    EXPECT_TRUE(named);
    EXPECT_EQ(bundle.beforeRuns, 0);
    ASSERT_TRUE(graph.execute());
    EXPECT_EQ(bundle.beforeRuns, 1);
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
        EXPECT_EQ(coreOrder(bundle.registry, graph, features), ids({ PassId::GBuffer, PassId::Decals, PassId::Gtao, PassId::Ssr, PassId::Lighting, PassId::LocalVolumes, PassId::Translucent }))
            << frame;
        EXPECT_TRUE(features.orderOk()) << frame;
        for (uint32_t p = 0; p < graph.passCount(); ++p)
            EXPECT_STRNE(graph.passName(p), "SsrCapture") << frame;
        EXPECT_TRUE(missingInputs().empty()) << frame;
        size_t unnamed = 0;
        for (const std::string& e : g_errors)
            if (e.find("beforeSsrCapture hook has no name") != std::string::npos)
                ++unnamed;
        EXPECT_EQ(unnamed, frame == 0 ? 1u : 0u) << frame;
    }
    EXPECT_EQ(bundle.beforeRuns, 0);
}

TEST(SceneRendererCoreFeatures, MissingFrameInputsKeepEmptyEntriesAndLogOnce)
{
    ErrorCapture errors;
    Bundle       bundle;
    ASSERT_TRUE(bundle.created);
    const auto needInputs = ids({ PassId::GBuffer, PassId::Decals, PassId::Gtao, PassId::Ssr, PassId::Lighting, PassId::LocalVolumes, PassId::Translucent });
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
        bundle.scene.setCoreFrame(in);

        Rg::RenderGraph     graph;
        FeatureFrame        features;
        std::vector<PassId> expected = ids({ PassId::GBuffer, PassId::Decals, PassId::Gtao, PassId::Ssr, PassId::Lighting, PassId::LocalVolumes, PassId::SsrCapture, PassId::Translucent });
        // Without the SSR pipelines, SsrCapture has a pass only for the hook (or its empty pass).
        if (cleared == "beforeSsrCapture" || cleared == "beforeSsrCaptureName")
            expected.erase(std::find(expected.begin(), expected.end(), PassId::SsrCapture));
        EXPECT_EQ(coreOrder(bundle.registry, graph, features), expected) << c.cleared;
        EXPECT_EQ(missingInputs(), c.missing) << c.cleared;
    }
}
