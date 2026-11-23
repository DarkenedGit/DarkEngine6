#include <gtest/gtest.h>

#include "AI/AiSystem.h"
#include "Assets/AssetManager.h"
#include "Audio/AudioSystem.h"
#include "Audio/SoundComponents.h"
#include "ECS/World.h"
#include "Math/Vector3f.h"

using Dark::AiSystem;
using Dark::AssetManager;
using Dark::Entity;
using Dark::SoundBankComponent;
using Dark::SoundCueDesc;
using Dark::World;
using Dark::addSoundCue;
using Dark::findSoundCue;
using Dark::playSoundCue;
using Dark::Audio::AudioSystem;
using Dark::Audio::PlayDesc;
using Dark::Audio::SoundStimulus;
using Dark::Audio::StimulusKind;
using Dark::Audio::stimulusAudible;
using Dark::Math::Vector3f;

namespace
{
    struct Report
    {
        int           count  = 0;
        Vector3f      pos{};
        uint32_t      source = 0;
        SoundStimulus stimulus{};
    };

    void onReport(void* user, const Vector3f& pos, uint32_t source, const SoundStimulus& stimulus)
    {
        auto* report = static_cast<Report*>(user);
        report->count += 1;
        report->pos       = pos;
        report->source    = source;
        report->stimulus  = stimulus;
    }

    SoundStimulus gun()
    {
        SoundStimulus s;
        s.radius     = 48.0f;
        s.loudness   = 4.0f;
        s.kind       = StimulusKind::Weapon;
        s.positional = true;
        return s;
    }

    PlayDesc taggedAt(const Vector3f& pos, uint32_t source)
    {
        PlayDesc desc{};
        desc.spatial  = true;
        desc.position = pos;
        desc.sourceId = source;
        desc.stimulus = gun();
        return desc;
    }

    void forwardToAi(void* user, const Vector3f& pos, uint32_t source, const SoundStimulus& stimulus)
    {
        static_cast<AiSystem*>(user)->addNoise(pos, stimulus.radius, stimulus.loudness, source, stimulus.kind);
    }
}

TEST(StimulusReport, UntaggedPlayDoesNotReport)
{
    AudioSystem audio;
    Report      report;
    audio.setStimulusReport(&onReport, &report);

    PlayDesc desc{};
    desc.spatial  = true;
    desc.position = { 1.0f, 2.0f, 3.0f };
    EXPECT_EQ(audio.play({}, desc), 0u);
    audio.play3D({}, { 4.0f, 0.0f, 0.0f }, 1.0f);
    EXPECT_EQ(report.count, 0);
}

TEST(StimulusReport, TaggedPlayReportsWhenDeviceIsDown)
{
    AudioSystem audio;
    EXPECT_FALSE(audio.isValid());
    Report report;
    audio.setStimulusReport(&onReport, &report);

    const PlayDesc desc = taggedAt({ 8.0f, 1.0f, 12.0f }, 42u);
    EXPECT_EQ(audio.play({}, desc), 0u);
    EXPECT_EQ(report.count, 1);
    EXPECT_FLOAT_EQ(report.pos.x, 8.0f);
    EXPECT_FLOAT_EQ(report.pos.y, 1.0f);
    EXPECT_FLOAT_EQ(report.pos.z, 12.0f);
    EXPECT_EQ(report.source, 42u);
    EXPECT_FLOAT_EQ(report.stimulus.radius, 48.0f);
    EXPECT_FLOAT_EQ(report.stimulus.loudness, 4.0f);
    EXPECT_EQ(report.stimulus.kind, StimulusKind::Weapon);
}

TEST(StimulusReport, Ui2dLoopAndMusicDoNotReport)
{
    AudioSystem audio;
    Report      report;
    audio.setStimulusReport(&onReport, &report);
    AssetManager assets;
    const auto   clip = audio.createTone(assets, 440.0f, 0.05f, 0.2f);
    ASSERT_TRUE(clip);

    PlayDesc ui = taggedAt({ 1.0f, 0.0f, 0.0f }, 1u);
    ui.ui       = true;
    audio.play(clip, ui);

    PlayDesc loop = taggedAt({ 2.0f, 0.0f, 0.0f }, 1u);
    loop.loop     = true;
    audio.play(clip, loop);

    audio.play2D(clip, 1.0f, false);
    audio.setMusic(clip, 0.2f);
    EXPECT_EQ(report.count, 0);
}

TEST(StimulusReport, VolumeZeroStillReports)
{
    AudioSystem audio;
    Report      report;
    audio.setStimulusReport(&onReport, &report);

    PlayDesc desc = taggedAt({ 3.0f, 1.0f, 4.0f }, 7u);
    desc.volume   = 0.0f;
    audio.play({}, desc);
    audio.setMasterVolume(0.0f);
    audio.play({}, desc);
    EXPECT_EQ(report.count, 2);
    EXPECT_FLOAT_EQ(audio.masterVolume(), 0.0f);
}

TEST(StimulusReport, NonPositionalCueDoesNotReport)
{
    AudioSystem  audio;
    AssetManager assets;
    World        world;
    const auto   clip = audio.createTone(assets, 440.0f, 0.05f, 0.2f);
    ASSERT_TRUE(clip);
    Entity e = world.createEntity();
    world.emplace<Dark::TransformComponent>(e, Dark::TransformComponent{});
    SoundBankComponent bank;
    addSoundCue(bank, "fire", clip->id, 1.0f, false, gun());
    world.emplace<SoundBankComponent>(e, std::move(bank));

    Report report;
    audio.setStimulusReport(&onReport, &report);
    EXPECT_TRUE(playSoundCue(world, audio, assets, e, "fire"));
    EXPECT_EQ(report.count, 0);

    addSoundCue(*world.get<SoundBankComponent>(e), "fire", clip->id, 1.0f, true, gun());
    EXPECT_TRUE(playSoundCue(world, audio, assets, e, "fire"));
    EXPECT_EQ(report.count, 1);
    EXPECT_TRUE(stimulusAudible(report.stimulus));
}

TEST(StimulusReport, HookForwardsIntoAddNoiseUntilCleared)
{
    AudioSystem audio;
    AiSystem    ai;
    audio.setStimulusReport(&forwardToAi, &ai);
    audio.play({}, taggedAt({ 1.0f, 2.0f, 3.0f }, 9u));
    EXPECT_EQ(ai.noiseCount(), 1);

    audio.setStimulusReport(nullptr, nullptr);
    audio.play({}, taggedAt({ 1.0f, 2.0f, 3.0f }, 9u));
    EXPECT_EQ(ai.noiseCount(), 1);
}
