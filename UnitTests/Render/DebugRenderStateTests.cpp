#include <gtest/gtest.h>

#include "Render/DebugRenderState.h"

using namespace Dark;

TEST(DebugRenderState, DefaultsAreLitSolidWithShadows)
{
    const DebugRenderState s{};
    EXPECT_EQ(s.fill, DebugFill::Solid);
    EXPECT_TRUE(s.lighting);
    EXPECT_TRUE(s.localLights);
    EXPECT_TRUE(s.bloom);
    EXPECT_TRUE(s.shadows);
    EXPECT_FALSE(s.aces);
    EXPECT_TRUE(s.motionBlur);
    EXPECT_TRUE(s.taa);
    EXPECT_FALSE(s.legacyUnormAlbedo);
    EXPECT_FALSE(s.showAlbedoLinear);
    EXPECT_FALSE(s.showAlbedoRaw);
    EXPECT_TRUE(s.iblEnabled);
    EXPECT_EQ(s.iblDebug, 0);
    EXPECT_FALSE(s.ssaoEnabled);
    EXPECT_EQ(s.ssaoDebug, 0);
    EXPECT_FALSE(s.ssrEnabled);
    EXPECT_EQ(s.ssrDebug, 0);
    EXPECT_TRUE(s.lightingActive());
}

TEST(DebugRenderState, LightingActiveRespectsAlbedoLinear)
{
    DebugRenderState s{};
    s.showAlbedoLinear = true;
    EXPECT_FALSE(s.lightingActive());
    s.lighting         = false;
    s.showAlbedoLinear = false;
    EXPECT_FALSE(s.lightingActive());
    s.lighting = true;
    EXPECT_TRUE(s.lightingActive());
}

TEST(DebugRenderState, CycleFillWraps)
{
    DebugRenderState s{};
    EXPECT_STREQ(toString(s.fill), "solid");

    s.cycleFill();
    EXPECT_EQ(s.fill, DebugFill::Wireframe);
    EXPECT_STREQ(toString(s.fill), "wireframe");

    s.cycleFill();
    EXPECT_EQ(s.fill, DebugFill::Points);
    EXPECT_STREQ(toString(s.fill), "points");

    s.cycleFill();
    EXPECT_EQ(s.fill, DebugFill::Solid);
    EXPECT_STREQ(toString(s.fill), "solid");
}
