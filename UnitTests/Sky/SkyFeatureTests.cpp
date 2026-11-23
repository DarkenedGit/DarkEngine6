#include <gtest/gtest.h>

#include "Render/FrameFeatures.h"
#include "Render/FrameView.h"
#include "Render/RenderFeature.h"
#include "Render/RenderGraph.h"
#include "Render/Renderer.h"
#include "Sky/SkyFeatures.h"

#include <algorithm>

using namespace Dark;

namespace
{
    Rg::TextureDesc absDesc(uint32_t format)
    {
        Rg::TextureDesc d;
        d.sizeMode = Rg::SizeMode::Absolute;
        d.width    = 320;
        d.height   = 180;
        d.format   = format;
        return d;
    }
} // namespace

TEST(SkyFeature, DrawsInTheSkyStageOnTheSkyEntry)
{
    SkyFeature sky;
    EXPECT_STREQ(sky.name(), "Sky");
    EXPECT_EQ(sky.stage(), FeatureStage::Sky);
    EXPECT_EQ(sky.pass(), PassId::Sky);
}

TEST(SkyFeature, CloudsDrawInTheTransparentStageOnTheCloudsEntry)
{
    CloudVolumeFeature clouds;
    EXPECT_STREQ(clouds.name(), "Clouds");
    EXPECT_EQ(clouds.stage(), FeatureStage::Transparent);
    EXPECT_EQ(clouds.pass(), PassId::Clouds);
}

TEST(SkyFeature, RegisterSkyFeaturesAddsTheSkyThenTheCloudsOnce)
{
    SkyFeatures           sky;
    RenderFeatureRegistry registry;
    registerSkyFeatures(registry, sky);
    registerSkyFeatures(registry, sky);
    ASSERT_EQ(registry.count(), 2u);
    EXPECT_EQ(registry.feature(0), &sky.sky);
    EXPECT_EQ(registry.feature(1), &sky.clouds);
}

TEST(SkyFeature, WithoutARendererTheyFailToCreateAndAreSkipped)
{
    SkyFeatures           sky;
    RenderFeatureRegistry registry;
    registerSkyFeatures(registry, sky);
    EXPECT_FALSE(registry.createAll({}));
    EXPECT_FALSE(registry.isCreated(0));
    EXPECT_FALSE(registry.isCreated(1));
    EXPECT_FALSE(sky.sky.pipeline().isValid());
    EXPECT_FALSE(sky.clouds.pipeline().isValid());
    EXPECT_FALSE(sky.clouds.gpuList().isValid());
}

TEST(SkyFeature, CloudsThatFailToCreateStillCreateAndAddNoPass)
{
    // No device: the cloud pipeline fails to create. Clouds are optional, so create still succeeds
    // (a required-features host keeps running) and the Clouds entry adds no pass.
    Renderer              renderer(RendererTestDevice{});
    CloudVolumeFeature    clouds;
    RenderFeatureRegistry registry;
    registry.add(&clouds);
    FeatureCreateInfo info;
    info.renderer = &renderer;
    EXPECT_TRUE(registry.createAll(info));
    EXPECT_TRUE(registry.isCreated(0));
    EXPECT_FALSE(clouds.enabled());
    EXPECT_FALSE(clouds.pipeline().isValid());
    EXPECT_FALSE(clouds.gpuList().isValid());

    uint32_t           count = 0;
    const FeaturePass* list  = sandboxFeatures(count);
    Rg::RenderGraph    g;
    FeatureFrame       features;
    g.beginFrame(320, 180);
    features.begin(list, count);
    features.importResource(g, RenderResource::Depth, nullptr, absDesc(40));
    features.importResource(g, RenderResource::Hdr, nullptr, absDesc(10));
    EXPECT_EQ(registry.declare(features, FrameView{}), 1u);
    ASSERT_TRUE(features.compileFrame(g, "clouds-failed"));
    EXPECT_EQ(g.passCount(), 0u);
    const std::vector<PassId>& order = features.compiledOrder();
    EXPECT_EQ(std::find(order.begin(), order.end(), PassId::Clouds), order.end());
    registry.destroyAll();
}
