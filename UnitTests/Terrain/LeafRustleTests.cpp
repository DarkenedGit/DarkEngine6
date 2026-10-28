#include <gtest/gtest.h>

#include "Assets/AssetManager.h"
#include "Audio/AudioSystem.h"
#include "Audio/SoundClip.h"
#include "Audio/WavFile.h"
#include "Terrain/LeafRustle.h"
#include "Terrain/LeafWind.h"

#include <cmath>
#include <filesystem>
#include <memory>

using namespace Dark;
using namespace Dark::Terrain;

namespace
{
    RustleTree treeAt(uint32_t id, float x, float z, float strength)
    {
        RustleTree tree;
        tree.id       = id;
        tree.x        = x;
        tree.y        = 0.0f;
        tree.z        = z;
        tree.strength = strength;
        return tree;
    }

    int indexOf(const RustleChoice* choices, int count, uint32_t id)
    {
        for (int i = 0; i < count; ++i)
        {
            if (choices[i].id == id)
                return i;
        }
        return -1;
    }

    struct TempDir
    {
        std::filesystem::path path;

        ~TempDir()
        {
            std::error_code ec;
            if (!path.empty())
                std::filesystem::remove_all(path, ec);
        }
    };
}

TEST(LeafRustle, FourNearestInside32)
{
    const RustleTree trees[] = {
        treeAt(5, 5.0f, 0.0f, 0.5f),
        treeAt(12, 12.0f, 0.0f, 0.5f),
        treeAt(18, 18.0f, 0.0f, 0.5f),
        treeAt(25, 25.0f, 0.0f, 0.5f),
        treeAt(31, 31.0f, 0.0f, 0.5f),
        treeAt(40, 40.0f, 0.0f, 0.5f),
    };
    RustleChoice choices[kRustleVoices];
    const int count = selectLeafRustle(trees, 6, 0.0f, 0.0f, nullptr, 0, choices, kRustleVoices);
    EXPECT_EQ(count, 4);
    EXPECT_GE(indexOf(choices, count, 5), 0);
    EXPECT_GE(indexOf(choices, count, 12), 0);
    EXPECT_GE(indexOf(choices, count, 18), 0);
    EXPECT_GE(indexOf(choices, count, 25), 0);
    EXPECT_LT(indexOf(choices, count, 31), 0);
    EXPECT_LT(indexOf(choices, count, 40), 0);
}

TEST(LeafRustle, ModerateWindIsAudibleAndLouderWhenStronger)
{
    EXPECT_GT(leafRustleVolume(0.2f), 0.0f);
    EXPECT_LT(leafRustleVolume(0.2f), leafRustleVolume(0.8f));

    const RustleTree trees[] = {
        treeAt(1, 4.0f, 0.0f, 0.2f),
        treeAt(2, 8.0f, 0.0f, 0.8f),
    };
    RustleChoice choices[kRustleVoices];
    const int count = selectLeafRustle(trees, 2, 0.0f, 0.0f, nullptr, 0, choices, kRustleVoices);
    ASSERT_EQ(count, 2);
    const int calm = indexOf(choices, count, 1);
    const int windy = indexOf(choices, count, 2);
    ASSERT_GE(calm, 0);
    ASSERT_GE(windy, 0);
    EXPECT_LT(choices[calm].volume, choices[windy].volume);
}

TEST(LeafRustle, NoneInside32IsEmpty)
{
    const RustleTree trees[] = { treeAt(1, 40.0f, 0.0f, 1.0f), treeAt(2, -40.0f, 10.0f, 1.0f) };
    RustleChoice choices[kRustleVoices];
    EXPECT_EQ(selectLeafRustle(trees, 2, 0.0f, 0.0f, nullptr, 0, choices, kRustleVoices), 0);
}

TEST(LeafRustle, BehindTheListenerIsEligible)
{
    const RustleTree trees[] = { treeAt(7, 0.0f, -10.0f, 0.4f) };
    RustleChoice choices[kRustleVoices];
    const int count = selectLeafRustle(trees, 1, 0.0f, 0.0f, nullptr, 0, choices, kRustleVoices);
    ASSERT_EQ(count, 1);
    EXPECT_EQ(choices[0].id, 7u);
    EXPECT_FALSE(choices[0].stop);
}

TEST(LeafRustle, SwapMarginKeepsTheHeldTree)
{
    const RustleTree trees[] = {
        treeAt(1, 5.0f, 0.0f, 0.5f),
        treeAt(2, 12.0f, 0.0f, 0.5f),
        treeAt(3, 18.0f, 0.0f, 0.5f),
        treeAt(4, 25.0f, 0.0f, 0.5f),
        treeAt(5, 24.2f, 0.0f, 0.5f),
    };
    const RustleHeld held[] = { { 1, false }, { 2, false }, { 3, false }, { 4, false } };
    RustleChoice choices[kRustleVoices];
    const int count = selectLeafRustle(trees, 5, 0.0f, 0.0f, held, 4, choices, kRustleVoices);
    EXPECT_GE(indexOf(choices, count, 4), 0);
    const int kept = indexOf(choices, count, 4);
    ASSERT_GE(kept, 0);
    EXPECT_FALSE(choices[kept].stop);
    EXPECT_LT(indexOf(choices, count, 5), 0);
}

TEST(LeafRustle, NearerChallengersStopOnlyOneHeld)
{
    // Farthest held is 30 m. Both challengers are closer by more than the 1 m swap margin.
    // 12 m is also inside the margin of the 24 m tree, so a second stop in this call would be wrong.
    EXPECT_LT(8.0f + kRustleSwapMarginM, 30.0f);
    EXPECT_LT(12.0f + kRustleSwapMarginM, 30.0f);
    EXPECT_LT(12.0f + kRustleSwapMarginM, 24.0f);

    const RustleTree trees[] = {
        treeAt(1, 10.0f, 0.0f, 0.5f),
        treeAt(2, 18.0f, 0.0f, 0.5f),
        treeAt(3, 24.0f, 0.0f, 0.5f),
        treeAt(4, 30.0f, 0.0f, 0.5f),
        treeAt(5, 8.0f, 0.0f, 0.5f),
        treeAt(6, 12.0f, 0.0f, 0.5f),
    };
    const RustleHeld held[] = { { 1, false }, { 2, false }, { 3, false }, { 4, false } };
    RustleChoice choices[kRustleVoices];
    const int count = selectLeafRustle(trees, 6, 0.0f, 0.0f, held, 4, choices, kRustleVoices);
    EXPECT_EQ(count, kRustleVoices);

    int      stops   = 0;
    uint32_t stopped = 0;
    for (int i = 0; i < count; ++i)
    {
        if (!choices[i].stop)
            continue;
        ++stops;
        stopped = choices[i].id;
    }
    EXPECT_EQ(stops, 1);
    EXPECT_EQ(stopped, 4u);
    for (uint32_t id = 1; id <= 3; ++id)
    {
        const int kept = indexOf(choices, count, id);
        ASSERT_GE(kept, 0);
        EXPECT_FALSE(choices[kept].stop);
    }
    EXPECT_LT(indexOf(choices, count, 5), 0);
    EXPECT_LT(indexOf(choices, count, 6), 0);
}

TEST(LeafRustle, DroppedTreeIsAStopNotASilentHold)
{
    const RustleTree trees[] = { treeAt(9, 40.0f, 0.0f, 1.0f) };
    const RustleHeld held[] = { { 9, false } };
    RustleChoice choices[kRustleVoices];
    const int count = selectLeafRustle(trees, 1, 0.0f, 0.0f, held, 1, choices, kRustleVoices);
    ASSERT_EQ(count, 1);
    EXPECT_EQ(choices[0].id, 9u);
    EXPECT_TRUE(choices[0].stop);
    EXPECT_FALSE(!choices[0].stop && choices[0].volume == 0.0f);
}

TEST(LeafRustle, EmptyClipStartsNoVoiceAndNoTone)
{
    const RustlePlay play = rustlePlayForClip(false);
    EXPECT_FALSE(play.startVoice);
    EXPECT_FALSE(play.requestTone);
    EXPECT_STREQ(kLeafRustleClipPath, "audio/leaves_rustle.wav");
}

TEST(LeafRustle, PlayDescLoopsSpatialWithoutDopplerOrSource)
{
    const Audio::PlayDesc desc = makeRustlePlayDesc(0.4f, 1.0f, 2.0f, 3.0f);
    EXPECT_TRUE(desc.loop);
    EXPECT_TRUE(desc.spatial);
    EXPECT_FALSE(desc.doppler);
    EXPECT_EQ(desc.sourceId, 0u);
    EXPECT_FLOAT_EQ(desc.position.x, 1.0f);
    EXPECT_FLOAT_EQ(desc.position.y, 2.0f);
    EXPECT_FLOAT_EQ(desc.position.z, 3.0f);
}

TEST(LeafRustle, StopClearsTheVoiceSoAOneShotCanAllocate)
{
    Audio::AudioSystem audio;
    if (!audio.create() || !audio.isValid())
        GTEST_SKIP() << "no audio device";

    const AssetRef<Audio::SoundClip> clip = std::make_shared<Audio::SoundClip>();
    ASSERT_TRUE(clip->createTone(440.0f, 0.05f, 0.2f));
    const Audio::VoiceId looped = audio.play(clip, makeRustlePlayDesc(0.2f, 0.0f, 0.0f, 1.0f));
    if (looped == 0)
        GTEST_SKIP() << "audio device did not start a voice";
    audio.stop(looped);
    Audio::PlayDesc once;
    const Audio::VoiceId shot = audio.play(clip, once);
    EXPECT_NE(shot, 0u);
    if (shot != 0)
        audio.stop(shot);
    audio.shutdown();
}

TEST(LeafRustle, FailedPlayLeavesTheSlotReusable)
{
    std::error_code ec;
    const std::filesystem::path temp = std::filesystem::temp_directory_path(ec);
    if (ec)
        GTEST_SKIP() << "no temp directory";

    TempDir scratch;
    scratch.path = temp / "darkengine6_leaf_rustle";
    const std::filesystem::path wav = scratch.path / "audio" / "leaves_rustle.wav";
    std::filesystem::create_directories(wav.parent_path(), ec);
    ASSERT_FALSE(ec);

    Audio::PcmWav pcm;
    pcm.channels   = 1;
    pcm.sampleRate = 22050;
    pcm.samples    = { 0, 800, -800, 400 };
    ASSERT_TRUE(Audio::writePcmWav(wav, pcm));

    AssetManager assets;
    ASSERT_TRUE(assets.mountDirectory(scratch.path));

    Audio::AudioSystem audio;
    EXPECT_FALSE(audio.isValid());
    const AssetRef<Audio::SoundClip> clip = audio.loadWav(assets, kLeafRustleClipPath);
    ASSERT_TRUE(static_cast<bool>(clip));
    ASSERT_TRUE(clip->valid());
    ASSERT_EQ(audio.play(clip, makeRustlePlayDesc(0.4f, 4.0f, 2.0f, 0.0f)), 0u);

    float crownX   = 0.0f;
    float crownZ   = 0.0f;
    float strength = 0.0f;
    bool  windy    = false;
    for (int z = 0; z < 8 && !windy; ++z)
    {
        for (int x = 0; x < 8; ++x)
        {
            const float wx = static_cast<float>(x) * 2.0f;
            const float wz = static_cast<float>(z) * 2.0f;
            strength       = sampleLeafWind(wx, wz, 1.0).strength;
            if (!(strength > 0.0f))
                continue;
            crownX = wx;
            crownZ = wz;
            windy  = true;
            break;
        }
    }
    ASSERT_TRUE(windy);
    ASSERT_GT(strength, 0.0f);

    LeafField::Crown crown;
    crown.id     = 3;
    crown.x      = crownX;
    crown.seatY  = 2.0f;
    crown.z      = crownZ;
    crown.crownY = 6.0f;

    LeafRustleVoices voices;
    voices.tick(audio, assets, &crown, 1, 0.0f, 0.0f, 1.0, 0.016f);
    for (int s = 0; s < kRustleVoices; ++s)
    {
        EXPECT_EQ(voices.slotTreeId(s), 0u);
        EXPECT_EQ(voices.slotVoice(s), 0u);
    }

    voices.tick(audio, assets, &crown, 1, 0.0f, 0.0f, 1.0, 0.016f);
    for (int s = 0; s < kRustleVoices; ++s)
    {
        EXPECT_EQ(voices.slotTreeId(s), 0u);
        EXPECT_EQ(voices.slotVoice(s), 0u);
    }
}
