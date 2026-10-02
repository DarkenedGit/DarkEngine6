#include <gtest/gtest.h>

#include "AI/AiSystem.h"
#include "AI/WolfApproach.h"
#include "Assets/AssetManager.h"
#include "Character/Health.h"
#include "Character/HealthComponent.h"
#include "Combat/JumpAttack.h"
#include "Combat/JumpAttackComponent.h"
#include "Core/AssetPinTable.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Terrain/HeightMap.h"
#include "Terrain/TerrainGrid.h"

#include <cstring>

using namespace Dark::AI;

namespace
{
    WolfNotice farNotice(uint32_t salt)
    {
        WolfNotice notice;
        notice.wolf          = { 0.0f, 0.0f, 20.0f };
        notice.player        = { 0.0f, 0.0f, 0.0f };
        notice.playerForward = { 0.0f, 0.0f, 1.0f };
        notice.horizDistance = 20.0f;
        notice.packCount     = 1;
        notice.salt          = salt;
        return notice;
    }
}

TEST(WolfApproach, LoneFarPicksLeftRightOrBehind)
{
    const WolfApproachPlan left = planWolfApproach(farNotice(0));
    EXPECT_FALSE(left.direct);
    EXPECT_EQ(left.side, WolfApproachSide::Left);
    EXPECT_NEAR(left.staging.x, -kWolfStageRadius, 1.0e-4f);
    EXPECT_NEAR(left.staging.z, 0.0f, 1.0e-4f);
    EXPECT_NEAR(left.backOff.x, 0.0f, 1.0e-4f);
    EXPECT_NEAR(left.backOff.z, 20.0f + kWolfBackOffDistance, 1.0e-4f);

    const WolfApproachPlan right = planWolfApproach(farNotice(1));
    EXPECT_FALSE(right.direct);
    EXPECT_EQ(right.side, WolfApproachSide::Right);
    EXPECT_NEAR(right.staging.x, kWolfStageRadius, 1.0e-4f);
    EXPECT_NEAR(right.staging.z, 0.0f, 1.0e-4f);

    const WolfApproachPlan behind = planWolfApproach(farNotice(2));
    EXPECT_FALSE(behind.direct);
    EXPECT_EQ(behind.side, WolfApproachSide::Behind);
    EXPECT_NEAR(behind.staging.x, 0.0f, 1.0e-4f);
    EXPECT_NEAR(behind.staging.z, -kWolfStageRadius, 1.0e-4f);

    EXPECT_EQ(planWolfApproach(farNotice(3)).side, WolfApproachSide::Left);
}

TEST(WolfApproach, SideFollowsPlayerLook)
{
    WolfNotice notice     = farNotice(0);
    notice.playerForward  = { 1.0f, 0.0f, 0.0f };
    const WolfApproachPlan left = planWolfApproach(notice);
    EXPECT_EQ(left.side, WolfApproachSide::Left);
    EXPECT_NEAR(left.staging.x, 0.0f, 1.0e-4f);
    EXPECT_NEAR(left.staging.z, kWolfStageRadius, 1.0e-4f);

    notice.salt = 1;
    const WolfApproachPlan right = planWolfApproach(notice);
    EXPECT_EQ(right.side, WolfApproachSide::Right);
    EXPECT_NEAR(right.staging.z, -kWolfStageRadius, 1.0e-4f);

    notice.salt = 2;
    const WolfApproachPlan behind = planWolfApproach(notice);
    EXPECT_EQ(behind.side, WolfApproachSide::Behind);
    EXPECT_NEAR(behind.staging.x, -kWolfStageRadius, 1.0e-4f);
    EXPECT_NEAR(behind.staging.z, 0.0f, 1.0e-4f);
}

TEST(WolfApproach, CloseOrPackClosesFromTheFront)
{
    WolfNotice close = farNotice(0);
    close.wolf       = { 0.0f, 0.0f, 3.0f };
    close.horizDistance = 3.0f;
    EXPECT_TRUE(planWolfApproach(close).direct);

    WolfNotice edge = farNotice(1);
    edge.horizDistance = kWolfCloseNotice;
    EXPECT_TRUE(planWolfApproach(edge).direct);

    WolfNotice justFar = farNotice(2);
    justFar.horizDistance = kWolfCloseNotice + 0.05f;
    EXPECT_FALSE(planWolfApproach(justFar).direct);

    WolfNotice pack = farNotice(0);
    pack.packCount  = 2;
    EXPECT_TRUE(planWolfApproach(pack).direct);
}

TEST(WolfApproach, CoincidentOrZeroLookStaysDefined)
{
    WolfNotice stacked = farNotice(0);
    stacked.wolf       = { 4.0f, 1.0f, 2.0f };
    stacked.player     = stacked.wolf;
    stacked.horizDistance = 12.0f;
    const WolfApproachPlan back = planWolfApproach(stacked);
    EXPECT_FALSE(back.direct);
    EXPECT_NEAR(back.backOff.x, 4.0f, 1.0e-4f);
    EXPECT_NEAR(back.backOff.z, 2.0f - kWolfBackOffDistance, 1.0e-4f);

    WolfNotice blind    = farNotice(2);
    blind.playerForward = { 0.0f, 0.0f, 0.0f };
    const WolfApproachPlan behind = planWolfApproach(blind);
    EXPECT_EQ(behind.side, WolfApproachSide::Behind);
    EXPECT_NEAR(behind.staging.x, 0.0f, 1.0e-4f);
    EXPECT_NEAR(behind.staging.z, -kWolfStageRadius, 1.0e-4f);
    EXPECT_NEAR(wolfPlanarForward(blind.playerForward).z, 1.0f, 1.0e-4f);
}

namespace
{
    struct CueCount
    {
        int growl = 0;
        int grunt = 0;
    };

    void countCue(void* user, Dark::Entity, const char* cue)
    {
        auto* count = static_cast<CueCount*>(user);
        if (!cue)
            return;
        if (std::strcmp(cue, "growl") == 0)
            ++count->growl;
        else if (std::strcmp(cue, "grunt") == 0)
            ++count->grunt;
    }

    Dark::Terrain::HeightMap flatMap()
    {
        Dark::Terrain::HeightMap hm;
        EXPECT_TRUE(hm.create(17, 17, 1.0f, 1.0f));
        hm.setOrigin(Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f });
        for (int z = 0; z < 17; ++z)
        {
            for (int x = 0; x < 17; ++x)
                hm.setHeight(x, z, 0.0f);
        }
        return hm;
    }

    struct WolfWorld
    {
        Dark::World                       world;
        Dark::AssetManager                assets;
        Dark::AssetPinTable               pins;
        Dark::AiSystem                    ai;
        Dark::Terrain::TerrainGrid        terrain;
        Dark::Entity                      player{};
        CueCount                          cues;

        bool setup()
        {
            if (!terrain.createFromHeightMap(flatMap(), 16))
                return false;
            Dark::AI::WalkabilityDesc walk;
            walk.heightMap   = &terrain.coarse();
            walk.waterLevel  = -100.0f;
            walk.agentRadius = 0.8f;
            if (!ai.bake(walk))
                return false;
            ai.setHunterCue(&countCue, &cues);
            player = world.createEntity();
            Dark::TransformComponent xf{};
            xf.position = { 8.0f, 1.0f, 8.0f };
            xf.rotation = Dark::Math::Quaternion::IDENTITY;
            world.emplace<Dark::TransformComponent>(player, xf);
            Dark::HealthComponent hc{};
            hc.health = Dark::Health{};
            world.emplace<Dark::HealthComponent>(player, std::move(hc));
            return player.valid();
        }

        Dark::Entity spawnWolf(float x, float z)
        {
            Dark::TransformComponent xf{};
            xf.position = { x, 1.0f, z };
            Dark::Entity e = ai.spawnHunter(world, pins, assets, xf);
            if (!e.valid())
                return {};
            if (Dark::TagComponent* tag = world.get<Dark::TagComponent>(e))
                tag->name = "Wolf";
            if (Dark::AiAgentComponent* agent = world.get<Dark::AiAgentComponent>(e))
            {
                Dark::Math::Vector3f look{ 8.0f - x, 0.0f, 8.0f - z };
                if (look.MagnitudeSqrd() > 1.0e-6f)
                    look.Normalize();
                agent->forward = look;
            }
            return e;
        }
    };
}

TEST(WolfApproach, LoneWolfGrowlsAndBacksOffInsteadOfPouncing)
{
    WolfWorld sim;
    ASSERT_TRUE(sim.setup());
    const Dark::Entity wolf = sim.spawnWolf(8.0f, 2.0f);
    ASSERT_TRUE(wolf.valid());

    sim.ai.tickHunters(sim.world, sim.terrain, false, 1.0f / 60.0f, sim.player);

    const Dark::AiAgentComponent* agent = sim.world.get<Dark::AiAgentComponent>(wolf);
    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->stalk, WolfStalk::BackOff);
    EXPECT_EQ(sim.cues.growl, 1);
    EXPECT_EQ(sim.cues.grunt, 0);
    const Dark::JumpAttackComponent* jump = sim.world.get<Dark::JumpAttackComponent>(wolf);
    ASSERT_NE(jump, nullptr);
    EXPECT_EQ(jump->jump.phase(), Dark::Combat::JumpAttackPhase::Idle);

    const Dark::PathAgentComponent* path = sim.world.get<Dark::PathAgentComponent>(wolf);
    ASSERT_NE(path, nullptr);
    ASSERT_FALSE(path->path.points.empty());
    const Dark::Math::Vector3f& end = path->path.points.back();
    const float away = (end.x - 8.0f) * (end.x - 8.0f) + (end.z - 8.0f) * (end.z - 8.0f);
    EXPECT_GT(away, 16.0f);

    sim.ai.tickHunters(sim.world, sim.terrain, false, 1.0f / 60.0f, sim.player);
    EXPECT_EQ(agent->stalk, WolfStalk::BackOff);
    EXPECT_EQ(sim.cues.growl, 1);
    EXPECT_EQ(sim.cues.grunt, 0);

    sim.ai.tickHunters(sim.world, sim.terrain, false, 1.0f, sim.player);
    EXPECT_EQ(agent->stalk, WolfStalk::Flank);
    EXPECT_EQ(sim.cues.grunt, 0);
    ASSERT_FALSE(path->path.points.empty());
    const Dark::Math::Vector3f stage = wolfStagePoint(Dark::Math::Vector3f{ 8.0f, 0.0f, 8.0f }, Dark::Math::Vector3f{ 0.0f, 0.0f, 1.0f }, agent->stalkSide);
    const Dark::Math::Vector3f& flankEnd = path->path.points.back();
    const float stageMiss = (flankEnd.x - stage.x) * (flankEnd.x - stage.x) + (flankEnd.z - stage.z) * (flankEnd.z - stage.z);
    EXPECT_LT(stageMiss, 4.0f);
    const float fromPlayer = (flankEnd.x - 8.0f) * (flankEnd.x - 8.0f) + (flankEnd.z - 8.0f) * (flankEnd.z - 8.0f);
    EXPECT_GT(fromPlayer, 9.0f);
}

TEST(WolfApproach, CloseNoticeAndPackCloseFromTheFront)
{
    WolfWorld closeSim;
    ASSERT_TRUE(closeSim.setup());
    const Dark::Entity closeWolf = closeSim.spawnWolf(8.0f, 6.0f);
    ASSERT_TRUE(closeWolf.valid());
    closeSim.ai.tickHunters(closeSim.world, closeSim.terrain, false, 1.0f / 60.0f, closeSim.player);
    const Dark::AiAgentComponent* closeAgent = closeSim.world.get<Dark::AiAgentComponent>(closeWolf);
    ASSERT_NE(closeAgent, nullptr);
    EXPECT_EQ(closeAgent->stalk, WolfStalk::Commit);
    EXPECT_EQ(closeSim.cues.growl, 1);

    WolfWorld packSim;
    ASSERT_TRUE(packSim.setup());
    const Dark::Entity a = packSim.spawnWolf(8.0f, 2.0f);
    const Dark::Entity b = packSim.spawnWolf(10.0f, 2.0f);
    ASSERT_TRUE(a.valid());
    ASSERT_TRUE(b.valid());
    packSim.ai.tickHunters(packSim.world, packSim.terrain, false, 1.0f / 60.0f, packSim.player);
    const Dark::AiAgentComponent* agentA = packSim.world.get<Dark::AiAgentComponent>(a);
    const Dark::AiAgentComponent* agentB = packSim.world.get<Dark::AiAgentComponent>(b);
    ASSERT_NE(agentA, nullptr);
    ASSERT_NE(agentB, nullptr);
    EXPECT_EQ(agentA->stalk, WolfStalk::Commit);
    EXPECT_EQ(agentB->stalk, WolfStalk::Commit);
    EXPECT_EQ(packSim.cues.growl, 2);
}
