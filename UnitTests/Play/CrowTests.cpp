#include <gtest/gtest.h>

#include "Play/Crow.h"

#include "Character/Health.h"
#include "Character/HealthComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"

#include <cmath>
#include <vector>

using namespace Dark;

namespace
{

void step(CrowBrain& brain, float groundY, float seconds, const Math::Vector3f* threats, int threatCount, const CrowPerch* perches, int perchCount, int* caws)
{
    const int ticks = static_cast<int>(seconds / 0.05f);
    for (int i = 0; i < ticks; ++i)
    {
        const bool caw = tickCrow(brain, groundY, 0.05f, nullptr, 0, threats, threatCount, perches, perchCount);
        if (caw && caws)
            *caws += 1;
    }
}

float horiz(const Math::Vector3f& a, const Math::Vector3f& b)
{
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

} // namespace

TEST(Crow, MeshUpWingsAreHigherThanFolded)
{
    MeshData foldedBody;
    MeshData foldedBeak;
    MeshData foldedEyes;
    MeshData upBody;
    MeshData upBeak;
    MeshData upEyes;
    ASSERT_TRUE(buildCrowMesh(foldedBody, foldedBeak, foldedEyes, -1.05f));
    ASSERT_TRUE(buildCrowMesh(upBody, upBeak, upEyes, 0.75f));
    EXPECT_GT(foldedBody.positions.size(), 20u);
    EXPECT_FALSE(foldedBeak.positions.empty());
    EXPECT_FALSE(foldedEyes.positions.empty());

    float foldedMax = -100.0f;
    float upMax     = -100.0f;
    for (const Math::Vector3f& p : foldedBody.positions)
    {
        if (p.y > foldedMax)
            foldedMax = p.y;
    }
    for (const Math::Vector3f& p : upBody.positions)
    {
        if (p.y > upMax)
            upMax = p.y;
    }
    EXPECT_GT(upMax, foldedMax + 0.15f);
}

TEST(Crow, FlockStaysAirborneNearHome)
{
    CrowBrain birds[4];
    const float start[4][2] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { -1.0f, 1.0f }, { 1.0f, 1.0f } };
    for (int i = 0; i < 4; ++i)
        initCrow(birds[i], start[i][0], start[i][1], 0.0f, 0.0f, 0.0f, 11u, i * 3, nullptr, 0);

    for (int tick = 0; tick < 50; ++tick)
    {
        CrowNeighbor neighbors[4];
        for (int i = 0; i < 4; ++i)
        {
            neighbors[i].position = birds[i].position;
            neighbors[i].velocity = birds[i].velocity;
            neighbors[i].flying   = true;
        }
        for (int i = 0; i < 4; ++i)
        {
            CrowNeighbor others[3];
            int n = 0;
            for (int j = 0; j < 4; ++j)
            {
                if (j == i)
                    continue;
                others[n++] = neighbors[j];
            }
            EXPECT_FALSE(tickCrow(birds[i], 0.0f, 0.05f, others, 3, nullptr, 0, nullptr, 0));
        }
    }

    for (int i = 0; i < 4; ++i)
    {
        EXPECT_EQ(birds[i].state, CrowState::Flock);
        EXPECT_GE(birds[i].position.y, 3.0f);
        EXPECT_LT(horiz(birds[i].position, Math::Vector3f{ 0.0f, 0.0f, 0.0f }), 40.0f);
    }
    float closest = 100.0f;
    for (int i = 0; i < 4; ++i)
    {
        for (int j = i + 1; j < 4; ++j)
        {
            const float d = horiz(birds[i].position, birds[j].position);
            if (d < closest)
                closest = d;
        }
    }
    EXPECT_GT(closest, 0.6f);
}

TEST(Crow, ForageHopsAlongTheGround)
{
    CrowBrain brain;
    initCrow(brain, 2.0f, -3.0f, 0.0f, 2.0f, -3.0f, 4u, 2, nullptr, 0);
    ASSERT_EQ(brain.state, CrowState::Forage);
    const Math::Vector3f start = brain.position;
    step(brain, 0.0f, 1.5f, nullptr, 0, nullptr, 0, nullptr);
    EXPECT_EQ(brain.state, CrowState::Forage);
    EXPECT_LT(brain.position.y, 1.0f);
    EXPECT_GT(horiz(brain.position, start), 0.2f);
}

TEST(Crow, PerchHoldsTheBranch)
{
    CrowPerch tree{ 3.0f, 5.5f, 1.0f };
    CrowBrain brain;
    initCrow(brain, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 9u, 1, &tree, 1);
    ASSERT_EQ(brain.state, CrowState::Perch);
    step(brain, 0.0f, 1.0f, nullptr, 0, &tree, 1, nullptr);
    EXPECT_EQ(brain.state, CrowState::Perch);
    EXPECT_NEAR(brain.position.x, tree.x, 0.05f);
    EXPECT_NEAR(brain.position.z, tree.z, 0.05f);
    EXPECT_NEAR(brain.position.y, tree.y, 0.05f);
}

TEST(Crow, ScareWithoutATreeCallsOnceAndRejoinsTheFlock)
{
    CrowPerch tree{ 0.0f, 5.0f, 0.0f };
    CrowBrain brain;
    initCrow(brain, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 3u, 1, &tree, 1);
    ASSERT_EQ(brain.state, CrowState::Perch);

    const Math::Vector3f threat{ 4.0f, 0.0f, 0.0f };
    int caws = 0;
    EXPECT_TRUE(tickCrow(brain, 0.0f, 0.05f, nullptr, 0, &threat, 1, &tree, 1));
    EXPECT_FALSE(tickCrow(brain, 0.0f, 0.05f, nullptr, 0, &threat, 1, &tree, 1));
    EXPECT_EQ(brain.state, CrowState::Flee);
    EXPECT_FALSE(brain.fleeToTree);

    step(brain, 0.0f, 1.0f, &threat, 1, &tree, 1, &caws);
    EXPECT_GT(horiz(brain.position, Math::Vector3f{ 0.0f, 5.0f, 0.0f }), 3.0f);
    EXPECT_NE(brain.state, CrowState::Perch);

    const Math::Vector3f gone{ 80.0f, 0.0f, 0.0f };
    step(brain, 0.0f, 2.0f, &gone, 1, &tree, 1, &caws);
    EXPECT_EQ(caws, 0);
    EXPECT_EQ(brain.state, CrowState::Flock);
    EXPECT_GT(brain.position.y, 3.0f);
}

TEST(Crow, ScareFliesToASaferTree)
{
    CrowBrain brain;
    initCrow(brain, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 6u, 2, nullptr, 0);
    ASSERT_EQ(brain.state, CrowState::Forage);

    CrowPerch tree{ 0.0f, 6.0f, 16.0f };
    const Math::Vector3f threat{ -4.0f, 0.0f, 0.0f };
    int caws = 0;
    EXPECT_TRUE(tickCrow(brain, 0.0f, 0.05f, nullptr, 0, &threat, 1, &tree, 1));
    step(brain, 0.0f, 6.0f, &threat, 1, &tree, 1, &caws);
    EXPECT_EQ(caws, 0);
    EXPECT_EQ(brain.state, CrowState::Perch);
    EXPECT_NEAR(brain.position.x, tree.x, 1.0f);
    EXPECT_NEAR(brain.position.z, tree.z, 1.0f);
    EXPECT_NEAR(brain.position.y, tree.y, 1.0f);
}

TEST(Crow, WorldTickFollowsTheBrainAndDropsCorpses)
{
    World world;
    Entity e = world.createEntity();
    CrowComponent crow;
    initCrow(crow.brain, 1.0f, 2.0f, 0.0f, 1.0f, 2.0f, 8u, 0, nullptr, 0);
    world.emplace<CrowComponent>(e, crow);
    TransformComponent xf;
    xf.position = crow.brain.position;
    xf.scale    = Math::Vector3f{ 1.0f, 1.0f, 1.0f };
    world.emplace<TransformComponent>(e, xf);

    simulateCrows(world, nullptr, nullptr, nullptr, nullptr, 0.4f);
    const CrowComponent* moved = world.get<CrowComponent>(e);
    const TransformComponent* pose = world.get<TransformComponent>(e);
    ASSERT_NE(moved, nullptr);
    ASSERT_NE(pose, nullptr);
    EXPECT_NEAR(pose->position.x, moved->brain.position.x, 1.0e-4f);
    EXPECT_NEAR(pose->position.y, moved->brain.position.y, 1.0e-4f);
    EXPECT_GT(pose->position.y, 3.0f);

    HealthSettings settings;
    settings.maxHp       = 12.0f;
    settings.regenPerSec = 0.0f;
    HealthComponent hp;
    hp.health = Health(settings);
    hp.health.applyDamage(40.0f);
    world.emplace<HealthComponent>(e, hp);
    pose = world.get<TransformComponent>(e);
    world.get<TransformComponent>(e)->position.y = 8.0f;
    simulateCrows(world, nullptr, nullptr, nullptr, nullptr, 1.5f);
    EXPECT_NEAR(world.get<TransformComponent>(e)->position.y, 0.12f, 1.0e-3f);
}

TEST(Spawner, MaintainReplacesACrow)
{
    SpawnerDesc desc{};
    desc.mode         = SpawnerMode::Maintain;
    desc.unit         = SpawnerUnit::Crow;
    desc.count        = 1;
    desc.radius       = 0.0f;
    desc.respawnDelay = 0.0f;
    SpawnerRuntime runtime{};
    bool alive[kSpawnerSlotCount]{};
    SpawnerRequest reqs[kSpawnerSlotCount];

    SpawnerUnit parsed = SpawnerUnit::Hunter;
    ASSERT_TRUE(tryParseSpawnerUnit("crow", parsed));
    EXPECT_EQ(parsed, SpawnerUnit::Crow);
    EXPECT_STREQ(toString(SpawnerUnit::Crow), "crow");

    ASSERT_EQ(tickSpawner(desc, runtime, 0.0f, 0.0f, 0.0f, alive, reqs, kSpawnerSlotCount), 1);
    EXPECT_EQ(reqs[0].unit, SpawnerUnit::Crow);
    alive[0] = false;
    ASSERT_EQ(tickSpawner(desc, runtime, 0.0f, 0.0f, 0.0f, alive, reqs, kSpawnerSlotCount), 1);
    EXPECT_EQ(reqs[0].unit, SpawnerUnit::Crow);
    EXPECT_EQ(reqs[0].slot, 0);
}
