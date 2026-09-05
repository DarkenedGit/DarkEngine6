#include <gtest/gtest.h>

#include "Audio/WavFile.h"
#include "Audio/SoundClip.h"

#include <filesystem>
#include <string>

using namespace Dark;
using namespace Audio;

namespace
{

std::filesystem::path tempWavPath()
{
    return std::filesystem::temp_directory_path() / "darkengine6_wav_test.wav";
}

} // namespace

TEST(WavFile, RoundTripMono16)
{
    PcmWav src{};
    src.channels   = 1;
    src.sampleRate = 22050;
    src.samples    = { 0, 1111, -2222, 32767, -32767, 50 };

    const auto path = tempWavPath();
    ASSERT_TRUE(writePcmWav(path, src));

    PcmWav loaded{};
    ASSERT_TRUE(loadPcmWav(path, loaded));
    EXPECT_EQ(loaded.channels, 1);
    EXPECT_EQ(loaded.sampleRate, 22050u);
    ASSERT_EQ(loaded.samples.size(), src.samples.size());
    for (size_t i = 0; i < src.samples.size(); ++i)
        EXPECT_EQ(loaded.samples[i], src.samples[i]);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(SoundClip, ToneAndBlipAreValidMono)
{
    SoundClip tone;
    ASSERT_TRUE(tone.createTone(440.0f, 0.05f, 0.2f, 44100));
    EXPECT_TRUE(tone.valid());
    EXPECT_EQ(tone.channels(), 1);
    EXPECT_EQ(tone.sampleRate(), 44100u);
    EXPECT_GT(tone.frameCount(), 100u);

    SoundClip blip;
    ASSERT_TRUE(blip.createBlip(880.0f, 0.08f, 0.4f, 44100));
    EXPECT_TRUE(blip.valid());
    EXPECT_GT(blip.frameCount(), 100u);
}

TEST(SoundClip, RejectsBadToneArgs)
{
    SoundClip clip;
    EXPECT_FALSE(clip.createTone(0.0f, 0.1f));
    EXPECT_FALSE(clip.createTone(440.0f, 0.0f));
    EXPECT_FALSE(clip.createBlip(-1.0f, 0.1f));
}
