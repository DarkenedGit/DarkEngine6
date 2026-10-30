#include <gtest/gtest.h>

#include "Assets/AssetManager.h"
#include "Audio/AudioSystem.h"
#include "Character/HealthComponent.h"
#include "Combat/CombatSystem.h"
#include "ECS/World.h"
#include "Gameplay/ThrowableBelt.h"
#include "Play/PlayerCombat.h"
#include "Play/PlaySession.h"

using namespace Dark;

TEST(PlayerCombat, OwnsMeleeWithoutAWorldAndNotAThrowable)
{
    PlayHost host;
    EXPECT_TRUE(playerOwnsWeapon(host, WeaponKind::Melee));
    EXPECT_TRUE(playerOwnsWeapon(host, WeaponKind::Projectile));
    EXPECT_FALSE(playerOwnsWeapon(host, WeaponKind::Throwable));
}

TEST(PlayerCombat, EmptyBeltIsUnownedAndAMissingInventoryOwnsTheSword)
{
    World world;
    const Entity player = world.createEntity();
    world.emplace<ThrowableBeltComponent>(player);
    PlayHost host;
    host.world  = &world;
    host.player = player;
    EXPECT_FALSE(playerOwnsWeapon(host, WeaponKind::Throwable));
    EXPECT_TRUE(playerOwnsWeapon(host, WeaponKind::Melee));
    EXPECT_TRUE(playerOwnsWeapon(host, WeaponKind::Projectile));
}

TEST(PlayerCombat, HitWithoutAudioLeavesHealth)
{
    World world;
    const Entity victim = world.createEntity();
    world.emplace<HealthComponent>(victim);
    PlayHost host;
    host.world = &world;
    PlayRuntime runtime;
    WeaponHit   hit;
    hit.hitTarget    = true;
    hit.targetEntity = victim;
    hit.damage       = 25.0f;
    onPlayerWeaponHit(host, runtime, hit);
    EXPECT_FLOAT_EQ(world.get<HealthComponent>(victim)->health.hp(), 100.0f);
}

TEST(PlayerCombat, NonAgentHitDealsDamageWithoutASoundBank)
{
    World              world;
    AssetManager       assets;
    Audio::AudioSystem audio;
    const Entity       victim = world.createEntity();
    world.emplace<HealthComponent>(victim);
    PlayHost host;
    host.world  = &world;
    host.assets = &assets;
    host.audio  = &audio;
    PlayRuntime runtime;
    WeaponHit   hit;
    hit.hitTarget    = true;
    hit.targetEntity = victim;
    hit.damage       = 25.0f;
    hit.weapon       = WeaponKind::Projectile;
    onPlayerWeaponHit(host, runtime, hit);
    EXPECT_FLOAT_EQ(world.get<HealthComponent>(victim)->health.hp(), 75.0f);
}

TEST(PlayerCombat, WeaponQueryReachesNinetyMeters)
{
    World world;
    PlayHost host;
    host.world = &world;
    PlayRuntime       runtime;
    PlayerCombatHooks hooks;
    const WeaponWorldQuery query = makePlayerWeaponQuery(host, runtime, hooks);
    EXPECT_FLOAT_EQ(query.maxRange, 90.0f);
}

TEST(PlayerCombat, FireLoadoutNeedsACamera)
{
    World              world;
    AssetManager       assets;
    Audio::AudioSystem audio;
    PlayHost           host;
    host.world  = &world;
    host.assets = &assets;
    host.audio  = &audio;
    PlayRuntime runtime;
    EXPECT_FALSE(firePlayerLoadout(host, runtime, false, {}));
}

TEST(PlayerCombat, ResolveJumpHitsWithNoEventsReturns)
{
    PlayHost             host;
    PlayRuntime          runtime;
    Combat::CombatSystem combat;
    resolvePlayerJumpHits(host, runtime, combat, nullptr, 0);
}
