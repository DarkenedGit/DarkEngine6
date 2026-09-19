#include <gtest/gtest.h>

#include "AI/AiSystem.h"
#include "Assets/AssetManager.h"
#include "Character/HealthComponent.h"
#include "Combat/CombatSystem.h"
#include "Combat/DamageEvent.h"
#include "Combat/JumpAttackComponent.h"
#include "Combat/JumpAttackResolve.h"
#include "Combat/PoiseComponent.h"
#include "Combat/StatusEffectComponent.h"
#include "Core/AssetPinTable.h"
#include "ECS/World.h"
#include "Weapons/HittableComponent.h"

using Dark::AiAgentComponent;
using Dark::AiSystem;
using Dark::AssetManager;
using Dark::AssetPinTable;
using Dark::BrainComponent;
using Dark::Entity;
using Dark::HealthComponent;
using Dark::HittableComponent;
using Dark::TransformComponent;
using Dark::World;

TEST(AiAgentEntity, SpawnHasBrainHealthHittable)
{
    World          world;
    AssetManager   assets;
    AssetPinTable  pins;
    AiSystem       ai;

    TransformComponent xf{};
    xf.position = Dark::Math::Vector3f{ 1.0f, 2.0f, 3.0f };
    xf.scale    = Dark::Math::Vector3f{ 2.0f, 2.0f, 2.0f };
    Entity e    = ai.spawnHunter(world, pins, assets, xf);
    ASSERT_TRUE(e.valid());
    ASSERT_TRUE(world.has<HealthComponent>(e));
    ASSERT_TRUE(world.has<HittableComponent>(e));
    ASSERT_TRUE(world.has<AiAgentComponent>(e));
    ASSERT_TRUE(world.has<BrainComponent>(e));
    ASSERT_NE(world.get<BrainComponent>(e)->brain, nullptr);
    EXPECT_TRUE(world.get<HealthComponent>(e)->health.alive());
    ASSERT_TRUE(world.has<Dark::Combat::StatusEffectComponent>(e));
    ASSERT_TRUE(world.has<Dark::Combat::PoiseComponent>(e));
    EXPECT_FALSE(world.get<Dark::Combat::PoiseComponent>(e)->hyperArmor);
    EXPECT_FALSE(world.has<Dark::JumpAttackComponent>(e));
}

TEST(AiAgentEntity, DamageThenDestroyFreesBrain)
{
    World          world;
    AssetManager   assets;
    AssetPinTable  pins;
    AiSystem       ai;

    TransformComponent xf{};
    Entity e = ai.spawnHunter(world, pins, assets, xf);
    ASSERT_TRUE(e.valid());
    EXPECT_TRUE(ai.applyHunterDamage(world, e, 200.0f));
    EXPECT_TRUE(world.get<HealthComponent>(e)->health.dead());
    world.destroyEntity(e);
    EXPECT_FALSE(world.alive(e));
    EXPECT_FALSE(world.has<BrainComponent>(e));
}

TEST(AiAgentEntity, SpawnStatusAcceptsKnockdownFromResolve)
{
    World         world;
    AssetManager  assets;
    AssetPinTable pins;
    AiSystem      ai;

    TransformComponent xf{};
    Entity e = ai.spawnHunter(world, pins, assets, xf);
    ASSERT_TRUE(e.valid());
    ASSERT_TRUE(world.has<Dark::Combat::StatusEffectComponent>(e));

    Dark::Combat::CombatSystem sys;
    Dark::Combat::DamageEvent  ev{};
    ev.target         = e;
    ev.amount         = 32.0f;
    ev.flags          = Dark::Combat::DamageFlags::CanBlock | Dark::Combat::DamageFlags::HardCc | Dark::Combat::DamageFlags::Knockdown;
    ev.statusDuration = 1.4f;
    EXPECT_EQ(Dark::Combat::resolveJumpAttackEvents(world, sys, &ev, 1), 1);
    const Dark::Combat::StatusEffectComponent* st = world.get<Dark::Combat::StatusEffectComponent>(e);
    ASSERT_NE(st, nullptr);
    EXPECT_TRUE(st->knockedDown());
    EXPECT_TRUE(st->hasHardCc());
    EXPECT_FALSE(world.get<HealthComponent>(e)->health.dead());
}
