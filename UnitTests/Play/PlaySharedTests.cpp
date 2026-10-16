#include <gtest/gtest.h>

#include "Character/HealthComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Play/WeaponTargets.h"
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

TEST(TerrainGround, NullGridContactIsGrass)
{
    Terrain::TerrainGround ground;
    const Terrain::GroundContact contact = Terrain::groundContactAt(ground, nullptr, 1.0f, 2.0f);
    EXPECT_STREQ(contact.id, "grass");
    EXPECT_STREQ(contact.cue, "step_grass");
}
