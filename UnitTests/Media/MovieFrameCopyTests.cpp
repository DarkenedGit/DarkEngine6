#include <gtest/gtest.h>

#include "Media/MovieFrameCopy.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace Dark;

TEST(MovieFrameCopy, PositiveStrideForcesAlpha)
{
    const uint8_t src[] = {
        1, 2, 3, 0, 4, 5, 6, 9, 0xAB, 0xCD, 0xEF, 0x11,
        7, 8, 9, 4, 10, 11, 12, 128, 0x22, 0x33, 0x44, 0x55,
    };
    uint8_t dst[16];
    std::memset(dst, 0x7F, sizeof(dst));

    ASSERT_TRUE(copyMovieFrameBgra(src, sizeof(src), 12, 2, 2, dst, sizeof(dst)));

    const uint8_t expect[] = {
        1, 2, 3, 255, 4, 5, 6, 255,
        7, 8, 9, 255, 10, 11, 12, 255,
    };
    EXPECT_EQ(0, std::memcmp(dst, expect, sizeof(expect)));
}

TEST(MovieFrameCopy, NegativeStrideFlipsToTopDown)
{
    // Buffer base is the bottom row.
    const uint8_t src[] = {
        9, 10, 11, 12, 13, 14, 15, 16,
        1, 2, 3, 4, 5, 6, 7, 8,
    };
    uint8_t dst[16]{};
    ASSERT_TRUE(copyMovieFrameBgra(src, sizeof(src), -8, 2, 2, dst, sizeof(dst)));

    const uint8_t expect[] = {
        1, 2, 3, 255, 5, 6, 7, 255,
        9, 10, 11, 255, 13, 14, 15, 255,
    };
    EXPECT_EQ(0, std::memcmp(dst, expect, sizeof(expect)));
}

TEST(MovieFrameCopy, RejectsStrideThatOverflowsTheDestination)
{
    uint8_t src[16]{};
    uint8_t dst[16];
    std::memset(dst, 0x5A, sizeof(dst));

    EXPECT_FALSE(copyMovieFrameBgra(src, sizeof(src), 8, 2, 2, dst, 15));
    EXPECT_EQ(dst[0], 0x5A);

    EXPECT_FALSE(copyMovieFrameBgra(src, 8, 8, 2, 2, dst, sizeof(dst)));
    EXPECT_FALSE(copyMovieFrameBgra(src, sizeof(src), 4, 2, 2, dst, sizeof(dst)));
    EXPECT_FALSE(copyMovieFrameBgra(src, sizeof(src), INT32_MIN, 2, 2, dst, sizeof(dst)));
    EXPECT_FALSE(copyMovieFrameBgra(src, 16, 32, 2, 2, dst, sizeof(dst)));
    EXPECT_EQ(dst[0], 0x5A);
}

TEST(MovieFrameCopy, SoftwarePackStopsAt1920x1080)
{
    uint8_t byte = 0;
    EXPECT_FALSE(copyMovieFrameBgra(&byte, 64, 16, 3840, 2160, &byte, 64));
    EXPECT_FALSE(copyMovieFrameBgra(&byte, 64, 16, 1921, 1080, &byte, 64));
    EXPECT_FALSE(copyMovieFrameBgra(&byte, 64, 16, 1920, 1081, &byte, 64));
    EXPECT_EQ(byte, 0);

    const uint32_t       w = 1920;
    const uint32_t       h = 1080;
    const size_t         n = static_cast<size_t>(w) * h * 4u;
    std::vector<uint8_t> src(n, uint8_t{1});
    std::vector<uint8_t> dst(n, 0);
    ASSERT_TRUE(copyMovieFrameBgra(src.data(), src.size(), static_cast<int32_t>(w * 4u), w, h, dst.data(), dst.size()));
    EXPECT_EQ(dst[0], 1);
    EXPECT_EQ(dst[3], 255);
    EXPECT_EQ(dst[n - 1], 255);
}
