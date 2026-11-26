#include <gtest/gtest.h>

#include "Core/Log.h"
#include "Render/FrameFeatures.h"
#include "Render/FrameRecord.h"
#include "Render/FrameView.h"
#include "Render/LocalShadowSystem.h"
#include "Render/RenderFeature.h"
#include "Render/RenderGraph.h"
#include "Render/Renderer.h"
#include "Render/ShadowCasterSource.h"
#include "Render/ShadowFeature.h"

#include <algorithm>
#include <string>
#include <vector>

// ShadowFeature without a device: its three entries, where they sit in the frame, how they declare,
// that a frame without a renderer, shadow map or faces asks the caster source for nothing but one
// prepareDirectional, and that only the shadow worker opens the shadow list.

using namespace Dark;

namespace
{
    struct FakeCasters final : IShadowCasterSource
    {
        std::vector<std::string> calls;

        void prepareDirectional() override
        {
            calls.emplace_back("prepare");
        }
        void drawGround(ID3D12GraphicsCommandList*, const Frustum3f&) override
        {
            calls.emplace_back("drawGround");
        }
        void drawCasters(ID3D12GraphicsCommandList*, const ShadowCasterView&) override
        {
            calls.emplace_back("drawCasters");
        }
        bool workerReady() const override
        {
            return true;
        }
        void beginWorkerCapture() override
        {
            calls.emplace_back("beginWorkerCapture");
        }
        bool groundOnWorker() const override
        {
            return false;
        }
        void captureGround(const Frustum3f&) override
        {
            calls.emplace_back("captureGround");
        }
        void captureCasters(const ShadowCasterView&) override
        {
            calls.emplace_back("captureCasters");
        }
        uint32_t retainFoliage(const Frustum3f&) override
        {
            calls.emplace_back("retainFoliage");
            return kNoRetainedFoliage;
        }
        ShadowWorkerDraws workerDraws() override
        {
            calls.emplace_back("workerDraws");
            return {};
        }
    };

    Rg::TextureDesc absDesc(uint32_t format)
    {
        Rg::TextureDesc d;
        d.sizeMode = Rg::SizeMode::Absolute;
        d.width    = 320;
        d.height   = 180;
        d.format   = format;
        return d;
    }

    ID3D12GraphicsCommandList* fakeCmd()
    {
        return reinterpret_cast<ID3D12GraphicsCommandList*>(static_cast<uintptr_t>(0x1000));
    }

    std::vector<std::string> passNames(const Rg::RenderGraph& g)
    {
        std::vector<std::string> names;
        for (uint32_t i = 0; i < g.passCount(); ++i)
            names.emplace_back(g.passName(i));
        return names;
    }

    bool has(const std::vector<std::string>& names, const char* name)
    {
        return std::find(names.begin(), names.end(), name) != names.end();
    }

    // Declares the registry on the Sandbox (or Editor) list with the shadow maps imported. With
    // `ran`, every other list entry is a stub that logs its PassId.
    bool declareFrame(const RenderFeatureRegistry& registry, Rg::RenderGraph& g, FeatureFrame& features, uint32_t& declared, bool editor = false, std::vector<PassId>* ran = nullptr,
                      const FrameView& view = {})
    {
        uint32_t           count = 0;
        const FeaturePass* list  = editor ? editorFeatures(count) : sandboxFeatures(count);
        g.beginFrame(320, 180);
        features.begin(list, count);
        features.importResource(g, RenderResource::Shadow, nullptr, absDesc(40), Rg::Layout::DepthStencilWrite);
        features.importResource(g, RenderResource::LocalShadow, nullptr, absDesc(40));
        declared = registry.declare(features, view);
        for (uint32_t i = 0; ran && i < count; ++i)
        {
            const PassId id = list[i].id;
            if (!registry.find(id))
                features.set(id, [ran, id](Rg::PassContext&) { ran->push_back(id); });
        }
        return features.compileFrame(g, "shadow-feature");
    }
} // namespace

TEST(ShadowFeature, EntriesAreTheFrameCullShadowsAndLocalShadowsEntries)
{
    ShadowFeature feature;
    EXPECT_STREQ(feature.cull().name(), "FrameCull");
    EXPECT_EQ(feature.cull().pass(), PassId::FrameCull);
    EXPECT_EQ(feature.cull().stage(), FeatureStage::PreFrame);
    EXPECT_STREQ(feature.directional().name(), "Shadows");
    EXPECT_EQ(feature.directional().pass(), PassId::Shadows);
    EXPECT_EQ(feature.directional().stage(), FeatureStage::Shadows);
    EXPECT_STREQ(feature.local().name(), "LocalShadows");
    EXPECT_EQ(feature.local().pass(), PassId::LocalShadows);
    EXPECT_EQ(feature.local().stage(), FeatureStage::Shadows);
}

TEST(ShadowFeature, RegisterAddsTheCullDirectionalAndLocalEntriesOnce)
{
    ShadowFeature         feature;
    RenderFeatureRegistry registry;
    registerShadowFeature(registry, feature);
    registerShadowFeature(registry, feature);
    ASSERT_EQ(registry.count(), 3u);
    EXPECT_EQ(registry.feature(0), &feature.cull());
    EXPECT_EQ(registry.feature(1), &feature.directional());
    EXPECT_EQ(registry.feature(2), &feature.local());
}

TEST(ShadowFeature, ListsPutTheEntriesInStageOrderAroundTheGBuffer)
{
    // FeatureStage order: PreFrame, GBuffer, Shadows, PostGBuffer. The lists set the graph order, so
    // each entry must sit after every entry of an earlier stage.
    ShadowFeature feature;
    for (const bool editor : { false, true })
    {
        uint32_t            count = 0;
        const FeaturePass*  list  = editor ? editorFeatures(count) : sandboxFeatures(count);
        std::vector<PassId> ids;
        for (uint32_t i = 0; i < count; ++i)
            ids.push_back(list[i].id);
        const auto at = [&ids](PassId id) { return std::find(ids.begin(), ids.end(), id) - ids.begin(); };
        ASSERT_LT(at(PassId::GBuffer), static_cast<long>(ids.size()));
        EXPECT_LT(at(feature.cull().pass()), at(PassId::GBuffer)) << editor;
        EXPECT_GT(at(feature.directional().pass()), at(PassId::GBuffer)) << editor;
        EXPECT_GT(at(feature.local().pass()), at(feature.directional().pass())) << editor;
        EXPECT_LT(at(feature.local().pass()), at(PassId::Decals)) << editor;
    }
}

TEST(ShadowFeature, CreatesWithoutARendererSoARequiredHostKeepsRunning)
{
    // The shadow maps belong to the host and SceneRenderer: creating the entries cannot fail.
    ShadowFeature         feature;
    RenderFeatureRegistry registry;
    registerShadowFeature(registry, feature);
    EXPECT_TRUE(registry.createAll({}));
    EXPECT_TRUE(registry.isCreated(0));
    EXPECT_TRUE(registry.isCreated(1));
    EXPECT_TRUE(registry.isCreated(2));
    registry.destroyAll();
}

TEST(ShadowFeature, SetFrameRestartsTheDepthViewIndexAfterTheCamera)
{
    ShadowFeature feature;
    EXPECT_EQ(feature.viewIndex(), 1u);
    FakeCasters casters;
    ShadowFrame frame;
    frame.casters = &casters;
    feature.setFrame(frame);
    EXPECT_EQ(feature.frame().casters, &casters);
    EXPECT_EQ(feature.viewIndex(), 1u);
}

TEST(ShadowFeature, DeclaresTheCullAndShadowsPassesAndNoLocalPassWithoutFaces)
{
    ShadowFeature         feature;
    RenderFeatureRegistry registry;
    registerShadowFeature(registry, feature);
    ASSERT_TRUE(registry.createAll({}));

    FakeCasters       casters;
    LocalShadowSystem local; // no faces selected
    for (LocalShadowSystem* localShadows : { static_cast<LocalShadowSystem*>(nullptr), &local })
    {
        feature.bindShadowMaps(nullptr, localShadows, nullptr, nullptr);
        ShadowFrame frame;
        frame.casters = &casters;
        feature.setFrame(frame);

        Rg::RenderGraph g;
        FeatureFrame    features;
        uint32_t        declared = 0;
        ASSERT_TRUE(declareFrame(registry, g, features, declared));
        EXPECT_EQ(declared, 3u);
        const std::vector<std::string> names = passNames(g);
        EXPECT_TRUE(has(names, "FrameCull"));
        EXPECT_TRUE(has(names, "Shadows"));
        EXPECT_FALSE(has(names, "LocalShadows"));
        const std::vector<PassId>& order = features.compiledOrder();
        EXPECT_NE(std::find(order.begin(), order.end(), PassId::Shadows), order.end());
        EXPECT_EQ(std::find(order.begin(), order.end(), PassId::LocalShadows), order.end());
    }
    registry.destroyAll();
}

TEST(ShadowFeature, ExecutesFrameCullBeforeTheGBufferAndTheShadowsAfterIt)
{
    ShadowFeature         feature;
    RenderFeatureRegistry registry;
    registerShadowFeature(registry, feature);
    ASSERT_TRUE(registry.createAll({}));
    for (const bool editor : { false, true })
    {
        FakeCasters casters;
        ShadowFrame frame;
        frame.casters = &casters;
        feature.setFrame(frame);

        Rg::RenderGraph     g;
        FeatureFrame        features;
        uint32_t            declared = 0;
        std::vector<PassId> ran;
        ASSERT_TRUE(declareFrame(registry, g, features, declared, editor, &ran));
        EXPECT_TRUE(features.orderOk()) << editor;
        const std::vector<PassId>& order = features.compiledOrder();
        ASSERT_GE(order.size(), 4u);
        const std::vector<PassId> head(order.begin(), order.begin() + 4);
        EXPECT_EQ(head, (std::vector<PassId>{ PassId::FrameCull, PassId::GBuffer, PassId::Shadows, PassId::Decals })) << editor;
        ASSERT_TRUE(g.execute());
        ASSERT_GE(ran.size(), 2u);
        EXPECT_EQ(ran[0], PassId::GBuffer);
        EXPECT_EQ(ran[1], PassId::Decals);
        // One prepare for the frame, from FrameCull; Shadows does not prepare again.
        EXPECT_EQ(casters.calls, std::vector<std::string>{ "prepare" });
    }
    registry.destroyAll();
}

TEST(ShadowFeature, ShadowsPreparesOnceWhenFrameCullDidNotRun)
{
    ShadowFeature feature;
    FakeCasters   casters;
    ShadowFrame   frame;
    frame.casters = &casters;
    feature.setFrame(frame);
    feature.recordFrameCull(fakeCmd());
    feature.recordFrameCull(fakeCmd());
    feature.recordDirectional(fakeCmd());
    EXPECT_EQ(casters.calls, std::vector<std::string>{ "prepare" });

    // A list without FrameCull: Shadows runs it first, once.
    casters.calls.clear();
    feature.setFrame(frame);
    feature.recordDirectional(fakeCmd());
    EXPECT_EQ(casters.calls, std::vector<std::string>{ "prepare" });

    // setFrame starts a new frame.
    casters.calls.clear();
    feature.setFrame(frame);
    feature.recordFrameCull(fakeCmd());
    EXPECT_EQ(casters.calls, std::vector<std::string>{ "prepare" });
}

TEST(ShadowFeature, ExecutingWithoutAListOnlyPreparesTheCasters)
{
    ShadowFeature         feature;
    RenderFeatureRegistry registry;
    registerShadowFeature(registry, feature);
    ASSERT_TRUE(registry.createAll({}));
    FakeCasters casters;
    ShadowFrame frame;
    frame.casters = &casters;
    feature.setFrame(frame);

    Rg::RenderGraph g;
    FeatureFrame    features;
    uint32_t        declared = 0;
    ASSERT_TRUE(declareFrame(registry, g, features, declared));
    ASSERT_TRUE(g.execute());
    EXPECT_EQ(casters.calls, std::vector<std::string>{ "prepare" });
    registry.destroyAll();
}

TEST(ShadowFeature, WithoutARendererOrShadowMapNothingRecords)
{
    ShadowFeature feature;
    FakeCasters   casters;
    ShadowFrame   frame;
    frame.casters = &casters;
    feature.setFrame(frame);
    feature.recordDirectional(fakeCmd());
    feature.recordLocal(fakeCmd());
    EXPECT_EQ(casters.calls, std::vector<std::string>{ "prepare" });
    EXPECT_EQ(feature.viewIndex(), 1u);

    // No caster source either: still nothing to record and nothing to crash on.
    feature.setFrame(ShadowFrame{});
    feature.recordDirectional(fakeCmd());
    feature.recordLocal(fakeCmd());
    EXPECT_EQ(feature.viewIndex(), 1u);
}

namespace
{
    std::vector<std::string> g_renderLogs;

    void captureRenderLog(LogLevel, LogCategory category, const char* message)
    {
        if (category == LogCategory::Render)
            g_renderLogs.emplace_back(message);
    }
} // namespace

TEST(ShadowFeature, WithoutTheWorkerTheShadowListIsNotOpened)
{
    // The test shell has no device, so opening the shadow list would log an error.
    Renderer renderer(RendererTestDevice{});
    ASSERT_TRUE(renderer.shadowSeparateList()) << "the default";
    ASSERT_FALSE(shadowWorker());
    FakeCasters casters;
    ShadowFrame frame;
    frame.renderer = &renderer;
    frame.casters  = &casters;
    ShadowFeature feature;
    feature.setFrame(frame);

    g_renderLogs.clear();
    Log::addCapture(&captureRenderLog);
    feature.recordFrameCull(fakeCmd());
    feature.recordDirectional(fakeCmd());
    Log::removeCapture(&captureRenderLog);
    EXPECT_TRUE(g_renderLogs.empty()) << g_renderLogs.front();
    EXPECT_EQ(casters.calls, std::vector<std::string>{ "prepare" });
}

TEST(ShadowFeature, CastersAreCapturedOnlyForTheWorker)
{
    EXPECT_FALSE(capturingShadowCasters());
    pushShadowCaster(nullptr, Math::Matrix4f::IDENTITY);
    EXPECT_FALSE(capturingShadowCasters());
}

TEST(ShadowFeature, UnboundMapsAddNoDepthViewsAndIgnoreTheHostCalls)
{
    ShadowFeature feature;
    int           views = 0;
    feature.forEachDepthView([&views](const Math::Matrix4f&, const Frustum3f&) { ++views; });
    EXPECT_EQ(views, 0);
    feature.setDebugEnabled(false);
    EXPECT_EQ(feature.skinnedShadowPipeline(), nullptr);
}

TEST(ShadowFeature, BoundMapsTakeTheDebugToggleAndAddNoViewsWithoutCascadesOrFaces)
{
    ShadowSystem      shadows; // not created: no map, so no cascade views
    LocalShadowSystem local;   // no faces selected
    ShadowFeature     feature;
    feature.bindShadowMaps(&shadows, &local, nullptr, nullptr);
    EXPECT_EQ(&feature.shadowMaps(), &shadows);
    EXPECT_EQ(&feature.localShadowMaps(), &local);
    int views = 0;
    feature.forEachDepthView([&views](const Math::Matrix4f&, const Frustum3f&) { ++views; });
    EXPECT_EQ(views, 0);
    feature.setDebugEnabled(false);
    EXPECT_FALSE(shadows.debugEnabled());
    EXPECT_FALSE(local.debugEnabled());
    feature.setDebugEnabled(true);
    EXPECT_TRUE(shadows.debugEnabled());
    EXPECT_TRUE(local.debugEnabled());
}

TEST(ShadowFeature, DisableThisFrameClearsEnabledAndStrength)
{
    ShadowSystem shadows;
    EXPECT_FALSE(shadows.enabled());
    shadows.disableThisFrame();
    EXPECT_FALSE(shadows.enabled());
    EXPECT_EQ(shadows.strength(), 0.0f);
}

TEST(ShadowFeature, ADisabledDirectionalBitAddsNoShadowsPassAndPublishesADisabledOutput)
{
    ShadowFeature         feature;
    RenderFeatureRegistry registry;
    registerShadowFeature(registry, feature);
    ASSERT_TRUE(registry.createAll({}));
    FrameView view;
    view.setup.disabled.set(EnginePass::DirectionalShadows);
    Rg::RenderGraph g;
    FeatureFrame    features;
    uint32_t        declared = 0;
    ASSERT_TRUE(declareFrame(registry, g, features, declared, false, nullptr, view));
    const std::vector<std::string> names = passNames(g);
    EXPECT_TRUE(has(names, "FrameCull"));
    EXPECT_FALSE(has(names, "Shadows"));
    EXPECT_TRUE(features.handle(RenderResource::Shadow).disabledOutput());
    EXPECT_STREQ(g.disabledOutputName(features.handle(RenderResource::Shadow)), "Shadows");
    EXPECT_EQ(std::find(features.compiledOrder().begin(), features.compiledOrder().end(), PassId::Shadows), features.compiledOrder().end());
    registry.destroyAll();
}

TEST(ShadowFeature, ADisabledLocalBitAddsNoLocalShadowsPassAndPublishesADisabledOutput)
{
    ShadowFeature         feature;
    RenderFeatureRegistry registry;
    registerShadowFeature(registry, feature);
    ASSERT_TRUE(registry.createAll({}));
    LocalShadowSystem local;
    feature.bindShadowMaps(nullptr, &local, nullptr, nullptr);
    FrameView view;
    view.setup.disabled.set(EnginePass::LocalShadows);
    Rg::RenderGraph g;
    FeatureFrame    features;
    uint32_t        declared = 0;
    ASSERT_TRUE(declareFrame(registry, g, features, declared, false, nullptr, view));
    EXPECT_FALSE(has(passNames(g), "LocalShadows"));
    EXPECT_TRUE(features.handle(RenderResource::LocalShadow).disabledOutput());
    EXPECT_STREQ(g.disabledOutputName(features.handle(RenderResource::LocalShadow)), "LocalShadows");
    registry.destroyAll();
}

TEST(ShadowFeature, AnEmptySetupStillDeclaresTheShadowsPass)
{
    ShadowFeature         feature;
    RenderFeatureRegistry registry;
    registerShadowFeature(registry, feature);
    ASSERT_TRUE(registry.createAll({}));
    FrameView view; // no bits: an empty hook's resolved setup
    Rg::RenderGraph g;
    FeatureFrame    features;
    uint32_t        declared = 0;
    ASSERT_TRUE(declareFrame(registry, g, features, declared, false, nullptr, view));
    EXPECT_TRUE(has(passNames(g), "Shadows"));
    EXPECT_FALSE(features.handle(RenderResource::Shadow).disabledOutput());
    registry.destroyAll();
}
