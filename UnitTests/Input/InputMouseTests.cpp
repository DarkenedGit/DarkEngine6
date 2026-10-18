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

TEST(SidedVirtualKey, ModifiersUseScanCodeAndLettersPassThrough)
{
    // 0x10/0x11/0x12 are the generic Shift, Ctrl, and Alt messages. 0x2A/0x36 are left and right Shift.
    EXPECT_EQ(sidedVirtualKey(0x10, 0x002A), static_cast<uint16_t>(Key::LeftShift));
    EXPECT_EQ(sidedVirtualKey(0x10, 0x0036), static_cast<uint16_t>(Key::RightShift));
    EXPECT_EQ(sidedVirtualKey(0x11, 0x001D), static_cast<uint16_t>(Key::LeftControl));
    EXPECT_EQ(sidedVirtualKey(0x11, 0x011D), static_cast<uint16_t>(Key::RightControl));
    EXPECT_EQ(sidedVirtualKey(0x12, 0x0038), static_cast<uint16_t>(Key::LeftAlt));
    EXPECT_EQ(sidedVirtualKey(0x12, 0x0138), static_cast<uint16_t>(Key::RightAlt));
    EXPECT_EQ(sidedVirtualKey(static_cast<uint16_t>(Key::W), 0x0011), static_cast<uint16_t>(Key::W));
    EXPECT_EQ(sidedVirtualKey(0x10, 0x0000), static_cast<uint16_t>(0x10));
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
