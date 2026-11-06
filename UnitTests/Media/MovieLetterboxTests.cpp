#include <gtest/gtest.h>

#include "Media/MoviePresentMath.h"

using namespace Dark::MoviePresentMath;

namespace
{

    void expectSame(const MovieLetterbox& a, const MovieLetterbox& b)
    {
        EXPECT_FLOAT_EQ(a.x0, b.x0);
        EXPECT_FLOAT_EQ(a.y0, b.y0);
        EXPECT_FLOAT_EQ(a.x1, b.x1);
        EXPECT_FLOAT_EQ(a.y1, b.y1);
    }

} // namespace

TEST(MovieLetterbox, Window2560x1600Frame1920x1080)
{
    MovieLetterbox rect{};
    ASSERT_TRUE(letterbox(2560, 1600, 1920, 1080, rect));
    EXPECT_FLOAT_EQ(rect.x0, 0.f);
    EXPECT_FLOAT_EQ(rect.x1, 1.f);
    EXPECT_NEAR(rect.y0, 0.05, 1.0e-6);
    EXPECT_NEAR(rect.y1, 0.95, 1.0e-6);
    EXPECT_NEAR(static_cast<double>(rect.y0) * 1600.0, 80.0, 1.0e-3);
    EXPECT_NEAR(static_cast<double>(rect.y1) * 1600.0, 1520.0, 1.0e-3);
}

TEST(MovieLetterbox, Frame3840x2160Matches1080pOnTheSameWindow)
{
    MovieLetterbox hd{};
    MovieLetterbox uhd{};
    ASSERT_TRUE(letterbox(2560, 1600, 1920, 1080, hd));
    ASSERT_TRUE(letterbox(2560, 1600, 3840, 2160, uhd));
    expectSame(hd, uhd);
}

TEST(MovieLetterbox, Frame3840x2160OnMatchingOrLargerTarget)
{
    MovieLetterbox match{};
    ASSERT_TRUE(letterbox(3840, 2160, 3840, 2160, match));
    EXPECT_FLOAT_EQ(match.x0, 0.f);
    EXPECT_FLOAT_EQ(match.y0, 0.f);
    EXPECT_FLOAT_EQ(match.x1, 1.f);
    EXPECT_FLOAT_EQ(match.y1, 1.f);

    MovieLetterbox larger{};
    ASSERT_TRUE(letterbox(7680, 4320, 3840, 2160, larger));
    expectSame(match, larger);
}

TEST(MovieLetterbox, SquareWindowVersus16x9BarsTopAndBottom)
{
    MovieLetterbox rect{};
    ASSERT_TRUE(letterbox(1080, 1080, 1920, 1080, rect));
    EXPECT_FLOAT_EQ(rect.x0, 0.f);
    EXPECT_FLOAT_EQ(rect.x1, 1.f);
    EXPECT_FLOAT_EQ(rect.y0, 0.21875f);
    EXPECT_FLOAT_EQ(rect.y1, 0.78125f);
}

TEST(MovieLetterbox, SquareFrameOn16x9HasSideBars)
{
    MovieLetterbox rect{};
    ASSERT_TRUE(letterbox(1920, 1080, 1080, 1080, rect));
    EXPECT_FLOAT_EQ(rect.y0, 0.f);
    EXPECT_FLOAT_EQ(rect.y1, 1.f);
    EXPECT_FLOAT_EQ(rect.x0, 0.21875f);
    EXPECT_FLOAT_EQ(rect.x1, 0.78125f);
}

TEST(MovieLetterbox, ZeroSizeReturnsEmpty)
{
    MovieLetterbox rect{1.f, 2.f, 3.f, 4.f};
    EXPECT_FALSE(letterbox(0, 1600, 1920, 1080, rect));
    EXPECT_FLOAT_EQ(rect.x0, 0.f);
    EXPECT_FLOAT_EQ(rect.y0, 0.f);
    EXPECT_FLOAT_EQ(rect.x1, 0.f);
    EXPECT_FLOAT_EQ(rect.y1, 0.f);

    rect = {1.f, 2.f, 3.f, 4.f};
    EXPECT_FALSE(letterbox(2560, 1600, 0, 1080, rect));
    EXPECT_FLOAT_EQ(rect.x0, 0.f);
    EXPECT_FLOAT_EQ(rect.y1, 0.f);

    EXPECT_FALSE(letterbox(0, 0, 0, 0, rect));
}
