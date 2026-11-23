#include <gtest/gtest.h>

#include "Render/RenderFeature.h"
#include "Sky/SkyFeature.h"

using namespace Dark;

TEST(SkyFeature, DrawsInTheSkyStageOnTheSkyEntry)
{
    SkyFeature sky;
    EXPECT_STREQ(sky.name(), "Sky");
    EXPECT_EQ(sky.stage(), FeatureStage::Sky);
    EXPECT_EQ(sky.pass(), PassId::Sky);
}

TEST(SkyFeature, RegisterSkyFeaturesAddsTheSkyOnce)
{
    SkyFeature            sky;
    RenderFeatureRegistry registry;
    registerSkyFeatures(registry, sky);
    registerSkyFeatures(registry, sky);
    ASSERT_EQ(registry.count(), 1u);
    EXPECT_EQ(registry.find(PassId::Sky), &sky);
}

TEST(SkyFeature, WithoutARendererItFailsToCreateAndIsSkipped)
{
    SkyFeature            sky;
    RenderFeatureRegistry registry;
    registerSkyFeatures(registry, sky);
    EXPECT_FALSE(registry.createAll({}));
    EXPECT_FALSE(registry.isCreated(0));
    EXPECT_FALSE(sky.pipeline().isValid());
}
