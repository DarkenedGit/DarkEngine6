#include <gtest/gtest.h>

#include "Character/HealthComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Physics/PhysicsWorld.h"
#include "Play/CharacterAttach.h"
#include "Play/PhysicsGround.h"
#include "Play/WeaponTargets.h"
#include "Terrain/HeightMap.h"
#include "Terrain/TerrainGround.h"
#include "Weapons/HittableComponent.h"
#include "Weapons/Weapon.h"

using namespace Dark;
using namespace Dark::Math;

TEST(WeaponTargetList, GatherSkipsSelfAndBodiesWithoutTransform)
{
    World  world;
    Entity self = world.createEntity();
    world.emplace<TransformComponent>(self, Vector3f(0.0f, 0.0f, 0.0f));
    world.emplace<HittableComponent>(self, Vector3f(1.0f, 1.0f, 1.0f));
    world.emplace<HealthComponent>(self);

    Entity other = world.createEntity();
    world.emplace<TransformComponent>(other, Vector3f(3.0f, 1.0f, 4.0f));
    world.emplace<HittableComponent>(other, Vector3f(0.2f, 0.3f, 0.4f));
    world.emplace<HealthComponent>(other);

    Entity dead = world.createEntity();
    world.emplace<TransformComponent>(dead, Vector3f(8.0f, 0.0f, 0.0f));
    world.emplace<HittableComponent>(dead, Vector3f(0.5f, 0.5f, 0.5f));
    world.emplace<HealthComponent>(dead).health.applyDamage(1000.0f);

    Entity noTransform = world.createEntity();
    world.emplace<HittableComponent>(noTransform, Vector3f(1.0f, 1.0f, 1.0f));

    WeaponTargetList list;
    list.gather(world, self);
    ASSERT_EQ(list.targets().size(), 2u);
    EXPECT_EQ(list.targets()[0].entity, other);
    EXPECT_TRUE(list.targets()[0].alive);
    EXPECT_NEAR(list.targets()[0].center.x, 3.0f, 1.0e-5f);
    EXPECT_NEAR(list.targets()[0].halfExtents.y, 0.3f, 1.0e-5f);
    EXPECT_EQ(list.targets()[1].entity, dead);
    EXPECT_FALSE(list.targets()[1].alive);
    EXPECT_FALSE(list.entityAt(-1).valid());
    EXPECT_FALSE(list.entityAt(2).valid());
    EXPECT_EQ(list.entityAt(1), dead);

    WeaponWorldQuery query;
    list.bind(query);
    EXPECT_EQ(weaponTargetCount(query), 2);
    EXPECT_TRUE(weaponTargetAlive(query, 0));
    EXPECT_FALSE(weaponTargetAlive(query, 1));
    EXPECT_FALSE(weaponTargetAlive(query, 4));
    Vector3f center;
    ASSERT_TRUE(weaponTargetCenter(query, 0, center));
    EXPECT_NEAR(center.z, 4.0f, 1.0e-5f);
    const AABox3f box = weaponTargetBox(query, 0);
    EXPECT_NEAR(box.Extents().x, 0.2f, 1.0e-5f);
    EXPECT_EQ(query.targetEntityAt(query.targetUser, 0), other);
    EXPECT_FALSE(query.targetEntityAt(query.targetUser, -1).valid());
}

TEST(CharacterAttach, HumanoidBodySetsHpOnce)
{
    World world;
    Entity fresh = world.createEntity();
    ensureHumanoidBody(world, fresh, 200.0f, Vector3f{ 0.35f, 0.7f, 0.35f });

    HealthComponent* health = world.get<HealthComponent>(fresh);
    ASSERT_NE(health, nullptr);
    EXPECT_FLOAT_EQ(health->health.maxHp(), 200.0f);
    EXPECT_FLOAT_EQ(health->health.hp(), 200.0f);
    HittableComponent* hit = world.get<HittableComponent>(fresh);
    ASSERT_NE(hit, nullptr);
    EXPECT_NEAR(hit->halfExtents.x, 0.35f, 1.0e-5f);
    EXPECT_NEAR(hit->halfExtents.y, 0.7f, 1.0e-5f);
    HitReactionComponent* reaction = world.get<HitReactionComponent>(fresh);
    ASSERT_NE(reaction, nullptr);
    EXPECT_FLOAT_EQ(reaction->hit.settings().stunSeconds, 0.28f);

    health->health.applyDamage(40.0f);
    hit->halfExtents.y = 3.0f;
    ensureHumanoidBody(world, fresh, 50.0f, Vector3f{ 9.0f, 9.0f, 9.0f });
    EXPECT_FLOAT_EQ(health->health.hp(), 160.0f);
    EXPECT_FLOAT_EQ(health->health.maxHp(), 200.0f);
    EXPECT_NEAR(hit->halfExtents.y, 3.0f, 1.0e-5f);

    Entity kept = world.createEntity();
    world.emplace<HealthComponent>(kept);
    ensureHumanoidBody(world, kept, 200.0f, Vector3f{ 0.35f, 0.7f, 0.35f });
    ASSERT_NE(world.get<HealthComponent>(kept), nullptr);
    EXPECT_FLOAT_EQ(world.get<HealthComponent>(kept)->health.maxHp(), 100.0f);
    EXPECT_NEAR(world.get<HittableComponent>(kept)->halfExtents.y, 0.7f, 1.0e-5f);

    ensureHumanoidBody(world, Entity{}, 200.0f, Vector3f{ 1.0f, 1.0f, 1.0f });
}

TEST(PhysicsGround, InvalidWorldKeepsTerrainAndRejectsHeightField)
{
    Physics::PhysicsWorld physics;
    Terrain::HeightMap empty;
    EXPECT_FALSE(physics.valid());
    EXPECT_EQ(bindHeightFieldGround(physics, empty, "Test"), Physics::kNullPhysicsBody);
    EXPECT_FLOAT_EQ(raisedGroundY(physics, Physics::kNullPhysicsBody, Physics::kNullPhysicsBody, 4.5f, 1.0f, 2.0f, 10.0f), 4.5f);
}

TEST(PhysicsGround, HeightFieldAndRaisedSurface)
{
    Physics::PhysicsWorld physics;
    Physics::PhysicsWorldDesc desc;
    desc.enabled = true;
    desc.gravity = 24.0f;
    ASSERT_TRUE(physics.create(desc));

    Terrain::HeightMap empty;
    EXPECT_EQ(bindHeightFieldGround(physics, empty, "Test"), Physics::kNullPhysicsBody);

    Terrain::HeightMap coarse;
    ASSERT_TRUE(coarse.create(2, 2, 1.0f, 1.0f));
    const Physics::PhysicsBodyId field = bindHeightFieldGround(physics, coarse, "Test");
    EXPECT_NE(field, Physics::kNullPhysicsBody);

    Physics::PhysicsBoxDesc box;
    box.position    = Vector3f(0.0f, 2.0f, 0.0f);
    box.halfExtents = Vector3f(1.0f, 1.0f, 1.0f);
    box.dynamic     = false;
    const Physics::PhysicsBodyId deck = physics.createBox(box);
    ASSERT_NE(deck, Physics::kNullPhysicsBody);

    const float raised = raisedGroundY(physics, Physics::kNullPhysicsBody, field, 0.0f, 0.0f, 0.0f, 10.0f);
    EXPECT_GT(raised, 0.0f);
    EXPECT_NEAR(raised, 3.0f, 0.35f);
    EXPECT_FLOAT_EQ(raisedGroundY(physics, Physics::kNullPhysicsBody, deck, 0.0f, 0.0f, 0.0f, 10.0f), 0.0f);
    EXPECT_FLOAT_EQ(raisedGroundY(physics, Physics::kNullPhysicsBody, field, 4.0f, 0.0f, 0.0f, 10.0f), 4.0f);
}

TEST(TerrainGround, NullGridContactIsGrass)
{
    Terrain::TerrainGround ground;
    const Terrain::GroundContact contact = Terrain::groundContactAt(ground, nullptr, 1.0f, 2.0f);
    EXPECT_STREQ(contact.id, "grass");
    EXPECT_STREQ(contact.cue, "step_grass");
}
