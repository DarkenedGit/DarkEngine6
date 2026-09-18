#include <gtest/gtest.h>

#include "Assets/Image.h"
#include "Math/Color.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

using Dark::Image;
using Dark::ImageFormat;
using Dark::Color::ColorSpace;

namespace
{
    std::string MakeUncompressedRgbe(uint32_t width, uint32_t height, uint8_t r, uint8_t g, uint8_t b, uint8_t e, const char* magic = "#?RADIANCE")
    {
        std::string s;
        s += magic;
        s += "\nFORMAT=32-bit_rle_rgbe\n\n-Y ";
        s += std::to_string(height);
        s += " +X ";
        s += std::to_string(width);
        s += "\n";
        s.append(static_cast<size_t>(width) * height * 4u, '\0');
        for (uint32_t i = 0; i < width * height; ++i)
        {
            const size_t o    = s.size() - static_cast<size_t>(width) * height * 4u + static_cast<size_t>(i) * 4u;
            s[o + 0]          = static_cast<char>(r);
            s[o + 1]          = static_cast<char>(g);
            s[o + 2]          = static_cast<char>(b);
            s[o + 3]          = static_cast<char>(e);
        }
        return s;
    }

    std::string MakeRgbE1x1(uint8_t r, uint8_t g, uint8_t b, uint8_t e, const char* magic = "#?RADIANCE")
    {
        return MakeUncompressedRgbe(1, 1, r, g, b, e, magic);
    }
} // namespace

TEST(Image_Hdr, RgbE_1x1)
{
    const std::string hdr = MakeRgbE1x1(128, 64, 32, 128);
    Image             img;
    ASSERT_TRUE(img.createFromHdrMemory(hdr.data(), hdr.size()));
    ASSERT_TRUE(img.valid());
    EXPECT_EQ(img.format(), ImageFormat::RGBA32F);
    EXPECT_EQ(img.width(), 1u);
    EXPECT_EQ(img.height(), 1u);
    EXPECT_EQ(img.bytesPerPixel(), 16u);
    EXPECT_EQ(img.colorSpace(), ColorSpace::Linear);
    EXPECT_FALSE(img.colorSpaceWasDefaulted());
    ASSERT_NE(img.pixels(), nullptr);

    float rgba[4] = {};
    std::memcpy(rgba, img.pixels(), sizeof(rgba));
    const float scale = std::ldexp(1.0f, 128 - (128 + 8));
    EXPECT_NEAR(rgba[0], 128.0f * scale, 1.0e-3f);
    EXPECT_NEAR(rgba[1], 64.0f * scale, 1.0e-3f);
    EXPECT_NEAR(rgba[2], 32.0f * scale, 1.0e-3f);
    EXPECT_NEAR(rgba[3], 1.0f, 1.0e-3f);
}

TEST(Image_Hdr, LoadMissing_ReturnsFalse)
{
    Image img;
    EXPECT_FALSE(img.createFromHdrFile("this_hdr_does_not_exist_a54de7b6.hdr"));
    EXPECT_FALSE(img.createFromFile("this_hdr_does_not_exist_a54de7b6.hdr"));
    EXPECT_FALSE(img.valid());
}

TEST(Image_Hdr, RejectsOversize)
{
    const char header[] = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 8192 +X 8192\n";
    Image      img;
    EXPECT_FALSE(img.createFromHdrMemory(header, sizeof(header) - 1));
    EXPECT_FALSE(img.valid());

    Image oversizeFile;
    EXPECT_FALSE(oversizeFile.createFromHdrMemory(nullptr, 32u * 1024u * 1024u + 1u));
    EXPECT_FALSE(oversizeFile.valid());
}

TEST(Image_Hdr, RejectsExrExtension)
{
    std::error_code ec;
    const auto      tmpBase = std::filesystem::temp_directory_path(ec);
    ASSERT_FALSE(ec);
    const auto path = tmpBase / "darkengine6_hdr_pr1.exr";
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(static_cast<bool>(out));
        const std::string hdr = MakeRgbE1x1(128, 128, 128, 128);
        out.write(hdr.data(), static_cast<std::streamsize>(hdr.size()));
        ASSERT_TRUE(static_cast<bool>(out));
    }
    Image img;
    EXPECT_FALSE(img.createFromFile(path));
    EXPECT_FALSE(img.valid());
    std::filesystem::remove(path, ec);
}

TEST(Image, BytesPerPixel_Rgba32f)
{
    const float rgba32[4] = { 1.0f, 2.0f, 3.0f, 1.0f };
    Image       rgba32f;
    ASSERT_TRUE(rgba32f.createFromRgba32f(rgba32, 1, 1, 16));
    EXPECT_EQ(rgba32f.format(), ImageFormat::RGBA32F);
    EXPECT_EQ(rgba32f.bytesPerPixel(), 16u);

    Image rgba8;
    ASSERT_TRUE(rgba8.createSolidColor(1, 2, 3, 4));
    EXPECT_EQ(rgba8.format(), ImageFormat::RGBA8);
    EXPECT_EQ(rgba8.bytesPerPixel(), 4u);

    const float r32[1] = { 0.5f };
    Image       r32f;
    ASSERT_TRUE(r32f.createFromR32Float(r32, 1, 1, 4));
    EXPECT_EQ(r32f.format(), ImageFormat::R32F);
    EXPECT_EQ(r32f.bytesPerPixel(), 4u);
}

TEST(Image_Hdr, RejectsXyze)
{
    const char xyze[] = "#?RADIANCE\nFORMAT=32-bit_rle_xyze\n\n-Y 1 +X 1\n\x80\x80\x80\x80";
    Image      img;
    EXPECT_FALSE(img.createFromHdrMemory(xyze, sizeof(xyze) - 1));
}

TEST(Image_Hdr, RejectsUnknownLayout)
{
    const char layout[] = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n+Y 1 +X 1\n\x80\x80\x80\x80";
    Image      img;
    EXPECT_FALSE(img.createFromHdrMemory(layout, sizeof(layout) - 1));
}

TEST(Image_Hdr, CreateFromFile_HdrExtension)
{
    std::error_code ec;
    const auto      tmpBase = std::filesystem::temp_directory_path(ec);
    ASSERT_FALSE(ec);
    const auto path = tmpBase / "darkengine6_hdr_pr1.hdr";
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(static_cast<bool>(out));
        const std::string hdr = MakeRgbE1x1(128, 64, 32, 128);
        out.write(hdr.data(), static_cast<std::streamsize>(hdr.size()));
        ASSERT_TRUE(static_cast<bool>(out));
    }
    Image img;
    ASSERT_TRUE(img.createFromFile(path));
    EXPECT_EQ(img.format(), ImageFormat::RGBA32F);
    EXPECT_EQ(img.colorSpace(), ColorSpace::Linear);
    float rgba[4] = {};
    std::memcpy(rgba, img.pixels(), sizeof(rgba));
    EXPECT_NEAR(rgba[0], 0.5f, 1.0e-3f);
    std::filesystem::remove(path, ec);
}

TEST(Image_Hdr, RgbE_RleScanline)
{
    std::string s = "#?RGBE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 8\n";
    const uint8_t rle[] = {
        0x02, 0x02, 0x00, 0x08,
        136, 128,
        136, 64,
        136, 32,
        136, 128,
    };
    s.append(reinterpret_cast<const char*>(rle), sizeof(rle));

    Image img;
    ASSERT_TRUE(img.createFromHdrMemory(s.data(), s.size()));
    EXPECT_EQ(img.width(), 8u);
    EXPECT_EQ(img.height(), 1u);
    EXPECT_EQ(img.format(), ImageFormat::RGBA32F);
    EXPECT_EQ(img.colorSpace(), ColorSpace::Linear);
    const float scale = std::ldexp(1.0f, 128 - (128 + 8));
    for (uint32_t x = 0; x < 8; ++x)
    {
        float rgba[4] = {};
        std::memcpy(rgba, img.pixels() + static_cast<size_t>(x) * 16u, sizeof(rgba));
        EXPECT_NEAR(rgba[0], 128.0f * scale, 1.0e-3f) << "x=" << x;
        EXPECT_NEAR(rgba[1], 64.0f * scale, 1.0e-3f) << "x=" << x;
        EXPECT_NEAR(rgba[2], 32.0f * scale, 1.0e-3f) << "x=" << x;
        EXPECT_NEAR(rgba[3], 1.0f, 1.0e-3f) << "x=" << x;
    }
}
