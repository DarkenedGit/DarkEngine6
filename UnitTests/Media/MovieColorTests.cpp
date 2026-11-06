#include <gtest/gtest.h>

#include "Media/MovieColor.h"

using namespace Dark::MovieColor;

namespace
{

    Bgra limited(float y, float cb, float cr, uint32_t height)
    {
        return sample(y, cb, cr, height, Range::Limited);
    }

    Bgra full(float y, float cb, float cr, uint32_t height)
    {
        return sample(y, cb, cr, height, Range::Full);
    }

    void expectBlack(const Bgra& px)
    {
        EXPECT_EQ(px.b, 0);
        EXPECT_EQ(px.g, 0);
        EXPECT_EQ(px.r, 0);
        EXPECT_EQ(px.a, 255);
    }

    void expectWhite(const Bgra& px)
    {
        EXPECT_EQ(px.b, 255);
        EXPECT_EQ(px.g, 255);
        EXPECT_EQ(px.r, 255);
        EXPECT_EQ(px.a, 255);
    }

} // namespace

TEST(MovieColor, LimitedBt709LockedVector)
{
    const Bgra px = limited(81.f, 90.f, 240.f, 720);
    EXPECT_EQ(px.r, 255);
    EXPECT_EQ(px.g, 24);
    EXPECT_EQ(px.b, 0);
    EXPECT_EQ(px.a, 255);
}

TEST(MovieColor, LimitedAndFullBlackWhite)
{
    expectBlack(limited(16.f, 128.f, 128.f, 1080));
    expectWhite(limited(235.f, 128.f, 128.f, 1080));
    expectBlack(full(0.f, 128.f, 128.f, 1080));
    expectWhite(full(255.f, 128.f, 128.f, 1080));
}

TEST(MovieColor, MatrixSwitchesAt576Versus720)
{
    const Bgra sd   = limited(81.f, 90.f, 240.f, 576);
    const Bgra pal  = limited(81.f, 90.f, 240.f, 480);
    const Bgra hd   = limited(81.f, 90.f, 240.f, 720);
    const Bgra fhd  = limited(81.f, 90.f, 240.f, 1080);
    const Bgra uhd  = limited(81.f, 90.f, 240.f, 2160);
    const Bgra edge = limited(81.f, 90.f, 240.f, 577);

    EXPECT_EQ(sd.r, pal.r);
    EXPECT_EQ(sd.g, pal.g);
    EXPECT_EQ(sd.b, pal.b);
    EXPECT_EQ(sd.a, 255);

    EXPECT_EQ(hd.r, 255);
    EXPECT_EQ(hd.g, 24);
    EXPECT_EQ(hd.b, 0);
    EXPECT_EQ(fhd.r, hd.r);
    EXPECT_EQ(fhd.g, hd.g);
    EXPECT_EQ(fhd.b, hd.b);
    EXPECT_EQ(uhd.r, hd.r);
    EXPECT_EQ(uhd.g, hd.g);
    EXPECT_EQ(uhd.b, hd.b);
    EXPECT_EQ(edge.r, hd.r);
    EXPECT_EQ(edge.g, hd.g);
    EXPECT_EQ(edge.b, hd.b);

    EXPECT_TRUE(sd.r != hd.r || sd.g != hd.g || sd.b != hd.b);
}
