#include <gtest/gtest.h>

#include "Render/TaaJitter.h"

using namespace Dark;
using namespace Dark::Math;

TEST(TaaJitter, HaltonBase2)
{
    EXPECT_NEAR(halton(1, 2), 0.5f, 1e-6f);
    EXPECT_NEAR(halton(2, 2), 0.25f, 1e-6f);
    EXPECT_NEAR(halton(3, 2), 0.75f, 1e-6f);
}

TEST(TaaJitter, PixelJitterIsSubpixel)
{
    float x = 0.0f, y = 0.0f;
    for (uint32_t i = 0; i < 8; ++i)
    {
        taaHaltonJitter(i, x, y);
        EXPECT_GE(x, -0.5f);
        EXPECT_LT(x, 0.5f);
        EXPECT_GE(y, -0.5f);
        EXPECT_LT(y, 0.5f);
    }
}

TEST(TaaJitter, NdcJitterOffsetsProjection)
{
    Matrix4f p = Matrix4f::PerspectiveFovLHMatrix(1.0f, 1.0f, 0.1f, 100.0f);
    const float m31 = p.m_afEntry[Mat4f::m31];
    const float m32 = p.m_afEntry[Mat4f::m32];
    applyNdcJitter(p, 0.5f, -0.5f, 200, 100);
    EXPECT_NEAR(p.m_afEntry[Mat4f::m31], m31 + 2.0f * 0.5f / 200.0f, 1e-6f);
    EXPECT_NEAR(p.m_afEntry[Mat4f::m32], m32 + 2.0f * -0.5f / 100.0f, 1e-6f);
}
