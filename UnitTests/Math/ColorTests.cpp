#include <gtest/gtest.h>
#include "Math/Color.h"
#include <cmath>
#include <cstdint>

using namespace Dark::Color;

TEST(Color, SrgbToLinear_BlackWhite)
{
    EXPECT_EQ(srgbToLinear(0.0f), 0.0f);
    EXPECT_EQ(srgbToLinear(1.0f), 1.0f);
}

TEST(Color, SrgbToLinear_Threshold)
{
    EXPECT_FLOAT_EQ(srgbToLinear(0.04045f), 0.04045f / 12.92f);
}

TEST(Color, SrgbToLinear_MidGray)
{
    EXPECT_NEAR(srgbToLinear(0.5f), 0.21404114048223255, 1.0e-6);
}

TEST(Color, SrgbToLinear_188)
{
    EXPECT_NEAR(srgbToLinear(188.0f / 255.0f), 0.5028864580325687, 1.0e-6);
}

TEST(Color, SrgbToLinear_Clamps)
{
    EXPECT_EQ(srgbToLinear(-1.0f), 0.0f);
    EXPECT_EQ(srgbToLinear(2.0f), 1.0f);
}

TEST(Color, LinearToSrgb_BlackWhite)
{
    EXPECT_EQ(linearToSrgb(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(linearToSrgb(1.0f), 1.0f);
}

TEST(Color, LinearToSrgb_Threshold)
{
    EXPECT_FLOAT_EQ(linearToSrgb(0.0031308f), 12.92f * 0.0031308f);
}

TEST(Color, LinearToSrgb_MidGray)
{
    EXPECT_NEAR(linearToSrgb(0.21404114048223255f), 0.5f, 1.0e-5f);
}

TEST(Color, LinearToSrgb_Clamps)
{
    EXPECT_EQ(linearToSrgb(-0.25f), 0.0f);
    EXPECT_FLOAT_EQ(linearToSrgb(1.5f), 1.0f);
}

TEST(Color, RoundTrip)
{
    const float kMaxErr = 2.0f / 255.0f;
    for (int i = 0; i < 32; ++i)
    {
        const float x   = static_cast<float>(i) / 31.0f;
        const float err = std::fabs(x - srgbToLinear(linearToSrgb(x)));
        EXPECT_LT(err, kMaxErr) << "x=" << x;
    }
}

TEST(Color, Srgb8RoundTrip)
{
    for (int i = 0; i < 256; ++i)
    {
        const uint8_t u    = static_cast<uint8_t>(i);
        const uint8_t back = linearToSrgb8(srgb8ToLinear(u));
        const int     diff = std::abs(static_cast<int>(u) - static_cast<int>(back));
        EXPECT_LE(diff, 1) << "u=" << i;
    }
}

TEST(Color, InferUsage)
{
    EXPECT_EQ(inferColorSpaceForUsage(TextureUsage::Albedo), ColorSpace::sRGB);
    EXPECT_EQ(inferColorSpaceForUsage(TextureUsage::Emissive), ColorSpace::sRGB);
    EXPECT_EQ(inferColorSpaceForUsage(TextureUsage::Normal), ColorSpace::Linear);
    EXPECT_EQ(inferColorSpaceForUsage(TextureUsage::Orm), ColorSpace::Linear);
    EXPECT_EQ(inferColorSpaceForUsage(TextureUsage::Data), ColorSpace::Linear);
    EXPECT_EQ(inferColorSpaceForUsage(TextureUsage::Height), ColorSpace::Linear);
    EXPECT_EQ(inferColorSpaceForUsage(TextureUsage::Hud), ColorSpace::Linear);
    EXPECT_EQ(inferColorSpaceForUsage(TextureUsage::Font), ColorSpace::Linear);
    EXPECT_EQ(inferColorSpaceForUsage(static_cast<TextureUsage>(255)), ColorSpace::Linear);
}

TEST(Color, Srgb8ToLinear3)
{
    float linear[3] = { 0.0f, 0.0f, 0.0f };
    srgb8ToLinear3(188, 188, 188, linear);
    EXPECT_NEAR(linear[0], 0.5028864580325687, 1.0e-6);
    EXPECT_NEAR(linear[1], 0.5028864580325687, 1.0e-6);
    EXPECT_NEAR(linear[2], 0.5028864580325687, 1.0e-6);
}

TEST(Color, SrgbToLinear3)
{
    const float srgb[3]   = { 0.0f, 0.5f, 1.0f };
    float       linear[3] = { 99.0f, 99.0f, 99.0f };
    srgbToLinear3(srgb, linear);
    EXPECT_EQ(linear[0], 0.0f);
    EXPECT_NEAR(linear[1], 0.21404114048223255, 1.0e-6);
    EXPECT_EQ(linear[2], 1.0f);
}

TEST(Color, LinearToSrgb3)
{
    const float linear[3] = { 0.0f, 0.21404114048223255f, 1.0f };
    float       srgb[3]   = { 99.0f, 99.0f, 99.0f };
    linearToSrgb3(linear, srgb);
    EXPECT_EQ(srgb[0], 0.0f);
    EXPECT_NEAR(srgb[1], 0.5f, 1.0e-5f);
    EXPECT_FLOAT_EQ(srgb[2], 1.0f);
}

TEST(Color, AlphaNotInferredAsSrgb)
{
    // Helpers take RGB only; alpha is identity UNORM (u/255), never the transfer.
    float linear[3] = {};
    srgb8ToLinear3(188, 188, 188, linear);
    const float a = 188.0f / 255.0f;
    EXPECT_NEAR(linear[0], 0.5028864580325687, 1.0e-6);
    EXPECT_NEAR(a, 188.0f / 255.0f, 1.0e-7);
    EXPECT_GT(std::fabs(linear[0] - a), 0.1f);
}

TEST(Color, Srgb8BlackWhite)
{
    EXPECT_EQ(srgb8ToLinear(0), 0.0f);
    EXPECT_EQ(srgb8ToLinear(255), 1.0f);
    EXPECT_EQ(linearToSrgb8(0.0f), 0);
    EXPECT_EQ(linearToSrgb8(1.0f), 255);
}
