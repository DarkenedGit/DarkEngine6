#include <gtest/gtest.h>

#include "Math/Color.h"
#include "Render/TextureMips.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using Dark::ImageFormat;
using Dark::MipFilter;
using Dark::MipLevel;
using Dark::buildMipChain;
using Dark::mipFilterForImage;
using Dark::textureMipCount;
using Dark::textureUploadMipCount;
using Dark::Color::ColorSpace;
using Dark::Color::TextureUsage;

namespace
{

    uint8_t encodeNormal(float n)
    {
        float u = n * 0.5f + 0.5f;
        if (u < 0.0f)
            u = 0.0f;
        if (u > 1.0f)
            u = 1.0f;
        return static_cast<uint8_t>(u * 255.0f + 0.5f);
    }

    float decodeNormal(uint8_t u)
    {
        return static_cast<float>(u) / 255.0f * 2.0f - 1.0f;
    }

    void writeRgba(uint8_t* pixel, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        pixel[0] = r;
        pixel[1] = g;
        pixel[2] = b;
        pixel[3] = a;
    }

} // namespace

TEST(TextureMip, ChainLength)
{
    EXPECT_EQ(textureMipCount(0, 4), 0u);
    EXPECT_EQ(textureMipCount(1, 1), 1u);
    EXPECT_EQ(textureMipCount(2, 2), 2u);
    EXPECT_EQ(textureMipCount(3, 2), 2u);
    EXPECT_EQ(textureMipCount(256, 128), 9u);
    EXPECT_EQ(textureUploadMipCount(1, 1, MipFilter::SrgbColor), 1u);
    EXPECT_EQ(textureUploadMipCount(256, 128, MipFilter::LinearUnorm), 9u);
    EXPECT_EQ(textureUploadMipCount(0, 4, MipFilter::None), 0u);
}

TEST(TextureMip, SrgbAveragesInLinear)
{
    uint8_t pixels[2 * 2 * 4];
    writeRgba(pixels + 0, 0, 0, 0, 0);
    writeRgba(pixels + 4, 255, 255, 255, 10);
    writeRgba(pixels + 8, 0, 0, 0, 20);
    writeRgba(pixels + 12, 255, 255, 255, 30);

    std::vector<MipLevel> chain;
    ASSERT_TRUE(buildMipChain(pixels, 2, 2, 8, MipFilter::SrgbColor, chain));
    ASSERT_EQ(chain.size(), 2u);
    ASSERT_EQ(chain[1].pixels.size(), 4u);

    const float  linear = 0.5f * (Dark::Color::srgb8ToLinear(0) + Dark::Color::srgb8ToLinear(255));
    const uint8_t encoded = Dark::Color::linearToSrgb8(linear);
    EXPECT_NE(encoded, static_cast<uint8_t>((0 + 255) / 2));
    EXPECT_EQ(chain[1].pixels[0], encoded);
    EXPECT_EQ(chain[1].pixels[1], encoded);
    EXPECT_EQ(chain[1].pixels[2], encoded);
    EXPECT_EQ(chain[1].pixels[3], 15);
    EXPECT_EQ(chain[0].pixels[0], 0);
    EXPECT_EQ(chain[0].pixels[7], 10);
}

TEST(TextureMip, NormalRenormalizes)
{
    uint8_t pixels[2 * 2 * 4];
    writeRgba(pixels + 0, encodeNormal(1.0f), encodeNormal(0.0f), encodeNormal(0.0f), 0);
    writeRgba(pixels + 4, encodeNormal(1.0f), encodeNormal(0.0f), encodeNormal(0.0f), 255);
    writeRgba(pixels + 8, encodeNormal(0.0f), encodeNormal(1.0f), encodeNormal(0.0f), 0);
    writeRgba(pixels + 12, encodeNormal(0.0f), encodeNormal(0.0f), encodeNormal(1.0f), 255);

    std::vector<MipLevel> chain;
    ASSERT_TRUE(buildMipChain(pixels, 2, 2, 8, MipFilter::NormalTangent, chain));
    ASSERT_EQ(chain.size(), 2u);

    const float x = decodeNormal(chain[1].pixels[0]);
    const float y = decodeNormal(chain[1].pixels[1]);
    const float z = decodeNormal(chain[1].pixels[2]);
    const float len = std::sqrt(x * x + y * y + z * z);
    EXPECT_NEAR(len, 1.0f, 0.05f);
    EXPECT_GT(x, 0.7f);
    EXPECT_EQ(chain[1].pixels[3], 128);

    uint8_t opposite[2 * 2 * 4];
    writeRgba(opposite + 0, encodeNormal(1.0f), encodeNormal(0.0f), encodeNormal(0.0f), 255);
    writeRgba(opposite + 4, encodeNormal(-1.0f), encodeNormal(0.0f), encodeNormal(0.0f), 255);
    writeRgba(opposite + 8, encodeNormal(0.0f), encodeNormal(1.0f), encodeNormal(0.0f), 255);
    writeRgba(opposite + 12, encodeNormal(0.0f), encodeNormal(-1.0f), encodeNormal(0.0f), 255);
    ASSERT_TRUE(buildMipChain(opposite, 2, 2, 8, MipFilter::NormalTangent, chain));
    EXPECT_EQ(chain[1].pixels[0], 128);
    EXPECT_EQ(chain[1].pixels[1], 128);
    EXPECT_EQ(chain[1].pixels[2], 255);
}

TEST(TextureMip, LinearUnormAndOddRow)
{
    uint8_t pixels[2 * 2 * 4];
    writeRgba(pixels + 0, 10, 20, 30, 40);
    writeRgba(pixels + 4, 50, 60, 70, 80);
    writeRgba(pixels + 8, 90, 100, 110, 120);
    writeRgba(pixels + 12, 130, 140, 150, 160);

    std::vector<MipLevel> chain;
    ASSERT_TRUE(buildMipChain(pixels, 2, 2, 8, MipFilter::LinearUnorm, chain));
    ASSERT_EQ(chain.size(), 2u);
    EXPECT_EQ(chain[1].pixels[0], 70);
    EXPECT_EQ(chain[1].pixels[1], 80);
    EXPECT_EQ(chain[1].pixels[2], 90);
    EXPECT_EQ(chain[1].pixels[3], 100);

    uint8_t row[3 * 4];
    writeRgba(row + 0, 10, 0, 0, 255);
    writeRgba(row + 4, 20, 0, 0, 255);
    writeRgba(row + 8, 30, 0, 0, 255);
    ASSERT_TRUE(buildMipChain(row, 3, 1, 12, MipFilter::LinearUnorm, chain));
    ASSERT_EQ(chain.size(), 2u);
    EXPECT_EQ(chain[0].width, 3u);
    EXPECT_EQ(chain[1].width, 1u);
    EXPECT_EQ(chain[1].height, 1u);
    EXPECT_EQ(chain[1].pixels[0], 20);
    EXPECT_EQ(chain[1].pixels[3], 255);

    uint8_t grid[3 * 2 * 4];
    for (int i = 0; i < 6; ++i)
        writeRgba(grid + i * 4, static_cast<uint8_t>(i + 1), 0, 0, 0);
    ASSERT_TRUE(buildMipChain(grid, 3, 2, 12, MipFilter::LinearUnorm, chain));
    ASSERT_EQ(chain.size(), 2u);
    EXPECT_EQ(chain[1].width, 1u);
    EXPECT_EQ(chain[1].height, 1u);
    EXPECT_EQ(chain[1].pixels[0], 4);
}

TEST(TextureMip, HeightAndRgFloatStaySingleLevel)
{
    EXPECT_EQ(mipFilterForImage(TextureUsage::Height, ColorSpace::Linear, ImageFormat::R32F), MipFilter::None);
    EXPECT_EQ(mipFilterForImage(TextureUsage::Albedo, ColorSpace::sRGB, ImageFormat::R32F), MipFilter::None);
    EXPECT_EQ(mipFilterForImage(TextureUsage::Font, ColorSpace::Linear, ImageFormat::RGBA8), MipFilter::None);
    EXPECT_EQ(textureUploadMipCount(512, 512, MipFilter::None), 1u);

    EXPECT_EQ(mipFilterForImage(TextureUsage::Albedo, ColorSpace::sRGB, ImageFormat::RGBA8), MipFilter::SrgbColor);
    EXPECT_EQ(mipFilterForImage(TextureUsage::Emissive, ColorSpace::sRGB, ImageFormat::RGBA8), MipFilter::SrgbColor);
    EXPECT_EQ(mipFilterForImage(TextureUsage::Albedo, ColorSpace::Linear, ImageFormat::RGBA8), MipFilter::LinearUnorm);
    EXPECT_EQ(mipFilterForImage(TextureUsage::Normal, ColorSpace::Linear, ImageFormat::RGBA8), MipFilter::NormalTangent);
    EXPECT_EQ(mipFilterForImage(TextureUsage::Orm, ColorSpace::Linear, ImageFormat::RGBA8), MipFilter::LinearUnorm);
    EXPECT_EQ(mipFilterForImage(TextureUsage::Data, ColorSpace::Linear, ImageFormat::RGBA8), MipFilter::LinearUnorm);
    EXPECT_EQ(mipFilterForImage(TextureUsage::Hud, ColorSpace::Linear, ImageFormat::RGBA8), MipFilter::LinearUnorm);
    EXPECT_EQ(mipFilterForImage(TextureUsage::Ibl, ColorSpace::Linear, ImageFormat::RGBA32F), MipFilter::LinearFloat);
}

TEST(TextureMip, LinearFloatAveragesChannels)
{
    float pixels[2 * 2 * 4] = {
        0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f,
    };
    std::vector<MipLevel> chain;
    ASSERT_TRUE(buildMipChain(pixels, 2, 2, 32, MipFilter::LinearFloat, chain));
    ASSERT_EQ(chain.size(), 2u);
    float avg[4];
    std::memcpy(avg, chain[1].pixels.data(), sizeof(avg));
    EXPECT_FLOAT_EQ(avg[0], 0.5f);
    EXPECT_FLOAT_EQ(avg[1], 0.5f);
    EXPECT_FLOAT_EQ(avg[2], 0.0f);
    EXPECT_FLOAT_EQ(avg[3], 1.0f);
}

TEST(TextureMip, RejectsEmptyInput)
{
    std::vector<MipLevel> chain;
    EXPECT_FALSE(buildMipChain(nullptr, 2, 2, 8, MipFilter::LinearUnorm, chain));
    uint8_t pixel[4] = {};
    EXPECT_FALSE(buildMipChain(pixel, 1, 1, 4, MipFilter::None, chain));
    EXPECT_TRUE(chain.empty());
}
