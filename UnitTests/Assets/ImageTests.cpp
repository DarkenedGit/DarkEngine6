#include <gtest/gtest.h>

#include "Assets/Image.h"
#include "Math/Color.h"

#include <cstdint>
#include <filesystem>
#include <system_error>

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

TEST(Image, WriteRgbaPngRoundTrip)
{
    // Pitch is wider than the two pixels so the pad bytes must not be stored.
    // Red and blue differ so a BGRA swap fails this check.
    const uint8_t rows[] = {
        10, 20, 30, 255, 200, 10, 1, 128, 0xAA, 0xBB,
        1, 2, 3, 4, 255, 0, 0, 64, 0xCC, 0xDD,
    };
    std::error_code ec;
    const auto      base = std::filesystem::temp_directory_path(ec);
    ASSERT_FALSE(ec);
    const auto path = base / "darkengine6_write_rgba.png";
    std::filesystem::remove(path, ec);

    ASSERT_TRUE(Dark::writeRgbaPng(path, rows, 2, 2, 10));
    const auto size = std::filesystem::file_size(path, ec);
    ASSERT_FALSE(ec);
    EXPECT_GT(size, 8u);

    Image img;
    ASSERT_TRUE(img.createFromFile(path));
    EXPECT_EQ(img.width(), 2u);
    EXPECT_EQ(img.height(), 2u);
    EXPECT_EQ(img.format(), ImageFormat::RGBA8);
    ASSERT_NE(img.pixels(), nullptr);
    const uint8_t* px    = img.pixels();
    const uint32_t pitch = img.rowPitchBytes();
    EXPECT_EQ(px[0], 10);
    EXPECT_EQ(px[1], 20);
    EXPECT_EQ(px[2], 30);
    EXPECT_EQ(px[3], 255);
    EXPECT_EQ(px[4], 200);
    EXPECT_EQ(px[5], 10);
    EXPECT_EQ(px[6], 1);
    EXPECT_EQ(px[7], 128);
    const uint8_t* row1 = px + pitch;
    EXPECT_EQ(row1[0], 1);
    EXPECT_EQ(row1[1], 2);
    EXPECT_EQ(row1[2], 3);
    EXPECT_EQ(row1[3], 4);
    EXPECT_EQ(row1[4], 255);
    EXPECT_EQ(row1[5], 0);
    EXPECT_EQ(row1[6], 0);
    EXPECT_EQ(row1[7], 64);
    std::filesystem::remove(path, ec);
}

TEST(Image, WriteRgbaPngRejectsEmpty)
{
    std::error_code ec;
    const auto      base = std::filesystem::temp_directory_path(ec);
    ASSERT_FALSE(ec);
    const auto path = base / "darkengine6_write_rgba_reject.png";
    std::filesystem::remove(path, ec);
    EXPECT_FALSE(Dark::writeRgbaPng(path, nullptr, 1, 1, 4));
    EXPECT_FALSE(std::filesystem::exists(path, ec));
}
