#include <gtest/gtest.h>

#include "Assets/Image.h"

using Dark::Image;

TEST(Image, SolidColorIs1x1Rgba)
{
    Image img;
    ASSERT_TRUE(img.createSolidColor(10, 20, 30, 40));
    ASSERT_TRUE(img.valid());
    EXPECT_EQ(img.width(), 1u);
    EXPECT_EQ(img.height(), 1u);
    ASSERT_NE(img.pixels(), nullptr);
    EXPECT_EQ(img.pixels()[0], 10);
    EXPECT_EQ(img.pixels()[1], 20);
    EXPECT_EQ(img.pixels()[2], 30);
    EXPECT_EQ(img.pixels()[3], 40);
}

TEST(Image, FromRgbaPitch)
{
    const uint8_t px[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    Image         img;
    ASSERT_TRUE(img.createFromRGBA(px, 2, 1, 8));
    EXPECT_EQ(img.width(), 2u);
    EXPECT_EQ(img.height(), 1u);
    EXPECT_EQ(img.rowPitchBytes(), 8u);
}

TEST(Image, SoftCircleCenterAlphaGreaterThanEdge)
{
    Image img;
    ASSERT_TRUE(img.createSoftCircle(16));
    ASSERT_TRUE(img.valid());
    const uint8_t* px   = img.pixels();
    const uint32_t mid  = 8;
    const uint8_t  centerA = px[(mid * 16 + mid) * 4 + 3];
    const uint8_t  edgeA   = px[3];
    EXPECT_GT(centerA, edgeA);
}

TEST(Image, EmptyRgbaFails)
{
    Image img;
    EXPECT_FALSE(img.createFromRGBA(nullptr, 0, 0, 0));
    EXPECT_FALSE(img.valid());
}
