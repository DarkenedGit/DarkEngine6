#include <gtest/gtest.h>

#include "Assets/AssetManager.h"

#include <chrono>
#include <thread>
#include "Audio/AudioSystem.h"
#include "Audio/SoundClip.h"
#include "Audio/SoundComponents.h"
#include "Core/AssetPinTable.h"
#include "Core/EntityPins.h"
#include "ECS/World.h"

using Dark::AssetManager;
using Dark::AssetPinTable;
using Dark::AssetType;
using Dark::Entity;
using Dark::NULL_ASSET;
using Dark::SoundEmitterComponent;
using Dark::World;
using Dark::onEntityRemoved;
using Dark::pinSoundEmitter;
using Dark::Audio::AudioSystem;

TEST(SoundClipAsset, InternToneHitsTheCache)
{
    AssetManager assets;
    AudioSystem  audio;
    auto         a = audio.createTone(assets, 980.0f, 0.16f, 0.35f);
    auto         b = audio.createTone(assets, 980.0f, 0.16f, 0.35f);
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    EXPECT_EQ(a->id, b->id);
    EXPECT_EQ(assets.assetCount(), 1u);
    EXPECT_EQ(a.get(), b.get());
}

TEST(SoundClipAsset, InternBlipSameKey)
{
    AssetManager assets;
    AudioSystem  audio;
    auto         a = audio.createBlip(assets, 440.0f, 0.05f, 0.2f);
    auto         b = audio.createBlip(assets, 440.0f, 0.05f, 0.2f);
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    EXPECT_EQ(a->type, AssetType::Audio);
    EXPECT_NE(a->id, NULL_ASSET);
    EXPECT_EQ(a->id, b->id);
    EXPECT_EQ(assets.assetCount(), 1u);
}

TEST(SoundClipAsset, StoppedVoiceHoldsClipUntilBufferEnds)
{
    AudioSystem audio;
    if (!audio.create() || !audio.isValid())
        GTEST_SKIP() << "no audio device";

    AssetManager assets;
    auto clip = audio.createBlip(assets, 440.0f, 0.35f, 0.05f);
    ASSERT_TRUE(clip);
    const auto id = clip->id;
    const Dark::Audio::VoiceId voice = audio.play2D(clip, 0.0f, false);
    if (voice == 0)
        GTEST_SKIP() << "voice did not start";
    clip.reset();

    audio.stop(voice);
    assets.collectGarbage();
    EXPECT_TRUE(assets.get(id)) << "PCM must stay until XAudio2 finishes the buffer";

    bool gone = false;
    for (int i = 0; i < 50; ++i)
    {
        audio.tick();
        assets.collectGarbage();
        if (!assets.get(id))
        {
            gone = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(gone);
}

TEST(SoundClipAsset, PinHoldsThroughCollectGarbage)
{
    AssetManager assets;
    AssetPinTable pins;
    AudioSystem   audio;
    auto          clip = audio.createBlip(assets, 880.0f, 0.04f, 0.2f);
    ASSERT_TRUE(clip);
    const auto id = clip->id;

    World world;
    Entity e = world.createEntity();
    SoundEmitterComponent se{};
    se.clipId = id;
    world.emplace<SoundEmitterComponent>(e, se);
    pinSoundEmitter(pins, assets, se);
    clip.reset();
    assets.collectGarbage();
    EXPECT_TRUE(assets.get(id) != nullptr);

    onEntityRemoved(world, e, &pins);
    world.destroyEntity(e);
    assets.collectGarbage();
    EXPECT_TRUE(assets.get(id) == nullptr);
}

TEST(SoundClipAsset, SoundBankPinAndFind)
{
    AssetManager assets;
    AssetPinTable pins;
    AudioSystem   audio;
    auto          clip = audio.createBlip(assets, 660.0f, 0.04f, 0.2f);
    ASSERT_TRUE(clip);

    World world;
    Entity e = world.createEntity();
    Dark::SoundBankComponent bank;
    Dark::addSoundCue(bank, "pain", clip->id, 0.5f, true);
    Dark::setSoundBank(world, pins, assets, e, std::move(bank));
    clip.reset();
    assets.collectGarbage();
    EXPECT_TRUE(assets.get(world.get<Dark::SoundBankComponent>(e)->cues[0].clipId) != nullptr);
    const Dark::SoundCueDesc* cue = Dark::findSoundCue(*world.get<Dark::SoundBankComponent>(e), "pain");
    ASSERT_NE(cue, nullptr);
    EXPECT_FLOAT_EQ(cue->volume, 0.5f);
    EXPECT_TRUE(cue->spatial);

    const Dark::AssetID id = cue->clipId;
    onEntityRemoved(world, e, &pins);
    world.destroyEntity(e);
    assets.collectGarbage();
    EXPECT_TRUE(assets.get(id) == nullptr);
}
