#include <gtest/gtest.h>

#include "Audio/AudioSystem.h"
#include "Media/MoviePlayer.h"

#include <chrono>
#include <cstdint>
#include <vector>

using namespace Dark;

namespace
{

    std::vector<int16_t> silence(uint32_t frames)
    {
        return std::vector<int16_t>(static_cast<size_t>(frames) * 2u, 0);
    }

    Audio::StreamDesc stereo48()
    {
        Audio::StreamDesc desc;
        desc.format.sampleRate    = 48000;
        desc.format.channels      = 2;
        desc.format.bitsPerSample = 16;
        return desc;
    }

} // namespace

struct Dark::MovieDecodePcmTestAccess
{
    static void arm(MoviePlayer& player, Audio::AudioSystem* audio, uint32_t stream)
    {
        player.m_decodeSubmitsPcm = true;
        player.m_audio            = audio;
        player.m_stream           = stream;
        player.m_epoch            = 1;
        player.m_liveGeneration   = 1;
        player.m_hasAudio         = true;
        player.m_audioEos         = false;
        player.m_videoEos         = false;
        player.m_shutdown         = false;
        player.m_dropReader       = false;
        player.m_eosSubmitted     = false;
        player.m_streamDropped    = false;
        player.m_state            = MoviePlayState::Playing;
        player.m_commands.clear();
        player.m_audioQueue.clear();
        for (MoviePlayer::VideoSlot& slot : player.m_slot)
            slot.state = MoviePlayer::SlotState::Queued;
    }

    static void push(MoviePlayer& player, std::vector<int16_t> pcm, bool end)
    {
        MoviePlayer::AudioPacket packet;
        packet.pcm   = std::move(pcm);
        packet.epoch = player.m_epoch;
        packet.end   = end;
        player.m_audioQueue.push_back(std::move(packet));
    }

    static void drain(MoviePlayer& player)
    {
        player.drainAudio();
    }

    static bool pumpContinue(MoviePlayer& player)
    {
        bool     prefer   = true;
        uint32_t videos   = 0;
        double   audioSec = 0.0;
        const MoviePlayer::Pump pump = player.pumpOne(player.m_liveGeneration, player.m_epoch, prefer, videos, audioSec, false);
        return pump == MoviePlayer::Pump::Continue;
    }

    static bool pumpIdle(MoviePlayer& player)
    {
        bool     prefer   = true;
        uint32_t videos   = 0;
        double   audioSec = 0.0;
        const MoviePlayer::Pump pump = player.pumpOne(player.m_liveGeneration, player.m_epoch, prefer, videos, audioSec, false);
        return pump == MoviePlayer::Pump::Idle;
    }

    static void markStreamsDone(MoviePlayer& player)
    {
        player.m_videoEos = true;
        player.m_audioEos = true;
    }

    static size_t queued(const MoviePlayer& player)
    {
        return player.m_audioQueue.size();
    }

    static bool eos(const MoviePlayer& player)
    {
        return player.m_eosSubmitted;
    }

};

TEST(MovieDecodePcm, QueueFullSubmitIsNotEndOfStream)
{
    Audio::AudioSystem audio;
    if (!audio.create())
        GTEST_SKIP() << "XAudio2 device is not available";
    const Audio::StreamId id = audio.openStream(stereo48());
    ASSERT_NE(id, 0u);
    audio.pauseStream(id);

    int16_t pcm[960 * 2] = {};
    for (int i = 0; i < 8; ++i)
        ASSERT_TRUE(audio.submitStreamPcm(id, pcm, 960)) << i;
    ASSERT_FALSE(audio.submitStreamPcm(id, pcm, 960));

    MoviePlayer player;
    MovieDecodePcmTestAccess::arm(player, &audio, id);
    MovieDecodePcmTestAccess::push(player, silence(960), true);
    MovieDecodePcmTestAccess::drain(player);

    EXPECT_FALSE(MovieDecodePcmTestAccess::eos(player));
    EXPECT_EQ(MovieDecodePcmTestAccess::queued(player), 1u);

    audio.stopStream(id);
    const Audio::StreamId next = audio.openStream(stereo48());
    ASSERT_NE(next, 0u);
    MovieDecodePcmTestAccess::arm(player, &audio, next);
    MovieDecodePcmTestAccess::push(player, silence(32), true);
    MovieDecodePcmTestAccess::drain(player);
    EXPECT_TRUE(MovieDecodePcmTestAccess::eos(player));
    EXPECT_EQ(MovieDecodePcmTestAccess::queued(player), 0u);
    audio.stopStream(next);
}

TEST(MovieDecodePcm, CapWasFullAndTickIsNotCalled)
{
    Audio::AudioSystem audio;
    if (!audio.create())
        GTEST_SKIP() << "XAudio2 device is not available";
    const Audio::StreamId id = audio.openStream(stereo48());
    ASSERT_NE(id, 0u);
    audio.pauseStream(id);

    int16_t pcm[960 * 2] = {};
    for (int i = 0; i < 8; ++i)
        ASSERT_TRUE(audio.submitStreamPcm(id, pcm, 960)) << i;
    ASSERT_FALSE(audio.submitStreamPcm(id, pcm, 960));

    MoviePlayer player;
    MovieDecodePcmTestAccess::arm(player, &audio, id);
    for (int i = 0; i < 8; ++i)
        MovieDecodePcmTestAccess::push(player, silence(960), false);
    ASSERT_EQ(MovieDecodePcmTestAccess::queued(player), 8u);

    // No MoviePlayer::tick. The decode pump has to submit again while the cap is full.
    const auto started = std::chrono::steady_clock::now();
    const bool blocked = MovieDecodePcmTestAccess::pumpContinue(player);
    const auto waitedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    EXPECT_TRUE(blocked);
    EXPECT_GE(waitedMs, 8);
    EXPECT_EQ(MovieDecodePcmTestAccess::queued(player), 8u);
    EXPECT_FALSE(MovieDecodePcmTestAccess::eos(player));

    audio.resumeStream(id);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (MovieDecodePcmTestAccess::queued(player) >= 8 && std::chrono::steady_clock::now() < deadline)
        MovieDecodePcmTestAccess::pumpContinue(player);

    EXPECT_LT(MovieDecodePcmTestAccess::queued(player), 8u);
    EXPECT_FALSE(MovieDecodePcmTestAccess::eos(player));
    audio.stopStream(id);
}

TEST(MovieDecodePcm, EndPacketSubmitsWhileTickIsNotCalled)
{
    Audio::AudioSystem audio;
    if (!audio.create())
        GTEST_SKIP() << "XAudio2 device is not available";
    const Audio::StreamId id = audio.openStream(stereo48());
    ASSERT_NE(id, 0u);
    audio.pauseStream(id);

    int16_t pcm[960 * 2] = {};
    for (int i = 0; i < 8; ++i)
        ASSERT_TRUE(audio.submitStreamPcm(id, pcm, 960)) << i;
    ASSERT_FALSE(audio.submitStreamPcm(id, pcm, 960));

    MoviePlayer player;
    MovieDecodePcmTestAccess::arm(player, &audio, id);
    MovieDecodePcmTestAccess::push(player, silence(960), true);
    MovieDecodePcmTestAccess::markStreamsDone(player);

    // No MoviePlayer::tick. Source EOS still has to submit the queued tail.
    const auto started = std::chrono::steady_clock::now();
    const bool idled = MovieDecodePcmTestAccess::pumpIdle(player);
    const auto waitedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    EXPECT_FALSE(idled);
    EXPECT_GE(waitedMs, 8);
    EXPECT_EQ(MovieDecodePcmTestAccess::queued(player), 1u);
    EXPECT_FALSE(MovieDecodePcmTestAccess::eos(player));

    audio.resumeStream(id);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!MovieDecodePcmTestAccess::eos(player) && std::chrono::steady_clock::now() < deadline)
        MovieDecodePcmTestAccess::pumpIdle(player);

    EXPECT_TRUE(MovieDecodePcmTestAccess::eos(player));
    EXPECT_EQ(MovieDecodePcmTestAccess::queued(player), 0u);
    audio.stopStream(id);
}
