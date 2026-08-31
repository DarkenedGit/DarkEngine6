#include <gtest/gtest.h>
#include "Input/Input.h"

using namespace Dark;

TEST(InputMouse, WarpDoesNotAddDelta)
{
    Input in;
    in.onMouseMove(10, 20);
    in.beginFrame();
    in.onMouseMove(40, 20);
    EXPECT_EQ(in.mouseDeltaX(), 30);

    in.beginFrame();
    in.warpMouse(100, 80);
    EXPECT_EQ(in.mouseDeltaX(), 0);
    EXPECT_EQ(in.mouseDeltaY(), 0);
    EXPECT_EQ(in.mouseX(), 100);
    EXPECT_EQ(in.mouseY(), 80);

    in.onMouseMove(100, 80);
    EXPECT_EQ(in.mouseDeltaX(), 0);
    EXPECT_EQ(in.mouseDeltaY(), 0);
}

TEST(InputMouse, MoveAfterWarpAccumulatesFromWarp)
{
    Input in;
    in.onMouseMove(0, 0);
    in.beginFrame();
    in.warpMouse(50, 50);
    in.onMouseMove(55, 50);
    EXPECT_EQ(in.mouseDeltaX(), 5);
    EXPECT_EQ(in.mouseY(), 50);
}
