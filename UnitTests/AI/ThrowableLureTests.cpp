#include <gtest/gtest.h>

#include "AI/AiSystem.h"
#include "AI/Awareness.h"
#include "Assets/AssetManager.h"
#include "Character/Health.h"
#include "Character/HealthComponent.h"
#include "Core/AssetPinTable.h"
#include "Core/ContentRoots.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Play/Crow.h"
#include "Play/Spawner.h"
#include "Terrain/HeightMap.h"
#include "Terrain/TerrainGrid.h"
#include "Weapons/ThrowableFlight.h"

#include <cmath>
#include <fstream>
#include <sstream>

using namespace Dark::AI;

namespace
{

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

struct LureWorld
{
    Dark::World                world;
    Dark::AssetManager         assets;
    Dark::AssetPinTable        pins;
    Dark::AiSystem             ai;
    Dark::Terrain::TerrainGrid terrain;
    Dark::Entity               player{};

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
        player = world.createEntity();
        Dark::TransformComponent xf{};
        xf.position = { 80.0f, 1.0f, 80.0f };
        xf.rotation = Dark::Math::Quaternion::IDENTITY;
        world.emplace<Dark::TransformComponent>(player, xf);
        Dark::HealthComponent hc{};
        hc.health = Dark::Health{};
        world.emplace<Dark::HealthComponent>(player, std::move(hc));
        Dark::AI::PackSettings pack = ai.packSettings();
        pack.walkSpeed   = 5.0f;
        pack.sprintSpeed = 5.0f;
        ai.setPackSettings(pack);
        return player.valid();
    }

    Dark::Entity spawnAgent(float x, float z, bool wolf)
    {
        Dark::TransformComponent xf{};
        xf.position = { x, 1.0f, z };
        Dark::Entity e = ai.spawnHunter(world, pins, assets, xf);
        if (!e.valid())
            return {};
        if (wolf)
        {
            if (Dark::TagComponent* tag = world.get<Dark::TagComponent>(e))
                tag->name = "Wolf";
        }
        if (Dark::AiAgentComponent* agent = world.get<Dark::AiAgentComponent>(e))
            agent->forward = { 0.0f, 0.0f, 1.0f };
        return e;
    }
};

Dark::Math::AABox3f cubeWall()
{
    return Dark::Math::AABox3f::FromCenterExtents(Dark::Math::Vector3f{ 8.0f, 1.5f, 5.0f }, Dark::Math::Vector3f{ 3.0f, 2.0f, 0.5f });
}

Leaf brainLeaf(Dark::World& world, Dark::Entity e)
{
    const Dark::BrainComponent* brain = world.get<Dark::BrainComponent>(e);
    if (!brain || !brain->brain)
        return Leaf::Wander;
    return brain->brain->leaf();
}

void placePlayer(LureWorld& sim, float x, float z)
{
    if (Dark::TransformComponent* xf = sim.world.get<Dark::TransformComponent>(sim.player))
        xf->position = { x, 1.0f, z };
}

float horiz(float x0, float z0, float x1, float z1)
{
    const float dx = x1 - x0;
    const float dz = z1 - z0;
    return std::sqrt(dx * dx + dz * dz);
}

Dark::Math::Vector3f agentPos(Dark::World& world, Dark::Entity e)
{
    const Dark::TransformComponent* xf = world.get<Dark::TransformComponent>(e);
    if (!xf)
        return {};
    return xf->position;
}

void postCall(LureWorld& sim, const Dark::Math::Vector3f& at, float callDistance, const Dark::Math::AABox3f* cubes, int cubeCount)
{
    Dark::ThrowableLanding landing;
    landing.itemId   = "rock";
    landing.position = at;
    landing.expired  = false;
    sim.ai.postThrowableLanding(sim.world, landing, callDistance, cubes, cubeCount);
}

void tickFor(LureWorld& sim, Dark::Entity e, float seconds, float& minImpact, const Dark::Math::Vector3f& impact, bool& visitedRing)
{
    const int steps = static_cast<int>(seconds / 0.1f);
    for (int i = 0; i < steps; ++i)
    {
        sim.ai.tickHunters(sim.world, sim.terrain, false, 0.1f, sim.player);
        const Dark::Math::Vector3f pos = agentPos(sim.world, e);
        const float d = horiz(pos.x, pos.z, impact.x, impact.z);
        if (d < minImpact)
            minImpact = d;
        if (minImpact < 1.0f && d > 1.1f && d <= 4.0f)
            visitedRing = true;
    }
}

bool hsmHasFleeToChase()
{
    std::filesystem::path file;
    for (const std::filesystem::path& root : Dark::contentRootCandidates())
    {
        const std::filesystem::path candidate = root / "ai/hunter.hsm.json";
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec) && !ec)
        {
            file = candidate;
            break;
        }
    }
    if (file.empty())
        return true;
    std::ifstream in(file, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    std::size_t cursor = 0;
    while (cursor < text.size())
    {
        const std::size_t from = text.find("\"from\": \"Flee\"", cursor);
        if (from == std::string::npos)
            break;
        const std::size_t end = text.find('}', from);
        if (end == std::string::npos)
            break;
        const std::string object = text.substr(from, end - from);
        if (object.find("\"to\": \"Chase\"") != std::string::npos)
            return true;
        cursor = end + 1;
    }
    return false;
}

} // namespace

TEST(ThrowableLure, ChaseIgnoresImpact)
{
    LureWorld sim;
    ASSERT_TRUE(sim.setup());
    placePlayer(sim, 8.0f, 8.0f);
    const Dark::Entity hunter = sim.spawnAgent(8.0f, 2.0f, false);
    ASSERT_TRUE(hunter.valid());
    sim.ai.tickHunters(sim.world, sim.terrain, false, 0.1f, sim.player);
    ASSERT_EQ(brainLeaf(sim.world, hunter), Leaf::Chase);
    const Dark::Math::Vector3f before = agentPos(sim.world, hunter);
    const int noises = sim.ai.noiseCount();
    postCall(sim, { 1.0f, 1.0f, 2.0f }, 16.0f, nullptr, 0);
    EXPECT_EQ(sim.ai.noiseCount(), noises);
    const Dark::AiAgentComponent* agent = sim.world.get<Dark::AiAgentComponent>(hunter);
    ASSERT_NE(agent, nullptr);
    EXPECT_FALSE(agent->lureSearch);
    sim.ai.tickHunters(sim.world, sim.terrain, false, 0.6f, sim.player);
    EXPECT_EQ(brainLeaf(sim.world, hunter), Leaf::Chase);
    EXPECT_FALSE(agent->lureSearch);
    const Dark::Math::Vector3f after = agentPos(sim.world, hunter);
    EXPECT_GT(after.x, 6.0f);
    EXPECT_GE(after.z, before.z - 0.05f);
}

TEST(ThrowableLure, PatrolSearchesThenResumes)
{
    LureWorld sim;
    ASSERT_TRUE(sim.setup());
    const Dark::Entity hunter = sim.spawnAgent(8.0f, 6.0f, false);
    ASSERT_TRUE(hunter.valid());
    Dark::PatrolComponent patrol;
    patrol.points.push_back({ 12.0f, 1.0f, 6.0f });
    patrol.points.push_back({ 4.0f, 1.0f, 6.0f });
    patrol.next = 0;
    sim.world.emplace<Dark::PatrolComponent>(hunter, std::move(patrol));
    const Dark::Math::Vector3f impact{ 8.0f, 1.0f, 12.0f };
    postCall(sim, impact, 16.0f, nullptr, 0);
    const Dark::AiAgentComponent* agent = sim.world.get<Dark::AiAgentComponent>(hunter);
    ASSERT_NE(agent, nullptr);
    EXPECT_TRUE(agent->lureSearch);
    EXPECT_EQ(brainLeaf(sim.world, hunter), Leaf::Wander);
    float minImpact = 100.0f;
    bool visitedRing = false;
    tickFor(sim, hunter, 8.0f, minImpact, impact, visitedRing);
    EXPECT_LT(minImpact, 1.0f);
    EXPECT_TRUE(visitedRing);
    EXPECT_TRUE(agent->lureSearch);
    tickFor(sim, hunter, 1.2f, minImpact, impact, visitedRing);
    EXPECT_FALSE(agent->lureSearch);
    EXPECT_EQ(brainLeaf(sim.world, hunter), Leaf::Wander);
    const float away = horiz(agentPos(sim.world, hunter).x, agentPos(sim.world, hunter).z, 12.0f, 6.0f);
    tickFor(sim, hunter, 1.5f, minImpact, impact, visitedRing);
    const float back = horiz(agentPos(sim.world, hunter).x, agentPos(sim.world, hunter).z, 12.0f, 6.0f);
    EXPECT_LT(back, away - 1.0f);
}

TEST(ThrowableLure, SightDuringSearchEntersChase)
{
    LureWorld sim;
    ASSERT_TRUE(sim.setup());
    const Dark::Entity wolf = sim.spawnAgent(8.0f, 8.0f, true);
    ASSERT_TRUE(wolf.valid());
    postCall(sim, { 8.0f, 1.0f, 12.0f }, 16.0f, nullptr, 0);
    const Dark::AiAgentComponent* agent = sim.world.get<Dark::AiAgentComponent>(wolf);
    ASSERT_NE(agent, nullptr);
    EXPECT_TRUE(agent->lureSearch);
    sim.ai.tickHunters(sim.world, sim.terrain, false, 0.2f, sim.player);
    EXPECT_TRUE(agent->lureSearch);
    EXPECT_NE(brainLeaf(sim.world, wolf), Leaf::Chase);
    placePlayer(sim, 8.0f, 11.0f);
    sim.ai.tickHunters(sim.world, sim.terrain, false, 0.1f, sim.player);
    EXPECT_FALSE(agent->lureSearch);
    EXPECT_EQ(brainLeaf(sim.world, wolf), Leaf::Chase);
}

TEST(ThrowableLure, CubeHidesAQuietCall)
{
    LureWorld sim;
    ASSERT_TRUE(sim.setup());
    const Dark::Math::AABox3f wall = cubeWall();
    const Dark::Entity wolf = sim.spawnAgent(8.0f, 2.0f, true);
    ASSERT_TRUE(wolf.valid());
    postCall(sim, { 8.0f, 1.0f, 10.0f }, 10.0f, &wall, 1);
    const Dark::AiAgentComponent* agent = sim.world.get<Dark::AiAgentComponent>(wolf);
    ASSERT_NE(agent, nullptr);
    EXPECT_FALSE(agent->lureSearch);
}

TEST(ThrowableLure, LongerCallReachesThroughTheCube)
{
    LureWorld sim;
    ASSERT_TRUE(sim.setup());
    const Dark::Math::AABox3f wall = cubeWall();
    const Dark::Entity wolf = sim.spawnAgent(8.0f, 2.0f, true);
    ASSERT_TRUE(wolf.valid());
    postCall(sim, { 8.0f, 1.0f, 10.0f }, 20.0f, &wall, 1);
    const Dark::AiAgentComponent* agent = sim.world.get<Dark::AiAgentComponent>(wolf);
    ASSERT_NE(agent, nullptr);
    EXPECT_TRUE(agent->lureSearch);
    EXPECT_NEAR(agent->lurePoint.z, 10.0f, 1.0e-4f);
}

TEST(ThrowableLure, SprintHearingEntersChaseWithoutSearch)
{
    LureWorld sim;
    ASSERT_TRUE(sim.setup());
    const Dark::Entity wolf = sim.spawnAgent(8.0f, 8.0f, true);
    ASSERT_TRUE(wolf.valid());
    placePlayer(sim, 8.0f, 3.0f);
    Dark::PreySense sense{};
    sense.hearRange    = 24.0f;
    sense.standoff     = 1.0f;
    sense.targetHeight = 0.5f;
    sim.ai.setPreySense(sense);
    const int noises = sim.ai.noiseCount();
    sim.ai.tickHunters(sim.world, sim.terrain, false, 0.1f, sim.player);
    const Dark::AiAgentComponent* agent = sim.world.get<Dark::AiAgentComponent>(wolf);
    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(brainLeaf(sim.world, wolf), Leaf::Chase);
    EXPECT_FALSE(agent->lureSearch);
    EXPECT_EQ(sim.ai.noiseCount(), noises);
}

TEST(ThrowableLure, SecondImpactRetargetsAndRestarts)
{
    LureWorld sim;
    ASSERT_TRUE(sim.setup());
    const Dark::Math::AABox3f wall = cubeWall();
    const Dark::Entity wolf = sim.spawnAgent(8.0f, 2.0f, true);
    ASSERT_TRUE(wolf.valid());
    postCall(sim, { 8.0f, 1.0f, 10.0f }, 20.0f, &wall, 1);
    Dark::AiAgentComponent* agent = sim.world.get<Dark::AiAgentComponent>(wolf);
    ASSERT_NE(agent, nullptr);
    ASSERT_TRUE(agent->lureSearch);
    float minImpact = 100.0f;
    bool visitedRing = false;
    tickFor(sim, wolf, 8.0f, minImpact, { 8.0f, 1.0f, 10.0f }, visitedRing);
    ASSERT_TRUE(agent->lureSearch);
    EXPECT_LT(agent->lureLeft, 2.0f);
    postCall(sim, { 8.0f, 1.0f, 9.0f }, 20.0f, &wall, 1);
    EXPECT_TRUE(agent->lureSearch);
    EXPECT_NEAR(agent->lurePoint.x, 8.0f, 1.0e-4f);
    EXPECT_NEAR(agent->lurePoint.z, 9.0f, 1.0e-4f);
    EXPECT_GT(agent->lureLeft, 8.5f);
    tickFor(sim, wolf, 8.0f, minImpact, { 8.0f, 1.0f, 9.0f }, visitedRing);
    EXPECT_TRUE(agent->lureSearch);
    tickFor(sim, wolf, 1.2f, minImpact, { 8.0f, 1.0f, 9.0f }, visitedRing);
    EXPECT_FALSE(agent->lureSearch);
}

TEST(ThrowableLure, FleeAnswersAndSightEntersChase)
{
    LureWorld sim;
    ASSERT_TRUE(sim.setup());
    const Dark::Entity hunter = sim.spawnAgent(8.0f, 8.0f, false);
    ASSERT_TRUE(hunter.valid());
    Dark::BrainComponent* brain = sim.world.get<Dark::BrainComponent>(hunter);
    Dark::AiAgentComponent* agent = sim.world.get<Dark::AiAgentComponent>(hunter);
    ASSERT_NE(brain, nullptr);
    ASSERT_NE(agent, nullptr);
    ASSERT_NE(brain->brain, nullptr);
    agent->fleeLeft = 30.0f;
    brain->brain->onFlee();
    ASSERT_EQ(brainLeaf(sim.world, hunter), Leaf::Flee);
    postCall(sim, { 8.0f, 1.0f, 4.0f }, 16.0f, nullptr, 0);
    EXPECT_TRUE(agent->lureSearch);
    EXPECT_EQ(brainLeaf(sim.world, hunter), Leaf::Flee);
    sim.ai.tickHunters(sim.world, sim.terrain, false, 0.4f, sim.player);
    EXPECT_TRUE(agent->lureSearch);
    EXPECT_EQ(brainLeaf(sim.world, hunter), Leaf::Flee);
    EXPECT_LT(agentPos(sim.world, hunter).z, 8.0f);
    EXPECT_FALSE(hsmHasFleeToChase());
    placePlayer(sim, 8.0f, 2.0f);
    sim.ai.tickHunters(sim.world, sim.terrain, false, 0.1f, sim.player);
    EXPECT_FALSE(agent->lureSearch);
    EXPECT_EQ(brainLeaf(sim.world, hunter), Leaf::Chase);
    EXPECT_FALSE(hsmHasFleeToChase());
}

TEST(ThrowableLure, HitPositionDoesNotFollowThePlayer)
{
    LureWorld sim;
    ASSERT_TRUE(sim.setup());
    const Dark::Entity hunter = sim.spawnAgent(8.0f, 8.0f, false);
    ASSERT_TRUE(hunter.valid());
    const Dark::Math::Vector3f hit{ 8.0f, 1.0f, 4.0f };
    placePlayer(sim, hit.x, hit.z);
    postCall(sim, hit, 16.0f, nullptr, 0);
    placePlayer(sim, 2.0f, 8.0f);
    Dark::AiAgentComponent* agent = sim.world.get<Dark::AiAgentComponent>(hunter);
    ASSERT_NE(agent, nullptr);
    EXPECT_TRUE(agent->lureSearch);
    EXPECT_NEAR(agent->lurePoint.x, hit.x, 1.0e-4f);
    EXPECT_NEAR(agent->lurePoint.z, hit.z, 1.0e-4f);
    sim.ai.tickHunters(sim.world, sim.terrain, false, 0.8f, sim.player);
    EXPECT_TRUE(agent->lureSearch);
    EXPECT_NE(brainLeaf(sim.world, hunter), Leaf::Chase);
    EXPECT_LT(agentPos(sim.world, hunter).z, 7.2f);
    EXPECT_NEAR(agentPos(sim.world, hunter).x, 8.0f, 0.75f);
    EXPECT_NEAR(agent->lurePoint.z, hit.z, 1.0e-4f);
}

TEST(ThrowableLure, PostDoesNotAppendNoise)
{
    LureWorld sim;
    ASSERT_TRUE(sim.setup());
    const Dark::Entity hunter = sim.spawnAgent(8.0f, 8.0f, false);
    ASSERT_TRUE(hunter.valid());
    sim.ai.addNoise({ 8.0f, 1.0f, 8.0f }, 10.0f);
    EXPECT_EQ(sim.ai.noiseCount(), 1);
    postCall(sim, { 8.0f, 1.0f, 12.0f }, 16.0f, nullptr, 0);
    EXPECT_EQ(sim.ai.noiseCount(), 1);
    const Dark::AiAgentComponent* agent = sim.world.get<Dark::AiAgentComponent>(hunter);
    ASSERT_NE(agent, nullptr);
    EXPECT_TRUE(agent->lureSearch);
}

TEST(ThrowableLure, ExpiredLandingCallsNobody)
{
    LureWorld sim;
    ASSERT_TRUE(sim.setup());
    const Dark::Entity hunter = sim.spawnAgent(8.0f, 8.0f, false);
    ASSERT_TRUE(hunter.valid());
    Dark::CrowComponent crow;
    Dark::initCrow(crow.brain, 8.0f, 9.0f, 0.0f, 8.0f, 9.0f, 3u, 0, nullptr, 0);
    const Dark::Entity bird = sim.world.createEntity();
    sim.world.emplace<Dark::CrowComponent>(bird, crow);
    sim.ai.addNoise({ 1.0f, 1.0f, 1.0f }, 4.0f);
    Dark::ThrowableLanding landing;
    landing.itemId   = "rock";
    landing.position = { 8.0f, 1.0f, 10.0f };
    landing.expired  = true;
    sim.ai.postThrowableLanding(sim.world, landing, 30.0f, nullptr, 0);
    const Dark::AiAgentComponent* agent = sim.world.get<Dark::AiAgentComponent>(hunter);
    const Dark::CrowComponent* perched = sim.world.get<Dark::CrowComponent>(bird);
    ASSERT_NE(agent, nullptr);
    ASSERT_NE(perched, nullptr);
    EXPECT_FALSE(agent->lureSearch);
    EXPECT_FALSE(perched->brain.lure);
    EXPECT_EQ(sim.ai.noiseCount(), 1);
}

TEST(ThrowableLure, EditorPostsTheSameCall)
{
    LureWorld sim;
    ASSERT_TRUE(sim.setup());
    const Dark::Entity hunter = sim.spawnAgent(4.0f, 4.0f, false);
    ASSERT_TRUE(hunter.valid());
    Dark::CrowComponent crow;
    Dark::initCrow(crow.brain, 4.0f, 6.0f, 0.0f, 4.0f, 6.0f, 9u, 0, nullptr, 0);
    const Dark::Entity bird = sim.world.createEntity();
    sim.world.emplace<Dark::CrowComponent>(bird, crow);
    const Dark::Math::AABox3f wall = cubeWall();
    postCall(sim, { 4.0f, 1.0f, 8.0f }, 12.0f, &wall, 1);
    const Dark::AiAgentComponent* agent = sim.world.get<Dark::AiAgentComponent>(hunter);
    const Dark::CrowComponent* moved = sim.world.get<Dark::CrowComponent>(bird);
    ASSERT_NE(agent, nullptr);
    ASSERT_NE(moved, nullptr);
    EXPECT_TRUE(agent->lureSearch);
    EXPECT_NEAR(agent->lurePoint.z, 8.0f, 1.0e-4f);
    EXPECT_TRUE(moved->brain.lure);
    EXPECT_NEAR(moved->brain.lurePoint.z, 8.0f, 1.0e-4f);
}

TEST(ThrowableLure, AlertNotYetChasingAnswers)
{
    LureWorld sim;
    ASSERT_TRUE(sim.setup());
    sim.ai.setAwarenessEnabled(true);
    const Dark::Entity hunter = sim.spawnAgent(8.0f, 4.0f, false);
    ASSERT_TRUE(hunter.valid());
    sim.ai.forceAlert(sim.world, hunter, { 8.0f, 1.0f, 8.0f });
    ASSERT_NE(brainLeaf(sim.world, hunter), Leaf::Chase);
    postCall(sim, { 8.0f, 1.0f, 12.0f }, 16.0f, nullptr, 0);
    Dark::AiAgentComponent* agent = sim.world.get<Dark::AiAgentComponent>(hunter);
    ASSERT_NE(agent, nullptr);
    EXPECT_TRUE(agent->lureSearch);
    sim.ai.tickHunters(sim.world, sim.terrain, false, 0.6f, sim.player);
    EXPECT_GT(agentPos(sim.world, hunter).z, 6.0f);
    EXPECT_NE(brainLeaf(sim.world, hunter), Leaf::Chase);
}

TEST(ThrowableLure, SpawnerIsNotCalled)
{
    LureWorld sim;
    ASSERT_TRUE(sim.setup());
    const Dark::Entity hunter = sim.spawnAgent(8.0f, 8.0f, false);
    ASSERT_TRUE(hunter.valid());
    sim.world.emplace<Dark::SpawnerComponent>(hunter);
    postCall(sim, { 8.0f, 1.0f, 10.0f }, 20.0f, nullptr, 0);
    const Dark::AiAgentComponent* agent = sim.world.get<Dark::AiAgentComponent>(hunter);
    ASSERT_NE(agent, nullptr);
    EXPECT_FALSE(agent->lureSearch);
}
