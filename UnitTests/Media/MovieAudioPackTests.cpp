#include <gtest/gtest.h>

#include "Media/MovieAudioPack.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace Dark;

namespace
{

    bool packOne(const void* samples, uint32_t frames, uint32_t channels, bool float32, std::vector<int16_t>& remainder, std::vector<std::vector<int16_t>>& packets)
    {
        MovieAudioPackInput in;
        in.samples    = samples;
        in.frameCount = frames;
        in.channels   = channels;
        in.float32    = float32;
        return packMovieAudio(in, remainder, packets);
    }

} // namespace

TEST(MovieAudioPack, MonoDuplicates)
{
    const int16_t                mono[] = {42, -7};
    std::vector<int16_t>         remainder;
    std::vector<std::vector<int16_t>> packets;
    ASSERT_TRUE(packOne(mono, 2, 1, false, remainder, packets));
    EXPECT_TRUE(packets.empty());
    ASSERT_EQ(remainder.size(), 4u);
    EXPECT_EQ(remainder[0], 42);
    EXPECT_EQ(remainder[1], 42);
    EXPECT_EQ(remainder[2], -7);
    EXPECT_EQ(remainder[3], -7);
}

TEST(MovieAudioPack, StereoFloatScalesAndClamps)
{
    const float                  stereo[] = {1.f, -1.f, 2.f, -2.f, 0.f, 0.5f};
    std::vector<int16_t>         remainder;
    std::vector<std::vector<int16_t>> packets;
    ASSERT_TRUE(packOne(stereo, 3, 2, true, remainder, packets));
    ASSERT_EQ(remainder.size(), 6u);
    EXPECT_EQ(remainder[0], 32767);
    EXPECT_EQ(remainder[1], -32767);
    EXPECT_EQ(remainder[2], 32767);
    EXPECT_EQ(remainder[3], -32767);
    EXPECT_EQ(remainder[4], 0);
    const int16_t half = static_cast<int16_t>(std::round(0.5f * 32767.f));
    EXPECT_EQ(remainder[5], half);
}

TEST(MovieAudioPack, StereoInt16Copies)
{
    const int16_t                stereo[] = {1234, -2222};
    std::vector<int16_t>         remainder;
    std::vector<std::vector<int16_t>> packets;
    ASSERT_TRUE(packOne(stereo, 1, 2, false, remainder, packets));
    ASSERT_EQ(remainder.size(), 2u);
    EXPECT_EQ(remainder[0], 1234);
    EXPECT_EQ(remainder[1], -2222);
}

TEST(MovieAudioPack, Downmix51DropsLfeAndClampsInFloat)
{
    const int16_t full[] = {32767, 32767, 32767, 32767, 32767, 32767};
    const int16_t silentLfe[] = {32767, 32767, 32767, 0, 32767, 32767};
    const int16_t onlyLfe[] = {0, 0, 0, 32767, 0, 0};
    const int16_t center[] = {0, 0, 1000, 0, 0, 0};

    std::vector<int16_t>              fullRem;
    std::vector<std::vector<int16_t>> packets;
    ASSERT_TRUE(packOne(full, 1, 6, false, fullRem, packets));
    ASSERT_EQ(fullRem.size(), 2u);
    EXPECT_EQ(fullRem[0], 32767);
    EXPECT_EQ(fullRem[1], 32767);

    std::vector<int16_t> silentRem;
    ASSERT_TRUE(packOne(silentLfe, 1, 6, false, silentRem, packets));
    EXPECT_EQ(silentRem, fullRem);

    std::vector<int16_t> lfeRem;
    ASSERT_TRUE(packOne(onlyLfe, 1, 6, false, lfeRem, packets));
    ASSERT_EQ(lfeRem.size(), 2u);
    EXPECT_EQ(lfeRem[0], 0);
    EXPECT_EQ(lfeRem[1], 0);

    std::vector<int16_t> centerRem;
    ASSERT_TRUE(packOne(center, 1, 6, false, centerRem, packets));
    const int16_t expected = static_cast<int16_t>(std::round(kMovieSurroundGain * 1000.f));
    ASSERT_EQ(centerRem.size(), 2u);
    EXPECT_EQ(centerRem[0], expected);
    EXPECT_EQ(centerRem[1], expected);
    EXPECT_GT(expected, 0);
}

TEST(MovieAudioPack, PacketCarriesRemainder)
{
    std::vector<int16_t> first(400, int16_t{1});
    std::vector<int16_t> second(600, int16_t{2});
    std::vector<int16_t> remainder;
    std::vector<std::vector<int16_t>> packets;

    ASSERT_TRUE(packOne(first.data(), 400, 1, false, remainder, packets));
    EXPECT_TRUE(packets.empty());
    EXPECT_EQ(remainder.size(), 800u);

    ASSERT_TRUE(packOne(second.data(), 600, 1, false, remainder, packets));
    ASSERT_EQ(packets.size(), 1u);
    EXPECT_EQ(packets[0].size(), static_cast<size_t>(kMovieAudioPacketFrames) * 2u);
    EXPECT_EQ(packets[0][0], 1);
    EXPECT_EQ(packets[0][799], 1);
    EXPECT_EQ(packets[0][800], 2);
    EXPECT_EQ(remainder.size(), 80u);
    EXPECT_EQ(remainder[0], 2);
    EXPECT_EQ(remainder.back(), 2);
}

TEST(MovieAudioPack, RejectsOtherChannelCounts)
{
    const int16_t sample = 1;
    std::vector<int16_t> remainder{9, 8};
    std::vector<std::vector<int16_t>> packets;
    const uint32_t bad[] = {0, 3, 4, 5, 7, 8};
    for (uint32_t channels : bad)
    {
        EXPECT_FALSE(packOne(&sample, 1, channels, false, remainder, packets)) << channels;
    }
    EXPECT_TRUE(packets.empty());
    ASSERT_EQ(remainder.size(), 2u);
    EXPECT_EQ(remainder[0], 9);

    MovieAudioPackInput missing;
    missing.frameCount = 4;
    missing.channels   = 2;
    EXPECT_FALSE(packMovieAudio(missing, remainder, packets));
    EXPECT_EQ(remainder.size(), 2u);
}
