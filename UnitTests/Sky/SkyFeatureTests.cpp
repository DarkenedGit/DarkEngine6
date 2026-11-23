#include <gtest/gtest.h>

#include "Render/RenderFeature.h"
#include "Sky/SkyFeatures.h"

using namespace Dark;

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
