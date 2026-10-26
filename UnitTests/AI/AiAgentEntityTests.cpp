#include <gtest/gtest.h>

#include "AI/AiSystem.h"
#include "Assets/AssetManager.h"
#include "Character/HealthComponent.h"
#include "Combat/ArmorPieces.h"
#include "Combat/CombatSystem.h"
#include "Combat/DamageEvent.h"
#include "Combat/DefenseComponent.h"
#include "Combat/HitVolumes.h"
#include "Combat/JumpAttackComponent.h"
#include "Combat/JumpAttackDef.h"
#include "Combat/JumpAttackResolve.h"
#include "Combat/PoiseComponent.h"
#include "Combat/StatusEffectComponent.h"
#include "Core/AssetPinTable.h"
#include "ECS/Components.h"
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
    ASSERT_TRUE(world.has<Dark::JumpAttackComponent>(e));
    const Dark::Combat::JumpAttackDef& def = world.get<Dark::JumpAttackComponent>(e)->jump.def();
    EXPECT_NEAR(def.telegraphSeconds, 0.40f, 1.0e-4f);
    EXPECT_NEAR(def.cooldown, 3.5f, 1.0e-4f);
    EXPECT_NEAR(def.groundOffset, 1.0f, 1.0e-4f);
    EXPECT_NEAR(def.connectDamage, 22.0f, 1.0e-4f);
    EXPECT_NEAR(def.poundDamage, 14.0f, 1.0e-4f);
    EXPECT_NEAR(def.leapVerticalSpeed, 10.0f, 1.0e-4f);
    EXPECT_NEAR(def.leapForwardSpeedMax, 12.0f, 1.0e-4f);
    EXPECT_NEAR(def.gravity, 24.0f, 1.0e-4f);
}

TEST(AiAgentEntity, AttachHunterOnExistingEntity)
{
    World         world;
    AssetManager  assets;
    AssetPinTable pins;
    AiSystem      ai;

    Entity e = world.createEntity();
    world.emplace<Dark::TagComponent>(e, "hunter");
    TransformComponent xf{};
    xf.position = Dark::Math::Vector3f{ 4.0f, 1.0f, -2.0f };
    world.emplace<TransformComponent>(e, xf);
    ASSERT_TRUE(ai.attachHunter(world, e, pins, assets));
    EXPECT_TRUE(world.has<AiAgentComponent>(e));
    EXPECT_TRUE(world.has<Dark::JumpAttackComponent>(e));
    EXPECT_TRUE(ai.attachHunter(world, e, pins, assets));
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

TEST(AiAgentEntity, ApplyHunterHitReactionRestoresDefaultsAfterKnockdown)
{
    World         world;
    AssetManager  assets;
    AssetPinTable pins;
    AiSystem      ai;

    Dark::HitReactionSettings hunterHit{};
    hunterHit.stunSeconds       = 0.45f;
    hunterHit.knockbackDistance = 2.2f;
    hunterHit.knockbackSeconds  = 0.18f;
    hunterHit.horizontalOnly    = true;
    ai.setHunterHitReactionSettings(hunterHit);

    TransformComponent xf{};
    Entity e = ai.spawnHunter(world, pins, assets, xf);
    ASSERT_TRUE(e.valid());

    Dark::Combat::CombatSystem sys;
    Dark::Combat::DamageEvent  ev{};
    ev.target           = e;
    ev.amount           = 32.0f;
    ev.flags            = Dark::Combat::DamageFlags::CanBlock | Dark::Combat::DamageFlags::HardCc | Dark::Combat::DamageFlags::Knockdown;
    ev.statusDuration   = 1.4f;
    ev.statusMagnitude  = 2.4f;
    ev.hitDir           = Dark::Math::Vector3f{ 0.0f, 0.0f, 1.0f };
    EXPECT_EQ(Dark::Combat::resolveJumpAttackEvents(world, sys, &ev, 1), 1);

    Dark::HitReactionComponent* hr = world.get<Dark::HitReactionComponent>(e);
    ASSERT_NE(hr, nullptr);
    EXPECT_NEAR(hr->hit.settings().stunSeconds, 1.4f, 1.0e-4f);
    EXPECT_NEAR(hr->hit.stunRemaining(), 1.4f, 1.0e-4f);
    ASSERT_TRUE(world.get<Dark::Combat::StatusEffectComponent>(e)->knockedDown());

    ai.applyHunterHitReaction(world, e, Dark::Math::Vector3f{ 1.0f, 0.0f, 0.0f });
    EXPECT_NEAR(hr->hit.settings().stunSeconds, hunterHit.stunSeconds, 1.0e-4f);
    EXPECT_NEAR(hr->hit.settings().knockbackDistance, hunterHit.knockbackDistance, 1.0e-4f);
    EXPECT_NEAR(hr->hit.settings().knockbackSeconds, hunterHit.knockbackSeconds, 1.0e-4f);
    EXPECT_NEAR(hr->hit.stunRemaining(), hunterHit.stunSeconds, 1.0e-4f);
    EXPECT_TRUE(world.get<Dark::Combat::StatusEffectComponent>(e)->knockedDown());
    EXPECT_TRUE(world.get<Dark::Combat::StatusEffectComponent>(e)->hasHardCc());
}

TEST(AiAgentEntity, StatusResetAfterKnockdownAllowsFullDurationAgain)
{
    World         world;
    AssetManager  assets;
    AssetPinTable pins;
    AiSystem      ai;

    TransformComponent xf{};
    Entity e = ai.spawnHunter(world, pins, assets, xf);
    ASSERT_TRUE(e.valid());
    Dark::Combat::StatusEffectComponent* st = world.get<Dark::Combat::StatusEffectComponent>(e);
    ASSERT_NE(st, nullptr);

    Dark::Combat::CombatSystem sys;
    Dark::Combat::DamageEvent  ev{};
    ev.target         = e;
    ev.amount         = 8.0f;
    ev.flags          = Dark::Combat::DamageFlags::CanBlock | Dark::Combat::DamageFlags::HardCc | Dark::Combat::DamageFlags::Knockdown;
    ev.statusDuration = 1.4f;
    EXPECT_EQ(Dark::Combat::resolveJumpAttackEvents(world, sys, &ev, 1), 1);
    EXPECT_TRUE(st->knockedDown());
    st->tick(0.25f);
    EXPECT_TRUE(st->knockedDown());
    EXPECT_NEAR(st->slots[0].remaining, 1.15f, 1.0e-3f);

    st->reset();
    EXPECT_FALSE(st->knockedDown());
    EXPECT_FALSE(st->hasHardCc());
    EXPECT_EQ(st->count, 0);
    EXPECT_NEAR(st->now, 0.0f, 1.0e-6f);
    EXPECT_EQ(st->dr[static_cast<int>(Dark::Combat::CcCategory::Knockdown)].applications, 0);

    EXPECT_EQ(Dark::Combat::resolveJumpAttackEvents(world, sys, &ev, 1), 1);
    EXPECT_TRUE(st->knockedDown());
    ASSERT_GT(st->count, 0);
    EXPECT_NEAR(st->slots[0].remaining, 1.4f, 1.0e-3f);
}

TEST(AiAgentEntity, JumpConnectSpendsHeadArmorAndDisablesOnce)
{
    World         world;
    AssetManager  assets;
    AssetPinTable pins;
    AiSystem      ai;

    TransformComponent xf{};
    Entity hunter = ai.spawnHunter(world, pins, assets, xf);
    ASSERT_TRUE(hunter.valid());
    world.emplace<Dark::Combat::ArmorPiecesComponent>(hunter) = Dark::Combat::makeHunterArmor();

    Entity attacker = world.createEntity();
    world.emplace<TransformComponent>(attacker, Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f });

    Dark::Combat::CombatSystem sys;
    Dark::Combat::DamageEvent  ev{};
    ev.source   = attacker;
    ev.target   = hunter;
    ev.amount   = 10.0f;
    ev.part     = Dark::Combat::HitPart::Head;
    ev.hitPoint = Dark::Math::Vector3f{ 0.0f, 1.0f, 2.0f };
    ev.hitDir   = Dark::Math::Vector3f{ 1.0f, 0.0f, 0.0f };
    ev.flags    = Dark::Combat::DamageFlags::CanBlock;
    EXPECT_EQ(Dark::Combat::resolveJumpAttackEvents(world, sys, &ev, 1), 1);

    const Dark::Combat::ArmorPiecesComponent* armor = world.get<Dark::Combat::ArmorPiecesComponent>(hunter);
    ASSERT_NE(armor, nullptr);
    EXPECT_NEAR(armor->pieces[0].hp, 32.0f, 1.0e-3f);
    EXPECT_TRUE(armor->pieces[0].attached);
    EXPECT_NEAR(armor->pieces[1].hp, 60.0f, 1.0e-3f);
    EXPECT_NEAR(world.get<HealthComponent>(hunter)->health.hp(), 46.0f, 1.0e-3f);

    const Dark::Combat::HitVolumesComponent* volumes = world.get<Dark::Combat::HitVolumesComponent>(hunter);
    ASSERT_NE(volumes, nullptr);
    const int head = Dark::Combat::hitPartIndex(Dark::Combat::HitPart::Head);
    EXPECT_FLOAT_EQ(volumes->disableTimer[head], 3.0f);
    EXPECT_FLOAT_EQ(volumes->impactWeight[head], 1.0f);
    for (int i = 0; i < Dark::Combat::HitVolumesComponent::kSlotCount; ++i)
    {
        if (i == head)
            continue;
        EXPECT_FLOAT_EQ(volumes->disableTimer[i], 0.0f);
        EXPECT_FLOAT_EQ(volumes->impactWeight[i], 0.0f);
    }

    Dark::HitReactionComponent* hr = world.get<Dark::HitReactionComponent>(hunter);
    ASSERT_NE(hr, nullptr);
    EXPECT_NEAR(hr->hit.knockDirection().x, 0.0f, 1.0e-4f);
    EXPECT_NEAR(hr->hit.knockDirection().y, 0.0f, 1.0e-4f);
    EXPECT_NEAR(hr->hit.knockDirection().z, 1.0f, 1.0e-4f);
}

TEST(AiAgentEntity, BlockedResolveDoesNotSpendArmorOrRefreshTimer)
{
    World         world;
    AssetManager  assets;
    AssetPinTable pins;
    AiSystem      ai;

    TransformComponent xf{};
    Entity hunter = ai.spawnHunter(world, pins, assets, xf);
    ASSERT_TRUE(hunter.valid());
    world.emplace<Dark::Combat::ArmorPiecesComponent>(hunter) = Dark::Combat::makeHunterArmor();
    Dark::Combat::HitVolumesComponent* volumes = world.get<Dark::Combat::HitVolumesComponent>(hunter);
    ASSERT_NE(volumes, nullptr);
    const int head = Dark::Combat::hitPartIndex(Dark::Combat::HitPart::Head);
    volumes->disableTimer[head] = 1.0f;
    volumes->impactWeight[head] = 0.25f;

    Dark::Combat::DefenseComponent& defense = world.emplace<Dark::Combat::DefenseComponent>(hunter);
    defense.blocking     = true;
    defense.facingYawRad = 0.0f;
    defense.blockArcDeg  = 140.0f;
    defense.stamina      = 100.0f;

    Dark::Combat::CombatSystem sys;
    Dark::Combat::DamageEvent  ev{};
    ev.target   = hunter;
    ev.amount   = 10.0f;
    ev.part     = Dark::Combat::HitPart::Head;
    ev.hitDir   = Dark::Math::Vector3f{ 0.0f, 0.0f, -1.0f };
    ev.hitPoint = Dark::Math::Vector3f{ 0.0f, 1.6f, 0.2f };
    ev.flags    = Dark::Combat::DamageFlags::CanBlock;
    Dark::Combat::ResolveResult results[1]{};
    EXPECT_EQ(Dark::Combat::resolveJumpAttackEvents(world, sys, &ev, 1, results), 1);
    EXPECT_TRUE(results[0].blocked);
    EXPECT_FALSE(Dark::Combat::partHitLanded(results[0]));

    const Dark::Combat::ArmorPiecesComponent* armor = world.get<Dark::Combat::ArmorPiecesComponent>(hunter);
    ASSERT_NE(armor, nullptr);
    EXPECT_FLOAT_EQ(armor->pieces[0].hp, 40.0f);
    EXPECT_FLOAT_EQ(armor->pieces[1].hp, 60.0f);
    EXPECT_FLOAT_EQ(volumes->disableTimer[head], 1.0f);
    EXPECT_FLOAT_EQ(volumes->impactWeight[head], 0.25f);
}

TEST(AiAgentEntity, KillingResolveDoesNotRearmPartTimer)
{
    World         world;
    AssetManager  assets;
    AssetPinTable pins;
    AiSystem      ai;

    TransformComponent xf{};
    Entity hunter = ai.spawnHunter(world, pins, assets, xf);
    ASSERT_TRUE(hunter.valid());
    world.emplace<Dark::Combat::ArmorPiecesComponent>(hunter) = Dark::Combat::makeHunterArmor();
    Dark::Combat::HitVolumesComponent* volumes = world.get<Dark::Combat::HitVolumesComponent>(hunter);
    ASSERT_NE(volumes, nullptr);
    const int head = Dark::Combat::hitPartIndex(Dark::Combat::HitPart::Head);
    volumes->disableTimer[head] = 1.0f;
    volumes->impactWeight[head] = 0.25f;

    Dark::Combat::CombatSystem sys;
    Dark::Combat::DamageEvent  ev{};
    ev.target = hunter;
    ev.amount = 300.0f;
    ev.part   = Dark::Combat::HitPart::Head;
    ev.hitDir = Dark::Math::Vector3f{ 0.0f, 0.0f, 1.0f };
    ev.flags  = Dark::Combat::DamageFlags::CanBlock;
    EXPECT_EQ(Dark::Combat::resolveJumpAttackEvents(world, sys, &ev, 1), 1);
    EXPECT_TRUE(world.get<HealthComponent>(hunter)->health.dead());

    const Dark::Combat::ArmorPiecesComponent* armor = world.get<Dark::Combat::ArmorPiecesComponent>(hunter);
    ASSERT_NE(armor, nullptr);
    EXPECT_FLOAT_EQ(armor->pieces[0].hp, 0.0f);
    EXPECT_FALSE(armor->pieces[0].attached);
    EXPECT_FLOAT_EQ(armor->pieces[1].hp, 60.0f);
    EXPECT_FLOAT_EQ(volumes->disableTimer[head], 1.0f);
    EXPECT_FLOAT_EQ(volumes->impactWeight[head], 0.25f);
}
