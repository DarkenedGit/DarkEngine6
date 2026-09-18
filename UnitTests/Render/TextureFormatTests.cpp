#include <gtest/gtest.h>

#include "Assets/Image.h"
#include "Math/Color.h"
#include "Render/Texture2D.h"

using Dark::ImageFormat;
using Dark::resolveTextureFormats;
using Dark::Color::ColorSpace;
using Dark::Color::inferColorSpaceForUsage;
using Dark::Color::resolveGpuColorSpace;
using Dark::Color::TextureUsage;

TEST(TextureFormat, SrgbRgba8IsTypelessWithTypedViews)
{
    DXGI_FORMAT resource = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT srv      = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT foot     = DXGI_FORMAT_UNKNOWN;
    ASSERT_TRUE(resolveTextureFormats(ColorSpace::sRGB, ImageFormat::RGBA8, resource, srv, foot));
    EXPECT_EQ(resource, DXGI_FORMAT_R8G8B8A8_TYPELESS);
    EXPECT_EQ(srv, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    EXPECT_EQ(foot, DXGI_FORMAT_R8G8B8A8_UNORM);
    EXPECT_NE(srv, DXGI_FORMAT_R8G8B8A8_TYPELESS);
    EXPECT_NE(foot, DXGI_FORMAT_R8G8B8A8_TYPELESS);
}

TEST(TextureFormat, LinearRgba8IsUnorm)
{
    DXGI_FORMAT resource = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT srv      = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT foot     = DXGI_FORMAT_UNKNOWN;
    ASSERT_TRUE(resolveTextureFormats(ColorSpace::Linear, ImageFormat::RGBA8, resource, srv, foot));
    EXPECT_EQ(resource, DXGI_FORMAT_R8G8B8A8_UNORM);
    EXPECT_EQ(srv, DXGI_FORMAT_R8G8B8A8_UNORM);
    EXPECT_EQ(foot, DXGI_FORMAT_R8G8B8A8_UNORM);
}

TEST(TextureFormat, LinearR32FIsFloat)
{
    DXGI_FORMAT resource = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT srv      = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT foot     = DXGI_FORMAT_UNKNOWN;
    ASSERT_TRUE(resolveTextureFormats(ColorSpace::Linear, ImageFormat::R32F, resource, srv, foot));
    EXPECT_EQ(resource, DXGI_FORMAT_R32_FLOAT);
    EXPECT_EQ(srv, DXGI_FORMAT_R32_FLOAT);
    EXPECT_EQ(foot, DXGI_FORMAT_R32_FLOAT);
}

TEST(TextureFormat, SrgbR32FFails)
{
    DXGI_FORMAT resource = DXGI_FORMAT_R32_FLOAT;
    DXGI_FORMAT srv      = DXGI_FORMAT_R32_FLOAT;
    DXGI_FORMAT foot     = DXGI_FORMAT_R32_FLOAT;
    EXPECT_FALSE(resolveTextureFormats(ColorSpace::sRGB, ImageFormat::R32F, resource, srv, foot));
}

TEST(TextureFormat, UnknownColorSpaceFails)
{
    DXGI_FORMAT resource = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT srv      = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT foot     = DXGI_FORMAT_UNKNOWN;
    EXPECT_FALSE(resolveTextureFormats(ColorSpace::Unknown, ImageFormat::RGBA8, resource, srv, foot));
    EXPECT_FALSE(resolveTextureFormats(ColorSpace::Unknown, ImageFormat::R32F, resource, srv, foot));
    EXPECT_FALSE(resolveTextureFormats(ColorSpace::Unknown, ImageFormat::RGBA32F, resource, srv, foot));
}

TEST(TextureFormat, InvalidImageFormatFails)
{
    DXGI_FORMAT resource = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT srv      = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT foot     = DXGI_FORMAT_UNKNOWN;
    EXPECT_FALSE(resolveTextureFormats(ColorSpace::Linear, static_cast<ImageFormat>(255), resource, srv, foot));
}

TEST(TextureFormat, InferUsage)
{
    EXPECT_EQ(inferColorSpaceForUsage(TextureUsage::Hud), ColorSpace::Linear);
    EXPECT_EQ(inferColorSpaceForUsage(TextureUsage::Albedo), ColorSpace::sRGB);
    EXPECT_EQ(inferColorSpaceForUsage(TextureUsage::Emissive), ColorSpace::sRGB);
    EXPECT_EQ(inferColorSpaceForUsage(TextureUsage::Data), ColorSpace::Linear);
}

TEST(TextureFormat, LinearUsageAlwaysWins)
{
    EXPECT_EQ(resolveGpuColorSpace(TextureUsage::Hud, ColorSpace::sRGB), ColorSpace::Linear);
    EXPECT_EQ(resolveGpuColorSpace(TextureUsage::Hud, ColorSpace::Unknown), ColorSpace::Linear);
    EXPECT_EQ(resolveGpuColorSpace(TextureUsage::Data, ColorSpace::sRGB), ColorSpace::Linear);
    EXPECT_EQ(resolveGpuColorSpace(TextureUsage::Font, ColorSpace::sRGB), ColorSpace::Linear);
    EXPECT_EQ(resolveGpuColorSpace(TextureUsage::Normal, ColorSpace::sRGB), ColorSpace::Linear);
    EXPECT_EQ(resolveGpuColorSpace(TextureUsage::Height, ColorSpace::sRGB), ColorSpace::Linear);
    EXPECT_EQ(resolveGpuColorSpace(TextureUsage::Ibl, ColorSpace::sRGB), ColorSpace::Linear);
}

TEST(TextureFormat, AlbedoKeepsExplicitLinear)
{
    EXPECT_EQ(resolveGpuColorSpace(TextureUsage::Albedo, ColorSpace::Linear), ColorSpace::Linear);
    EXPECT_EQ(resolveGpuColorSpace(TextureUsage::Emissive, ColorSpace::Linear), ColorSpace::Linear);
}

TEST(TextureFormat, AlbedoUnknownDefaultsSrgb)
{
    EXPECT_EQ(resolveGpuColorSpace(TextureUsage::Albedo, ColorSpace::Unknown), ColorSpace::sRGB);
    EXPECT_EQ(resolveGpuColorSpace(TextureUsage::Albedo, ColorSpace::sRGB), ColorSpace::sRGB);
    EXPECT_EQ(resolveGpuColorSpace(TextureUsage::Emissive, ColorSpace::Unknown), ColorSpace::sRGB);
}

TEST(TextureFormats, LinearRgba32f)
{
    DXGI_FORMAT resource = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT srv      = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT foot     = DXGI_FORMAT_UNKNOWN;
    ASSERT_TRUE(resolveTextureFormats(ColorSpace::Linear, ImageFormat::RGBA32F, resource, srv, foot));
    EXPECT_EQ(resource, DXGI_FORMAT_R16G16B16A16_FLOAT);
    EXPECT_EQ(srv, DXGI_FORMAT_R16G16B16A16_FLOAT);
    EXPECT_EQ(foot, DXGI_FORMAT_R16G16B16A16_FLOAT);
}

TEST(TextureFormats, SrgbRgba32f_Fails)
{
    DXGI_FORMAT resource = DXGI_FORMAT_R16G16B16A16_FLOAT;
    DXGI_FORMAT srv      = DXGI_FORMAT_R16G16B16A16_FLOAT;
    DXGI_FORMAT foot     = DXGI_FORMAT_R16G16B16A16_FLOAT;
    EXPECT_FALSE(resolveTextureFormats(ColorSpace::sRGB, ImageFormat::RGBA32F, resource, srv, foot));
}

TEST(Color, InferUsage_Ibl)
{
    EXPECT_EQ(inferColorSpaceForUsage(TextureUsage::Ibl), ColorSpace::Linear);
}
