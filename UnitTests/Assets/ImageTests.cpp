#include <gtest/gtest.h>

#include "Assets/Image.h"
#include "Math/Color.h"

using Dark::Image;
using Dark::ImageFormat;
using Dark::Color::ColorSpace;

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
    EXPECT_EQ(img.colorSpace(), ColorSpace::Unknown);
    EXPECT_TRUE(img.colorSpaceWasDefaulted());
}

TEST(Image, FromRgbaPitch)
{
    const uint8_t px[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    Image         img;
    ASSERT_TRUE(img.createFromRGBA(px, 2, 1, 8));
    EXPECT_EQ(img.width(), 2u);
    EXPECT_EQ(img.height(), 1u);
    EXPECT_EQ(img.rowPitchBytes(), 8u);
    EXPECT_EQ(img.colorSpace(), ColorSpace::Unknown);
    EXPECT_TRUE(img.colorSpaceWasDefaulted());
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
    EXPECT_EQ(img.colorSpace(), ColorSpace::Linear);
    EXPECT_FALSE(img.colorSpaceWasDefaulted());
}

TEST(Image, SoftStreakIsLinear)
{
    Image img;
    ASSERT_TRUE(img.createSoftStreak(16));
    EXPECT_EQ(img.colorSpace(), ColorSpace::Linear);
    EXPECT_FALSE(img.colorSpaceWasDefaulted());
}

TEST(Image, R32FloatIsLinear)
{
    const float samples[] = { 1.5f, -0.25f };
    Image       img;
    ASSERT_TRUE(img.createFromR32Float(samples, 2, 1, 8));
    EXPECT_EQ(img.format(), ImageFormat::R32F);
    EXPECT_EQ(img.colorSpace(), ColorSpace::Linear);
    EXPECT_FALSE(img.colorSpaceWasDefaulted());
}

TEST(Image, SetColorSpaceClearsDefaulted)
{
    Image img;
    ASSERT_TRUE(img.createSolidColor(1, 2, 3, 4));
    EXPECT_TRUE(img.colorSpaceWasDefaulted());
    img.setColorSpace(ColorSpace::sRGB);
    EXPECT_EQ(img.colorSpace(), ColorSpace::sRGB);
    EXPECT_FALSE(img.colorSpaceWasDefaulted());
}

TEST(Image, EmptyRgbaFails)
{
    Image img;
    EXPECT_FALSE(img.createFromRGBA(nullptr, 0, 0, 0));
    EXPECT_FALSE(img.valid());
}

TEST(Image, FailedCreateLeavesColorSpace)
{
    Image img;
    ASSERT_TRUE(img.createSoftCircle(8));
    EXPECT_EQ(img.colorSpace(), ColorSpace::Linear);
    EXPECT_FALSE(img.colorSpaceWasDefaulted());
    EXPECT_FALSE(img.createFromRGBA(nullptr, 0, 0, 0));
    EXPECT_FALSE(img.createFromR32Float(nullptr, 0, 0, 0));
    EXPECT_EQ(img.colorSpace(), ColorSpace::Linear);
    EXPECT_FALSE(img.colorSpaceWasDefaulted());
}
