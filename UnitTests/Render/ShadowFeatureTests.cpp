#include <gtest/gtest.h>

#include "Render/FrameFeatures.h"
#include "Render/FrameView.h"
#include "Render/LocalShadowSystem.h"
#include "Render/RenderFeature.h"
#include "Render/RenderGraph.h"
#include "Render/ShadowCasterSource.h"
#include "Render/ShadowFeature.h"

#include <algorithm>
#include <string>
#include <vector>

// ShadowFeature without a device: its two entries, how they declare, and that a frame without a
// renderer, shadow map or faces asks the caster source for nothing but prepareDirectional.

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

    // Declares the registry on the Sandbox list with the shadow maps imported.
    bool declareFrame(const RenderFeatureRegistry& registry, Rg::RenderGraph& g, FeatureFrame& features, uint32_t& declared)
    {
        uint32_t           count = 0;
        const FeaturePass* list  = sandboxFeatures(count);
        g.beginFrame(320, 180);
        features.begin(list, count);
        features.importResource(g, RenderResource::Shadow, nullptr, absDesc(40), Rg::Layout::DepthStencilWrite);
        features.importResource(g, RenderResource::LocalShadow, nullptr, absDesc(40));
        declared = registry.declare(features, FrameView{});
        return features.compileFrame(g, "shadow-feature");
    }
} // namespace

TEST(ShadowFeature, EntriesAreTheShadowsAndLocalShadowsEntries)
{
    ShadowFeature feature;
    EXPECT_STREQ(feature.directional().name(), "Shadows");
    EXPECT_EQ(feature.directional().pass(), PassId::Shadows);
    EXPECT_EQ(feature.directional().stage(), FeatureStage::Shadows);
    EXPECT_STREQ(feature.local().name(), "LocalShadows");
    EXPECT_EQ(feature.local().pass(), PassId::LocalShadows);
    EXPECT_EQ(feature.local().stage(), FeatureStage::Shadows);
}

TEST(ShadowFeature, RegisterAddsTheDirectionalThenTheLocalEntryOnce)
{
    ShadowFeature         feature;
    RenderFeatureRegistry registry;
    registerShadowFeature(registry, feature);
    registerShadowFeature(registry, feature);
    ASSERT_EQ(registry.count(), 2u);
    EXPECT_EQ(registry.feature(0), &feature.directional());
    EXPECT_EQ(registry.feature(1), &feature.local());
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

TEST(ShadowFeature, DeclaresTheShadowsPassAndNoLocalPassWithoutFaces)
{
    ShadowFeature         feature;
    RenderFeatureRegistry registry;
    registerShadowFeature(registry, feature);
    ASSERT_TRUE(registry.createAll({}));

    FakeCasters       casters;
    LocalShadowSystem local; // no faces selected
    for (LocalShadowSystem* localShadows : { static_cast<LocalShadowSystem*>(nullptr), &local })
    {
        ShadowFrame frame;
        frame.casters      = &casters;
        frame.localShadows = localShadows;
        feature.setFrame(frame);

        Rg::RenderGraph g;
        FeatureFrame    features;
        uint32_t        declared = 0;
        ASSERT_TRUE(declareFrame(registry, g, features, declared));
        EXPECT_EQ(declared, 2u);
        const std::vector<std::string> names = passNames(g);
        EXPECT_TRUE(has(names, "Shadows"));
        EXPECT_FALSE(has(names, "LocalShadows"));
        const std::vector<PassId>& order = features.compiledOrder();
        EXPECT_NE(std::find(order.begin(), order.end(), PassId::Shadows), order.end());
        EXPECT_EQ(std::find(order.begin(), order.end(), PassId::LocalShadows), order.end());
    }
    registry.destroyAll();
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

TEST(ShadowFeature, CastersAreCapturedOnlyForTheWorker)
{
    EXPECT_FALSE(capturingShadowCasters());
    pushShadowCaster(nullptr, Math::Matrix4f::IDENTITY);
    EXPECT_FALSE(capturingShadowCasters());
}
