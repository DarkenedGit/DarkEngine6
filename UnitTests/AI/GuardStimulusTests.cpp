#include <gtest/gtest.h>

#include "AI/AiSystem.h"
#include "AI/Awareness.h"
#include "AI/Brain.h"
#include "Assets/AssetManager.h"
#include "Audio/SoundStimulus.h"
#include "Character/Health.h"
#include "Character/HealthComponent.h"
#include "Character/PlayerStealth.h"
#include "Core/AssetPinTable.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/AABox3f.h"
#include "Terrain/HeightMap.h"
#include "Terrain/TerrainGrid.h"

using namespace Dark::AI;

using Dark::AiAgentComponent;
using Dark::AiSystem;
using Dark::AssetManager;
using Dark::AssetPinTable;
using Dark::BrainComponent;
using Dark::Entity;
using Dark::Health;
using Dark::HealthComponent;
using Dark::TagComponent;
using Dark::TransformComponent;
using Dark::World;
using Dark::Audio::StimulusKind;
using Dark::Math::AABox3f;
using Dark::Math::Vector3f;

namespace
{
    Dark::Terrain::HeightMap flatMap()
    {
        Dark::Terrain::HeightMap hm;
        EXPECT_TRUE(hm.create(17, 17, 1.0f, 1.0f));
        hm.setOrigin(Vector3f{ 0.0f, 0.0f, 0.0f });
        for (int z = 0; z < 17; ++z)
        {
            for (int x = 0; x < 17; ++x)
                hm.setHeight(x, z, 0.0f);
        }
        return hm;
    }

    struct HearWorld
    {
        World                world;
        AssetManager         assets;
        AssetPinTable        pins;
        AiSystem             ai;
        Dark::Terrain::TerrainGrid terrain;
        Entity               player{};

        bool setup()
        {
            if (!terrain.createFromHeightMap(flatMap(), 16))
                return false;
            WalkabilityDesc walk;
            walk.heightMap   = &terrain.coarse();
            walk.waterLevel  = -100.0f;
            walk.agentRadius = 0.8f;
            if (!ai.bake(walk))
                return false;
            player = world.createEntity();
            TransformComponent xf{};
            xf.position = { 400.0f, 1.0f, 400.0f };
            xf.rotation = Dark::Math::Quaternion::IDENTITY;
            world.emplace<TransformComponent>(player, xf);
            HealthComponent hc{};
            hc.health = Health{};
            world.emplace<HealthComponent>(player, std::move(hc));
            ai.setAwarenessEnabled(true);
            return player.valid();
        }

        Entity spawn(float x, float z)
        {
            TransformComponent xf{};
            xf.position = { x, 1.0f, z };
            return ai.spawnHunter(world, pins, assets, xf);
        }

        AiAgentComponent* agent(Entity e) { return world.get<AiAgentComponent>(e); }

        void tick(float dt, const AABox3f* cubes = nullptr, int cubeCount = 0)
        {
            ai.tickHunters(world, terrain, false, dt, player, cubes, cubeCount);
        }
    };

    void addKind(HearWorld& sim, const Vector3f& pos, float range, float loudness, StimulusKind kind, uint32_t source = 0)
    {
        sim.ai.addNoise(pos, range, loudness, source, kind);
    }

    AABox3f cubeAt(float x, float z)
    {
        return AABox3f::FromCenterExtents(Vector3f{ x, 1.5f, z }, Vector3f{ 3.0f, 2.0f, 0.5f });
    }

    void pointBlankCase(float loudness)
    {
        HearWorld sim;
        ASSERT_TRUE(sim.setup());
        const Entity hearer = sim.spawn(8.0f, 4.0f);
        const Entity mate   = sim.spawn(8.0f, 32.0f);
        const Entity farMate    = sim.spawn(8.0f, 62.0f);
        const Entity wolf   = sim.spawn(8.0f, 10.0f);
        ASSERT_TRUE(hearer.valid());
        ASSERT_TRUE(mate.valid());
        ASSERT_TRUE(farMate.valid());
        ASSERT_TRUE(wolf.valid());
        if (TagComponent* tag = sim.world.get<TagComponent>(wolf))
            tag->name = "Wolf";
        sim.agent(hearer)->squad = 7u;
        sim.agent(mate)->squad   = 7u;
        sim.agent(farMate)->squad    = 7u;
        sim.agent(wolf)->squad   = 7u;
        const AABox3f wall = AABox3f::FromCenterExtents({ 8.0f, 1.5f, 17.0f }, { 4.0f, 3.0f, 0.5f });
        const Vector3f shot{ 8.0f, 1.0f, 2.0f };
        addKind(sim, shot, 48.0f, loudness, StimulusKind::Weapon, sim.player.id());
        sim.tick(0.05f, &wall, 1);

        const AiAgentComponent* heard = sim.agent(hearer);
        ASSERT_NE(heard, nullptr);
        EXPECT_EQ(heard->aware.alertness, Alertness::Alert);
        EXPECT_TRUE(heard->hearingAlert);
        EXPECT_FALSE(heard->aware.seesNow);
        EXPECT_FALSE(heard->aware.heardOnly);
        EXPECT_NEAR(heard->aware.stateTime, 0.0f, 1.0e-4f);
        EXPECT_NEAR(heard->aware.noticePos.z, shot.z, 1.0e-3f);
        EXPECT_EQ(sim.agent(mate)->aware.alertness, Alertness::Investigating);
        EXPECT_TRUE(sim.agent(mate)->aware.heardOnly);
        EXPECT_EQ(sim.agent(farMate)->aware.alertness, Alertness::Calm);
        EXPECT_EQ(sim.agent(wolf)->aware.alertness, Alertness::Calm);

        addKind(sim, shot, 48.0f, loudness, StimulusKind::Weapon, sim.player.id());
        sim.tick(1.0f, &wall, 1);
        EXPECT_EQ(sim.agent(hearer)->aware.alertness, Alertness::Alert);
        EXPECT_NEAR(sim.agent(hearer)->aware.stateTime, 0.0f, 1.0e-3f);
        EXPECT_EQ(sim.agent(farMate)->aware.alertness, Alertness::Calm);
        EXPECT_NE(sim.agent(mate)->aware.alertness, Alertness::Alert);

        sim.tick(2.0f, &wall, 1);
        EXPECT_EQ(sim.agent(hearer)->aware.alertness, Alertness::Searching);
        EXPECT_FALSE(sim.agent(hearer)->hearingAlert);
    }
}

TEST(GuardHearing, DelaySamples)
{
    EXPECT_NEAR(soundInvestigateDelaySeconds(47.9f, 48.0f, 4.0f), 4.015f, 5.0e-4f);
    EXPECT_NEAR(soundInvestigateDelaySeconds(4.1f, 48.0f, 4.0f), 0.664f, 5.0e-4f);
    EXPECT_NEAR(soundInvestigateDelaySeconds(0.0f, 48.0f, 4.0f), 0.35f, 1.0e-4f);
    EXPECT_NEAR(soundInvestigateDelaySeconds(24.0f, 48.0f, 1.0f), 5.047f, 5.0e-4f);
}

TEST(GuardHearing, GunshotInvestigatesSoundNotPlayer)
{
    HearWorld sim;
    ASSERT_TRUE(sim.setup());
    const Entity guard = sim.spawn(8.0f, 2.0f);
    ASSERT_TRUE(guard.valid());
    const Vector3f muzzle{ 8.0f, 1.0f, 12.0f };
    addKind(sim, muzzle, 48.0f, 4.0f, StimulusKind::Weapon, sim.player.id());
    sim.tick(0.05f);
    const AiAgentComponent* agent = sim.agent(guard);
    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->aware.alertness, Alertness::Suspicious);
    EXPECT_TRUE(agent->aware.heardOnly);
    EXPECT_NEAR(agent->aware.meter, 0.0f, 1.0e-4f);
    EXPECT_NEAR(agent->aware.stateTime, 0.0f, 1.0e-4f);
    EXPECT_NEAR(agent->aware.noticePos.x, muzzle.x, 1.0e-3f);
    EXPECT_NEAR(agent->aware.noticePos.z, muzzle.z, 1.0e-3f);
    EXPECT_NEAR(agent->soundInvestigateDelay, soundInvestigateDelaySeconds(10.0f, 48.0f, 4.0f), 1.0e-4f);
    EXPECT_NEAR(agent->soundInvestigateDelay, 1.115f, 5.0e-3f);
    EXPECT_NE(agent->aware.noticePos.x, 400.0f);
}

TEST(GuardHearing, WallScalesRadius)
{
    HearWorld open;
    ASSERT_TRUE(open.setup());
    const Entity guard = open.spawn(8.0f, 2.0f);
    ASSERT_TRUE(guard.valid());
    addKind(open, { 8.0f, 1.0f, 32.0f }, 48.0f, 4.0f, StimulusKind::Weapon, open.player.id());
    open.tick(0.05f);
    const AiAgentComponent* openAgent = open.agent(guard);
    ASSERT_NE(openAgent, nullptr);
    EXPECT_EQ(openAgent->aware.alertness, Alertness::Suspicious);
    EXPECT_NEAR(openAgent->aware.meter, 0.0f, 1.0e-4f);

    HearWorld blocked;
    ASSERT_TRUE(blocked.setup());
    const Entity walled = blocked.spawn(8.0f, 2.0f);
    ASSERT_TRUE(walled.valid());
    const AABox3f wall = cubeAt(8.0f, 5.0f);
    addKind(blocked, { 8.0f, 1.0f, 32.0f }, 48.0f, 4.0f, StimulusKind::Weapon, blocked.player.id());
    blocked.tick(0.05f, &wall, 1);
    const AiAgentComponent* walledAgent = blocked.agent(walled);
    ASSERT_NE(walledAgent, nullptr);
    EXPECT_EQ(walledAgent->aware.alertness, Alertness::Calm);
    EXPECT_NEAR(walledAgent->aware.meter, 0.0f, 1.0e-4f);
}

TEST(GuardHearing, SpeakerSkipsSelf)
{
    HearWorld sim;
    ASSERT_TRUE(sim.setup());
    const Entity speaker = sim.spawn(8.0f, 2.0f);
    const Entity mate    = sim.spawn(8.0f, 12.0f);
    ASSERT_TRUE(speaker.valid() && mate.valid());
    addKind(sim, { 8.0f, 1.0f, 2.0f }, 28.0f, 5.0f, StimulusKind::Voice, speaker.id());
    sim.tick(0.05f);
    EXPECT_EQ(sim.agent(speaker)->aware.alertness, Alertness::Calm);
    EXPECT_NEAR(sim.agent(speaker)->aware.meter, 0.0f, 1.0e-4f);
    EXPECT_EQ(sim.agent(mate)->aware.alertness, Alertness::Suspicious);
    EXPECT_NEAR(sim.agent(mate)->aware.noticePos.z, 2.0f, 1.0e-3f);
}

TEST(GuardHearing, AlertVictimIsNotRetargeted)
{
    HearWorld sim;
    ASSERT_TRUE(sim.setup());
    const Entity victim    = sim.spawn(8.0f, 2.0f);
    const Entity bystander = sim.spawn(39.0f, 2.0f);
    ASSERT_TRUE(victim.valid() && bystander.valid());
    sim.tick(0.05f);
    const Vector3f playerPos{ 400.0f, 1.0f, 400.0f };
    sim.ai.forceAlert(sim.world, victim, playerPos);
    EXPECT_EQ(sim.agent(bystander)->aware.alertness, Alertness::Calm);

    addKind(sim, { 8.0f, 1.0f, 2.0f }, 48.0f, 4.0f, StimulusKind::Weapon, sim.player.id());
    addKind(sim, { 39.0f, 1.0f, 18.0f }, 28.0f, 5.0f, StimulusKind::Voice, victim.id());
    sim.tick(0.05f);

    const AiAgentComponent* hit = sim.agent(victim);
    ASSERT_NE(hit, nullptr);
    EXPECT_EQ(hit->aware.alertness, Alertness::Alert);
    EXPECT_FALSE(hit->aware.heardOnly);
    EXPECT_FALSE(hit->hearingAlert);
    EXPECT_NEAR(hit->aware.noticePos.x, playerPos.x, 1.0e-3f);
    EXPECT_NEAR(hit->aware.noticePos.z, playerPos.z, 1.0e-3f);

    const AiAgentComponent* other = sim.agent(bystander);
    ASSERT_NE(other, nullptr);
    EXPECT_EQ(other->aware.alertness, Alertness::Suspicious);
    EXPECT_TRUE(other->aware.heardOnly);
    EXPECT_NEAR(other->aware.meter, 0.0f, 1.0e-4f);
    EXPECT_NEAR(other->aware.noticePos.x, 39.0f, 1.0e-3f);
    EXPECT_NEAR(other->aware.noticePos.z, 18.0f, 1.0e-3f);
}

TEST(GuardHearing, CapKeepsTheLouderEvent)
{
    HearWorld sim;
    ASSERT_TRUE(sim.setup());
    const Entity guard = sim.spawn(8.0f, 2.0f);
    ASSERT_TRUE(guard.valid());
    for (int i = 0; i < 32; ++i)
        addKind(sim, { 8.0f, 1.0f, 2.0f }, 10.0f, 1.0f, StimulusKind::Footstep, sim.player.id());
    addKind(sim, { 8.0f, 1.0f, 12.0f }, 48.0f, 4.0f, StimulusKind::Weapon, sim.player.id());
    EXPECT_EQ(sim.ai.noiseCount(), 32);
    sim.tick(0.05f);
    const AiAgentComponent* agent = sim.agent(guard);
    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->aware.alertness, Alertness::Suspicious);
    EXPECT_NEAR(agent->aware.meter, 0.0f, 1.0e-4f);
    EXPECT_NEAR(agent->aware.noticePos.z, 12.0f, 1.0e-3f);
}

TEST(GuardHearing, FootstepLoudnessUnchanged)
{
    HearWorld sim;
    ASSERT_TRUE(sim.setup());
    AwarenessSettings aware{};
    aware.reactMeter   = 0.9f;
    aware.maxRange     = 5.0f;
    aware.noiseBase    = 0.3f;
    aware.noiseFalloff = 0.5f;
    sim.ai.setAwarenessSettings(aware);
    const Entity guard = sim.spawn(8.0f, 2.0f);
    ASSERT_TRUE(guard.valid());
    sim.ai.addNoise({ 8.0f, 1.0f, 10.0f }, 10.0f);
    sim.tick(1.0f / 60.0f);
    const AiAgentComponent* agent = sim.agent(guard);
    ASSERT_NE(agent, nullptr);
    EXPECT_NEAR(agent->aware.meter, 0.4f, 1.0e-3f);
    EXPECT_EQ(agent->aware.alertness, Alertness::Calm);
}

TEST(GuardHearing, WolfIgnoresGunshot)
{
    HearWorld sim;
    ASSERT_TRUE(sim.setup());
    Dark::PreySense sense{};
    sense.hearRange = 0.0f;
    sim.ai.setPreySense(sense);
    const Entity wolf = sim.spawn(8.0f, 2.0f);
    ASSERT_TRUE(wolf.valid());
    if (TagComponent* tag = sim.world.get<TagComponent>(wolf))
        tag->name = "Wolf";
    addKind(sim, { 8.0f, 1.0f, 3.0f }, 48.0f, 4.0f, StimulusKind::Weapon, sim.player.id());
    sim.tick(0.05f);
    const BrainComponent* brain = sim.world.get<BrainComponent>(wolf);
    ASSERT_NE(brain, nullptr);
    ASSERT_NE(brain->brain, nullptr);
    EXPECT_NE(brain->brain->leaf(), Leaf::Chase);
    EXPECT_EQ(sim.agent(wolf)->aware.alertness, Alertness::Calm);
    EXPECT_NEAR(sim.agent(wolf)->aware.meter, 0.0f, 1.0e-4f);
}

TEST(GuardHearing, PointBlankWeaponAlertsWithoutShout)
{
    pointBlankCase(4.0f);
    pointBlankCase(2.0f);
}

TEST(GuardHearing, FootstepInsideInstantRangeDoesNotAlert)
{
    HearWorld sim;
    ASSERT_TRUE(sim.setup());
    const Entity guard = sim.spawn(8.0f, 2.0f);
    ASSERT_TRUE(guard.valid());
    addKind(sim, { 8.0f, 1.0f, 4.0f }, 10.0f, 1.0f, StimulusKind::Footstep, sim.player.id());
    sim.tick(0.05f);
    const AiAgentComponent* agent = sim.agent(guard);
    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->aware.alertness, Alertness::Calm);
    EXPECT_NEAR(agent->aware.meter, 0.7f, 1.0e-3f);
}

TEST(GuardHearing, ImpactInsideInstantRangeDoesNotAlert)
{
    HearWorld sim;
    ASSERT_TRUE(sim.setup());
    const Entity guard = sim.spawn(8.0f, 2.0f);
    ASSERT_TRUE(guard.valid());
    addKind(sim, { 8.0f, 1.0f, 4.0f }, 14.0f, 3.4f, StimulusKind::Impact, sim.player.id());
    sim.tick(0.05f);
    const AiAgentComponent* agent = sim.agent(guard);
    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->aware.alertness, Alertness::Suspicious);
    EXPECT_NE(agent->aware.alertness, Alertness::Alert);
    EXPECT_NEAR(agent->aware.meter, 0.0f, 1.0e-4f);
    EXPECT_NEAR(agent->soundInvestigateDelay, soundInvestigateDelaySeconds(2.0f, 14.0f, 3.4f), 1.0e-4f);
}

TEST(GuardHearing, EdgeShotStoresLongDelay)
{
    HearWorld sim;
    ASSERT_TRUE(sim.setup());
    const Entity guard = sim.spawn(8.0f, 2.0f);
    ASSERT_TRUE(guard.valid());
    addKind(sim, { 8.0f, 1.0f, 49.9f }, 48.0f, 4.0f, StimulusKind::Weapon, sim.player.id());
    sim.tick(0.05f);
    const AiAgentComponent* agent = sim.agent(guard);
    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->aware.alertness, Alertness::Suspicious);
    EXPECT_NEAR(agent->aware.stateTime, 0.0f, 1.0e-4f);
    EXPECT_NEAR(agent->soundInvestigateDelay, 4.015f, 5.0e-4f);
    sim.tick(1.0f);
    EXPECT_EQ(agent->aware.alertness, Alertness::Suspicious);
    EXPECT_NEAR(agent->aware.stateTime, 1.0f, 1.0e-3f);
}

TEST(GuardHearing, CloseShotStoresShortDelay)
{
    HearWorld sim;
    ASSERT_TRUE(sim.setup());
    const Entity guard = sim.spawn(8.0f, 2.0f);
    ASSERT_TRUE(guard.valid());
    addKind(sim, { 8.0f, 1.0f, 6.1f }, 48.0f, 4.0f, StimulusKind::Weapon, sim.player.id());
    sim.tick(0.05f);
    const AiAgentComponent* agent = sim.agent(guard);
    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->aware.alertness, Alertness::Suspicious);
    EXPECT_NE(agent->aware.alertness, Alertness::Alert);
    EXPECT_NEAR(agent->soundInvestigateDelay, 0.664f, 5.0e-4f);
}

TEST(GuardHearing, SoundShareStopsAtRadius)
{
    HearWorld sound;
    ASSERT_TRUE(sound.setup());
    const Entity hearer = sound.spawn(8.0f, 2.0f);
    const Entity mate   = sound.spawn(8.0f, 62.0f);
    ASSERT_TRUE(hearer.valid() && mate.valid());
    sound.agent(hearer)->squad = 3u;
    sound.agent(mate)->squad   = 3u;
    addKind(sound, { 8.0f, 1.0f, 12.0f }, 48.0f, 4.0f, StimulusKind::Weapon, sound.player.id());
    sound.tick(0.05f);
    EXPECT_EQ(sound.agent(hearer)->aware.alertness, Alertness::Suspicious);
    EXPECT_EQ(sound.agent(mate)->aware.alertness, Alertness::Calm);

    HearWorld sight;
    ASSERT_TRUE(sight.setup());
    const Entity guard = sight.spawn(8.0f, 2.0f);
    const Entity farMate   = sight.spawn(8.0f, 62.0f);
    ASSERT_TRUE(guard.valid() && farMate.valid());
    sight.agent(guard)->squad   = 3u;
    sight.agent(guard)->forward = { 0.0f, 0.0f, 1.0f };
    sight.agent(farMate)->squad     = 3u;
    if (TransformComponent* player = sight.world.get<TransformComponent>(sight.player))
        player->position = { 8.0f, 1.0f, 8.0f };
    sight.ai.setPlayerExposure(1.0f);
    sight.tick(1.0f);
    EXPECT_EQ(sight.agent(guard)->aware.alertness, Alertness::Suspicious);
    EXPECT_FALSE(sight.agent(guard)->aware.heardOnly);
    EXPECT_EQ(sight.agent(farMate)->aware.alertness, Alertness::Investigating);
    EXPECT_NEAR(sight.agent(farMate)->aware.noticePos.z, 8.0f, 1.0e-2f);
}

TEST(GuardHearing, SoundDelayIgnoresInvestigateAfter)
{
    HearWorld sim;
    ASSERT_TRUE(sim.setup());
    AwarenessSettings settings{};
    settings.investigateAfter = 60.0f;
    sim.ai.setAwarenessSettings(settings);
    EXPECT_FLOAT_EQ(sim.ai.awarenessSettings().investigateAfter, 60.0f);
    const Entity guard = sim.spawn(8.0f, 2.0f);
    ASSERT_TRUE(guard.valid());
    addKind(sim, { 8.0f, 1.0f, 49.9f }, 48.0f, 4.0f, StimulusKind::Weapon, sim.player.id());
    sim.tick(0.05f);
    const AiAgentComponent* agent = sim.agent(guard);
    ASSERT_NE(agent, nullptr);
    EXPECT_NEAR(agent->soundInvestigateDelay, 4.015f, 5.0e-4f);
    EXPECT_GT(60.0f, agent->soundInvestigateDelay);
    sim.tick(1.0f);
    EXPECT_EQ(agent->aware.alertness, Alertness::Suspicious);
    sim.tick(3.2f);
    EXPECT_EQ(agent->aware.alertness, Alertness::Investigating);
}

TEST(GuardHearing, LouderSoundShortensDelay)
{
    HearWorld sim;
    ASSERT_TRUE(sim.setup());
    const Entity guard = sim.spawn(8.0f, 2.0f);
    ASSERT_TRUE(guard.valid());
    addKind(sim, { 8.0f, 1.0f, 49.9f }, 48.0f, 4.0f, StimulusKind::Weapon, sim.player.id());
    sim.tick(0.05f);
    EXPECT_EQ(sim.agent(guard)->aware.alertness, Alertness::Suspicious);
    EXPECT_NEAR(sim.agent(guard)->soundInvestigateDelay, 4.015f, 5.0e-4f);
    EXPECT_NEAR(sim.agent(guard)->aware.stateTime, 0.0f, 1.0e-4f);

    sim.tick(1.0f);
    EXPECT_NEAR(sim.agent(guard)->aware.stateTime, 1.0f, 1.0e-3f);
    EXPECT_EQ(sim.agent(guard)->aware.alertness, Alertness::Suspicious);

    addKind(sim, { 8.0f, 1.0f, 6.1f }, 48.0f, 4.0f, StimulusKind::Weapon, sim.player.id());
    sim.tick(0.05f);
    EXPECT_EQ(sim.agent(guard)->aware.alertness, Alertness::Suspicious);
    EXPECT_NEAR(sim.agent(guard)->soundInvestigateDelay, 0.664f, 5.0e-4f);
    EXPECT_NEAR(sim.agent(guard)->aware.stateTime, 0.05f, 1.0e-3f);
    EXPECT_NEAR(sim.agent(guard)->aware.noticePos.z, 6.1f, 1.0e-2f);

    HearWorld held;
    ASSERT_TRUE(held.setup());
    const Entity waiter = held.spawn(8.0f, 2.0f);
    ASSERT_TRUE(waiter.valid());
    addKind(held, { 8.0f, 1.0f, 49.9f }, 48.0f, 4.0f, StimulusKind::Weapon, held.player.id());
    held.tick(0.05f);
    held.tick(0.2f);
    EXPECT_NEAR(held.agent(waiter)->aware.stateTime, 0.2f, 1.0e-3f);
    addKind(held, { 8.0f, 1.0f, 17.0f }, 48.0f, 4.0f, StimulusKind::Weapon, held.player.id());
    held.tick(0.05f);
    EXPECT_NEAR(held.agent(waiter)->soundInvestigateDelay, soundInvestigateDelaySeconds(15.0f, 48.0f, 4.0f), 1.0e-4f);
    EXPECT_NEAR(held.agent(waiter)->aware.stateTime, 0.25f, 1.0e-3f);
    EXPECT_NEAR(held.agent(waiter)->aware.noticePos.z, 17.0f, 1.0e-2f);
    const float keptDelay = held.agent(waiter)->soundInvestigateDelay;
    addKind(held, { 8.0f, 1.0f, 32.0f }, 48.0f, 3.0f, StimulusKind::Weapon, held.player.id());
    held.tick(0.05f);
    EXPECT_NEAR(held.agent(waiter)->soundInvestigateDelay, keptDelay, 1.0e-4f);
    EXPECT_NEAR(held.agent(waiter)->aware.noticePos.z, 17.0f, 1.0e-2f);

    HearWorld walk;
    ASSERT_TRUE(walk.setup());
    const Entity searcher = walk.spawn(8.0f, 2.0f);
    ASSERT_TRUE(searcher.valid());
    addKind(walk, { 8.0f, 1.0f, 49.9f }, 48.0f, 4.0f, StimulusKind::Weapon, walk.player.id());
    walk.tick(0.05f);
    const float farMateDelay = walk.agent(searcher)->soundInvestigateDelay;
    walk.tick(4.1f);
    EXPECT_EQ(walk.agent(searcher)->aware.alertness, Alertness::Investigating);
    walk.tick(2.0f);
    addKind(walk, { 8.0f, 1.0f, 6.1f }, 48.0f, 4.0f, StimulusKind::Weapon, walk.player.id());
    walk.tick(0.05f);
    EXPECT_EQ(walk.agent(searcher)->aware.alertness, Alertness::Investigating);
    EXPECT_NEAR(walk.agent(searcher)->soundInvestigateDelay, farMateDelay, 1.0e-4f);
    EXPECT_NEAR(walk.agent(searcher)->aware.noticePos.z, 6.1f, 1.0e-2f);
    EXPECT_GT(walk.agent(searcher)->aware.stateTime, 0.5f);
}
