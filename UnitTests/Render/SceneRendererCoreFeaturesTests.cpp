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
#include <string>
#include <vector>

// registerCoreFeatures: the Decals, Gtao, Ssr, Lighting and LocalVolumes entries as render features
// declared on a host list from a SceneRenderer without pipelines (setup only; nothing executes).

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

    constexpr PassId kCore[] = { PassId::Decals, PassId::Gtao, PassId::Ssr, PassId::Lighting, PassId::LocalVolumes };

    bool isCore(PassId id)
    {
        return std::find(std::begin(kCore), std::end(kCore), id) != std::end(kCore);
    }

    // The compiled Sandbox order of the core entries. Every other list entry is an opaque host pass.
    std::vector<PassId> coreOrder(const RenderFeatureRegistry& registry, Rg::RenderGraph& graph, FeatureFrame& features)
    {
        uint32_t           count = 0;
        const FeaturePass* list  = sandboxFeatures(count);
        graph.beginFrame(1280, 720);
        features.begin(list, count);
        for (uint32_t r = 0; r < kRenderResourceCount; ++r)
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
} // namespace

TEST(SceneRendererCoreFeatures, RegistersTheFiveEntriesByStage)
{
    SceneRenderer         scene;
    RenderFeatureRegistry registry;
    registerCoreFeatures(registry, scene);
    ASSERT_EQ(registry.count(), 5u);
    const FeatureStage stages[] = { FeatureStage::PostGBuffer, FeatureStage::PostGBuffer, FeatureStage::PostGBuffer, FeatureStage::Lighting, FeatureStage::Lighting };
    for (uint32_t i = 0; i < 5; ++i)
    {
        EXPECT_EQ(registry.feature(i)->pass(), kCore[i]) << i;
        EXPECT_EQ(registry.feature(i)->stage(), stages[i]) << i;
        EXPECT_STREQ(registry.feature(i)->name(), passName(kCore[i])) << i;
    }

    ErrorCapture errors;
    registerCoreFeatures(registry, scene);
    EXPECT_EQ(registry.count(), 5u);
    EXPECT_EQ(g_errors.size(), 5u);
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
    SceneRenderer         scene;
    RenderFeatureRegistry registry;
    registerCoreFeatures(registry, scene);
    Renderer          renderer(RendererTestDevice{});
    FeatureCreateInfo info;
    info.renderer = &renderer;
    ASSERT_TRUE(registry.createAll(info));

    World                world;
    Camera3D             camera;
    GtaoSettings         gtao;
    SsrSettings          ssr;
    DeferredResolveInput resolve;
    CoreFrameInputs      in;
    in.world   = &world;
    in.camera  = &camera;
    in.gtao    = &gtao;
    in.ssr     = &ssr;
    in.resolve = &resolve;
    scene.setCoreFrame(in);

    Rg::RenderGraph graph;
    FeatureFrame    features;
    EXPECT_EQ(coreOrder(registry, graph, features), (std::vector<PassId>(std::begin(kCore), std::end(kCore))));
    EXPECT_TRUE(features.orderOk());
}

TEST(SceneRendererCoreFeatures, MissingFrameInputsKeepEmptyEntriesAndLogOnce)
{
    ErrorCapture          errors;
    SceneRenderer         scene;
    RenderFeatureRegistry registry;
    registerCoreFeatures(registry, scene);
    Renderer          renderer(RendererTestDevice{});
    FeatureCreateInfo info;
    info.renderer = &renderer;
    ASSERT_TRUE(registry.createAll(info));

    for (int frame = 0; frame < 2; ++frame)
    {
        g_errors.clear();
        Rg::RenderGraph graph;
        FeatureFrame    features;
        EXPECT_EQ(coreOrder(registry, graph, features), (std::vector<PassId>(std::begin(kCore), std::end(kCore)))) << frame;
        EXPECT_TRUE(features.orderOk()) << frame;
        EXPECT_EQ(graph.passCount(), features.compiledOrder().size()) << frame;
        size_t missing = 0;
        for (const std::string& e : g_errors)
            if (e.find("missing a frame input") != std::string::npos)
                ++missing;
        EXPECT_EQ(missing, frame == 0 ? 5u : 0u) << frame;
    }
}

TEST(SceneRendererCoreFeatures, EachEntryNeedsOnlyItsOwnInputs)
{
    World                world;
    Camera3D             camera;
    GtaoSettings         gtao;
    SsrSettings          ssr;
    DeferredResolveInput resolve;
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
        { "ssrEnv", {} },
    };
    for (const Case& c : cases)
    {
        ErrorCapture          errors;
        SceneRenderer         scene;
        RenderFeatureRegistry registry;
        registerCoreFeatures(registry, scene);
        Renderer          renderer(RendererTestDevice{});
        FeatureCreateInfo info;
        info.renderer = &renderer;
        ASSERT_TRUE(registry.createAll(info));
        CoreFrameInputs in;
        in.world                  = &world;
        in.camera                 = &camera;
        in.gtao                   = &gtao;
        in.ssr                    = &ssr;
        in.resolve                = &resolve;
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
        scene.setCoreFrame(in);

        Rg::RenderGraph graph;
        FeatureFrame    features;
        EXPECT_EQ(coreOrder(registry, graph, features), (std::vector<PassId>(std::begin(kCore), std::end(kCore)))) << c.cleared;
        std::vector<std::string> missing;
        for (const std::string& e : g_errors)
        {
            const size_t at = e.find(" is missing a frame input");
            if (at != std::string::npos)
                missing.push_back(e.substr(std::string("SceneRenderer: ").size(), at - std::string("SceneRenderer: ").size()));
        }
        EXPECT_EQ(missing, c.missing) << c.cleared;
    }
}
