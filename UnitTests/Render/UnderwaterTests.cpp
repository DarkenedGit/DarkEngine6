#include <gtest/gtest.h>

#include "Render/Underwater.h"

using Dark::kUnderwaterEaseMeters;
using Dark::underwaterAmount;
using Dark::underwaterTravel;

TEST(Underwater, TravelAboveThePlaneIsZero)
{
    EXPECT_FLOAT_EQ(underwaterTravel(5.0f, -4.0f, 0.0f, 10.0f), 0.0f);
    EXPECT_FLOAT_EQ(underwaterTravel(0.0f, 8.0f, 0.0f, 8.0f), 0.0f);
    EXPECT_FLOAT_EQ(underwaterTravel(-2.0f, -4.0f, 0.0f, 0.0f), 0.0f);
}

TEST(Underwater, TravelBelowUsesTheFullSegment)
{
    EXPECT_FLOAT_EQ(underwaterTravel(-2.0f, -4.0f, 0.0f, 8.0f), 8.0f);
    EXPECT_FLOAT_EQ(underwaterTravel(-2.0f, 0.0f, 0.0f, 6.0f), 6.0f);
}

TEST(Underwater, TravelAboveUsesThePlaneSegment)
{
    EXPECT_FLOAT_EQ(underwaterTravel(-2.0f, 2.0f, 0.0f, 10.0f), 5.0f);
}

TEST(Underwater, TravelStraightUpSkyStopsAtThePlane)
{
    EXPECT_FLOAT_EQ(underwaterTravel(-3.0f, 10000.0f, 0.0f, 10003.0f), 3.0f);
}

TEST(Underwater, AmountRisesAcrossTheEase)
{
    EXPECT_FLOAT_EQ(underwaterAmount(false, -2.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(underwaterAmount(true, 1.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(underwaterAmount(true, 0.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(underwaterAmount(true, -0.5f * kUnderwaterEaseMeters, 0.0f), 0.5f);
    EXPECT_FLOAT_EQ(underwaterAmount(true, -kUnderwaterEaseMeters, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(underwaterAmount(true, -4.0f, 0.0f), 1.0f);
}
