#include <gtest/gtest.h>

#include "Audio/AudioSystem.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

using namespace Dark::Audio;

namespace
{

    StreamDesc stereo48()
    {
        StreamDesc desc;
        desc.format.sampleRate    = 48000;
        desc.format.channels      = 2;
        desc.format.bitsPerSample = 16;
        return desc;
    }

    bool openDevice(AudioSystem& audio, StreamId& id)
    {
        if (!audio.create())
            return false;
        id = audio.openStream(stereo48());
        return id != 0;
    }

} // namespace

TEST(AudioStream, SubmitDoesNotEndTheVoice)
{
    AudioSystem audio;
    StreamId    id = 0;
    if (!openDevice(audio, id))
        GTEST_SKIP() << "XAudio2 device is not available";

    constexpr uint32_t kFrames = 480;
    int16_t            pcm[kFrames * 2] = {};
    ASSERT_TRUE(audio.submitStreamPcm(id, pcm, kFrames));

    const auto firstDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (audio.streamBuffersQueued(id) != 0 && std::chrono::steady_clock::now() < firstDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ASSERT_EQ(audio.streamBuffersQueued(id), 0u);
    ASSERT_GE(audio.streamSamplesPlayed(id), static_cast<uint64_t>(kFrames));

    const uint64_t settled = audio.streamSamplesPlayed(id);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_EQ(audio.streamSamplesPlayed(id), settled);

    ASSERT_TRUE(audio.submitStreamPcm(id, pcm, kFrames));
    const auto secondDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (audio.streamSamplesPlayed(id) <= settled && std::chrono::steady_clock::now() < secondDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_GT(audio.streamSamplesPlayed(id), settled);
    EXPECT_TRUE(audio.streamActive(id));
    audio.stopStream(id);
}

TEST(AudioStream, FullQueueReturnsFalseAndStaysOpen)
{
    AudioSystem audio;
    StreamId    id = 0;
    if (!openDevice(audio, id))
        GTEST_SKIP() << "XAudio2 device is not available";

    audio.pauseStream(id);
    constexpr uint32_t kFrames = 960;
    int16_t            pcm[kFrames * 2] = {};
    for (int i = 0; i < 8; ++i)
        ASSERT_TRUE(audio.submitStreamPcm(id, pcm, kFrames)) << i;
    EXPECT_FALSE(audio.submitStreamPcm(id, pcm, kFrames));
    EXPECT_TRUE(audio.streamActive(id));
    EXPECT_EQ(audio.streamBuffersQueued(id), 8u);
    audio.stopStream(id);
    EXPECT_FALSE(audio.streamActive(id));
    EXPECT_FALSE(audio.submitStreamPcm(id, pcm, kFrames));
}

TEST(AudioStream, StopDuringSubmitDoesNotUseDestroyedVoice)
{
    AudioSystem audio;
    if (!audio.create())
        GTEST_SKIP() << "XAudio2 device is not available";

    int16_t pcm[64] = {};
    for (int n = 0; n < 8; ++n)
    {
        const StreamId id = audio.openStream(stereo48());
        ASSERT_NE(id, 0u);

        std::atomic<bool> run{true};
        std::atomic<int>  submits{0};
        std::thread       feeder([&] {
            while (run.load(std::memory_order_acquire))
            {
                submits.fetch_add(1, std::memory_order_release);
                audio.submitStreamPcm(id, pcm, 32);
            }
        });

        while (submits.load(std::memory_order_acquire) == 0)
            std::this_thread::yield();
        audio.stopStream(id);
        run.store(false, std::memory_order_release);
        feeder.join();

        EXPECT_FALSE(audio.streamActive(id));
        EXPECT_FALSE(audio.submitStreamPcm(id, pcm, 32));
    }
}
