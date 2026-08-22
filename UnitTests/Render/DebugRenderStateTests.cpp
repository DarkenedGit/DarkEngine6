#include <gtest/gtest.h>

#include "Render/DebugRenderState.h"

using namespace Dark;

TEST(DebugRenderState, DefaultsAreLitSolidWithShadows)
{
    const DebugRenderState s{};
    EXPECT_EQ(s.fill, DebugFill::Solid);
    EXPECT_TRUE(s.lighting);
    EXPECT_TRUE(s.shadows);
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
