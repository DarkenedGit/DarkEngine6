#include <gtest/gtest.h>
#include "Math/MathHelper.h"
#include "Math/MathDefines.h"

using namespace Dark::Math;

TEST(MathHelper, Clamp)
{
    EXPECT_EQ(Clamp(5, 0, 10), 5);
    EXPECT_EQ(Clamp(-1, 0, 10), 0);
    EXPECT_EQ(Clamp(11, 0, 10), 10);
    EXPECT_FLOAT_EQ(Clamp(0.5f, 0.0f, 1.0f), 0.5f);
}

TEST(MathHelper, MinMax)
{
    EXPECT_EQ(Min(3, 7), 3);
    EXPECT_EQ(Max(3, 7), 7);
    EXPECT_FLOAT_EQ(Min(-1.0f, -2.0f), -2.0f);
    EXPECT_FLOAT_EQ(Max(-1.0f, -2.0f), -1.0f);
}

TEST(MathHelper, Lerp)
{
    EXPECT_FLOAT_EQ(Lerp(0.0f, 10.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(Lerp(0.0f, 10.0f, 1.0f), 10.0f);
    EXPECT_FLOAT_EQ(Lerp(0.0f, 10.0f, 0.5f), 5.0f);
}

TEST(MathHelper, DegreesRadians)
{
    EXPECT_TRUE(NearEqual(DegreesToRadians(180.0f), Pi));
    EXPECT_TRUE(NearEqual(RadiansToDegrees(Pi), 180.0f));
    EXPECT_TRUE(NearEqual(DegreesToRadians(90.0f), HalfPi));
}

TEST(MathHelper, NearEqual)
{
    EXPECT_TRUE(NearEqual(1.0f, 1.0f));
    EXPECT_TRUE(NearEqual(1.0f, 1.0f + Epsilon * 0.5f));
    EXPECT_FALSE(NearEqual(1.0f, 1.1f));
}

TEST(MathHelper, SmoothStep)
{
    EXPECT_FLOAT_EQ(SmoothStep(0.0f, 1.0f, -1.0f), 0.0f);
    EXPECT_FLOAT_EQ(SmoothStep(0.0f, 1.0f, 2.0f), 1.0f);
    EXPECT_FLOAT_EQ(SmoothStep(0.0f, 1.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(SmoothStep(0.0f, 1.0f, 1.0f), 1.0f);
    // Midpoint of smoothstep(0,1,0.5) = 0.5
    EXPECT_NEAR(SmoothStep(0.0f, 1.0f, 0.5f), 0.5f, 1.0e-6f);
}

TEST(MathHelper, AngleFromXY)
{
    EXPECT_NEAR(AngleFromXY(1.0f, 0.0f), 0.0f, 1.0e-5f);
    EXPECT_NEAR(AngleFromXY(0.0f, 1.0f), HalfPi, 1.0e-5f);
    EXPECT_NEAR(AngleFromXY(-1.0f, 0.0f), Pi, 1.0e-5f);
}
