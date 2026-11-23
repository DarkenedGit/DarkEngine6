#include <gtest/gtest.h>

#include "Assets/AssetManager.h"
#include "Audio/AudioSystem.h"
#include "Audio/SoundComponents.h"
#include "Combat/StatusDef.h"
#include "Core/AssetPinTable.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Play/CharacterAudio.h"
#include "Terrain/TerrainGround.h"

using Dark::AssetManager;
using Dark::AssetPinTable;
using Dark::CueOwner;
using Dark::Entity;
using Dark::SoundBankComponent;
using Dark::SoundCueDesc;
using Dark::World;
using Dark::addSoundCue;
using Dark::findSoundCue;
using Dark::stimulusForCue;
using Dark::Audio::AudioSystem;
using Dark::Audio::StimulusKind;
using Dark::Audio::stimulusAudible;
using Dark::Combat::StatusId;
using Dark::Combat::statusDef;

namespace
{
    void expectNone(CueOwner owner, const char* name)
    {
        EXPECT_FALSE(stimulusAudible(stimulusForCue(owner, name))) << name;
    }

    void expectKind(CueOwner owner, const char* name, StimulusKind kind, float radius, float loudness)
    {
        const Dark::Audio::SoundStimulus s = stimulusForCue(owner, name);
        EXPECT_TRUE(stimulusAudible(s)) << name;
        EXPECT_EQ(s.kind, kind) << name;
        EXPECT_FLOAT_EQ(s.radius, radius) << name;
        EXPECT_FLOAT_EQ(s.loudness, loudness) << name;
        EXPECT_TRUE(s.positional) << name;
    }
}

TEST(CueStimulus, Table)
{
    expectKind(CueOwner::Player, "fire", StimulusKind::Weapon, 48.0f, 4.0f);
    expectNone(CueOwner::Hunter, "fire");
    expectKind(CueOwner::Player, "swing", StimulusKind::Weapon, 12.0f, 3.4f);
    expectNone(CueOwner::Hunter, "swing");
    expectKind(CueOwner::Player, "impact", StimulusKind::Impact, 14.0f, 3.4f);
    expectKind(CueOwner::Hunter, "impact", StimulusKind::Impact, 14.0f, 3.4f);
    expectKind(CueOwner::Player, "pain", StimulusKind::Voice, 28.0f, 5.0f);
    expectKind(CueOwner::Hunter, "pain", StimulusKind::Voice, 28.0f, 5.0f);
    expectKind(CueOwner::Hunter, "grunt", StimulusKind::Voice, 18.0f, 3.4f);
    expectNone(CueOwner::Player, "grunt");
    expectKind(CueOwner::Player, "death", StimulusKind::Voice, 36.0f, 5.0f);
    expectNone(CueOwner::Hunter, "death");
    expectNone(CueOwner::Player, "step");
    expectNone(CueOwner::Hunter, "step");
    expectNone(CueOwner::Player, "step_grass");
    expectNone(CueOwner::Hunter, "step_grass");
    expectNone(CueOwner::Player, "status");
    expectNone(CueOwner::Hunter, "status");
    expectNone(CueOwner::Player, "click");
    expectNone(CueOwner::Hunter, "growl");
}

TEST(CueStimulus, ReplaceWritesStimulus)
{
    SoundBankComponent bank;
    addSoundCue(bank, "fire", 1, 1.0f, true, stimulusForCue(CueOwner::Player, "fire"));
    ASSERT_TRUE(stimulusAudible(findSoundCue(bank, "fire")->stimulus));
    addSoundCue(bank, "fire", 1, 1.0f, true, {});
    const SoundCueDesc* cue = findSoundCue(bank, "fire");
    ASSERT_NE(cue, nullptr);
    EXPECT_FALSE(stimulusAudible(cue->stimulus));
}

TEST(CueStimulus, StatusDoesNotUseFire)
{
    const StatusId ids[] = { StatusId::Poison, StatusId::Ignite, StatusId::Bleed, StatusId::Chill, StatusId::Shock, StatusId::Stun };
    for (StatusId id : ids)
    {
        const Dark::Combat::StatusDef* def = statusDef(id);
        ASSERT_NE(def, nullptr);
        EXPECT_STREQ(def->applyCue, "status");
    }
}

TEST(CueStimulus, PlayerDeathIsSpatial)
{
    World                     world;
    AssetPinTable             pins;
    AssetManager              assets;
    AudioSystem               audio;
    Dark::Terrain::TerrainGround ground;
    Entity                    e = world.createEntity();
    world.emplace<Dark::TransformComponent>(e, Dark::TransformComponent{});
    Dark::attachPlayerSounds(world, pins, assets, audio, e, ground);
    const SoundBankComponent* bank = world.get<SoundBankComponent>(e);
    ASSERT_NE(bank, nullptr);
    const SoundCueDesc* death = findSoundCue(*bank, "death");
    ASSERT_NE(death, nullptr);
    EXPECT_TRUE(death->spatial);
    EXPECT_EQ(death->stimulus.kind, StimulusKind::Voice);
    EXPECT_FLOAT_EQ(death->stimulus.radius, 36.0f);
    EXPECT_FLOAT_EQ(death->stimulus.loudness, 5.0f);
    const SoundCueDesc* fire = findSoundCue(*bank, "fire");
    ASSERT_NE(fire, nullptr);
    EXPECT_TRUE(stimulusAudible(fire->stimulus));
    const SoundCueDesc* status = findSoundCue(*bank, "status");
    ASSERT_NE(status, nullptr);
    EXPECT_FALSE(stimulusAudible(status->stimulus));
}
