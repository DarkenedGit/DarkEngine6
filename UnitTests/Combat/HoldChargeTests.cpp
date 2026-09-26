#include <gtest/gtest.h>

#include <vector>

#include "Character/Health.h"
#include "Character/HealthComponent.h"
#include "Combat/CombatSystem.h"
#include "Combat/DefenseComponent.h"
#include "Combat/HoldCharge.h"
#include "Combat/StatusEffectComponent.h"
#include "Combat/StatusId.h"
#include "ECS/World.h"
#include "Math/Vector3f.h"
#include "Weapons/MeleeWeapon.h"

using namespace Dark;
using namespace Dark::Combat;
using namespace Dark::Math;

TEST(HoldCharge, ReleaseBeforeWindowIsQuick)
{
    HoldCharge charge;
    charge.settings().windowSeconds = 0.40f;
    HoldChargeEvent ev{};
    charge.tick(0.20f, true, ev);
    EXPECT_FALSE(ev.releasedQuick);
    EXPECT_FALSE(ev.becameCharged);
    EXPECT_EQ(charge.phase(), HoldChargePhase::Holding);

    charge.tick(0.0f, false, ev);
    EXPECT_TRUE(ev.releasedQuick);
    EXPECT_FALSE(ev.releasedCharged);
    EXPECT_EQ(charge.phase(), HoldChargePhase::Idle);
}

TEST(HoldCharge, HoldPastWindowChargesAndReleasePlaysIt)
{
    HoldCharge charge;
    charge.settings().windowSeconds = 0.40f;
    HoldChargeEvent ev{};
    charge.tick(0.40f, true, ev);
    EXPECT_TRUE(ev.becameCharged);
    EXPECT_TRUE(charge.isCharged());
    EXPECT_FALSE(ev.releasedCharged);

    charge.tick(0.10f, true, ev);
    EXPECT_FALSE(ev.becameCharged);
    EXPECT_TRUE(charge.isCharged());

    charge.tick(0.0f, false, ev);
    EXPECT_TRUE(ev.releasedCharged);
    EXPECT_FALSE(ev.releasedQuick);
}

TEST(HoldCharge, ResetDoesNotCountAsRelease)
{
    HoldCharge attack;
    HoldCharge block;
    attack.settings().windowSeconds = 0.20f;
    block.settings().windowSeconds  = 0.20f;
    PlayerChargeSettings settings{};
    settings.attackWindowSeconds = 0.20f;
    settings.blockWindowSeconds  = 0.20f;

    PlayerChargeInput in{};
    in.dt              = 0.25f;
    in.attackDown      = true;
    in.canChargeAttack = true;
    in.holdShield      = true;
    in.canHoldShield   = true;
    PlayerChargeStep step{};
    stepPlayerCharge(attack, block, settings, in, step);
    EXPECT_TRUE(attack.isCharged());
    EXPECT_TRUE(block.isCharged());

    in.dt              = 0.016f;
    in.canChargeAttack = false;
    in.canHoldShield   = false;
    in.attackDown      = false;
    in.holdShield      = false;
    stepPlayerCharge(attack, block, settings, in, step);
    EXPECT_FALSE(step.fireQuick);
    EXPECT_FALSE(step.fireCharged);
    EXPECT_FALSE(step.openChargedParry);
    EXPECT_FALSE(attack.isCharged());
    EXPECT_FALSE(block.isCharged());
}

TEST(Combat_ParryPunish, ChargedSlamOutlastsParryStun)
{
    World world;
    Entity defender = world.createEntity();
    Entity attacker = world.createEntity();

    HealthSettings hs{};
    hs.maxHp = 100.0f;
    HealthComponent hp{};
    hp.health = Health{ hs };
    world.emplace<HealthComponent>(defender, std::move(hp));

    DefenseComponent defense{};
    defense.blockArcDeg              = 120.0f;
    defense.parryStunSeconds         = 0.90f;
    defense.chargedParryStunSeconds  = 1.80f;
    defense.beginParryWindow(0.20f);
    defense.chargedParry = false;
    world.emplace<DefenseComponent>(defender, defense);
    world.emplace<StatusEffectComponent>(attacker);
    world.emplace<HitReactionComponent>(attacker);

    DamageEvent ev{};
    ev.source = attacker;
    ev.target = defender;
    ev.amount = 22.0f;
    ev.hitDir = Vector3f{ 0.0f, 0.0f, -1.0f };
    ev.flags  = DamageFlags::CanParry | DamageFlags::CanBlock;

    CombatSystem sys;
    const ResolveResult quick = sys.resolve(world, ev);
    EXPECT_TRUE(quick.parried);
    EXPECT_FLOAT_EQ(quick.finalDamage, 0.0f);

    StatusEffectComponent* st = world.get<StatusEffectComponent>(attacker);
    ASSERT_NE(st, nullptr);
    EXPECT_TRUE(st->has(StatusId::Stun));
    EXPECT_FALSE(st->knockedDown());
    EXPECT_NEAR(st->remaining(StatusId::Stun), 0.90f, 0.05f);

    DefenseComponent* live = world.get<DefenseComponent>(defender);
    ASSERT_NE(live, nullptr);
    live->beginParryWindow(0.20f);
    live->chargedParry             = true;
    live->parryStunSeconds         = 0.90f;
    live->chargedParryStunSeconds  = 1.80f;

    const ResolveResult slammed = sys.resolve(world, ev);
    EXPECT_TRUE(slammed.parried);
    EXPECT_TRUE(st->knockedDown());
    EXPECT_GT(st->remaining(StatusId::Knockdown), st->remaining(StatusId::Stun));
    EXPECT_FALSE(live->chargedParry);
}

TEST(MeleeWeapon, ChargedScaleMultipliesDamage)
{
    struct Target
    {
        std::vector<Vector3f> centers;
        std::vector<WeaponHit> hits;
        static int count(void* user) { return static_cast<int>(static_cast<Target*>(user)->centers.size()); }
        static bool alive(void*, int) { return true; }
        static Vector3f center(void* user, int i) { return static_cast<Target*>(user)->centers[static_cast<size_t>(i)]; }
        static void onHit(void* user, const WeaponHit& hit) { static_cast<Target*>(user)->hits.push_back(hit); }
    } target;
    target.centers.push_back(Vector3f{ 0.0f, 1.0f, 2.0f });

    WeaponWorldQuery q{};
    q.targetUser   = &target;
    q.targetCount  = &Target::count;
    q.targetAlive  = &Target::alive;
    q.targetCenter = &Target::center;

    MeleeWeapon w;
    w.setHitListener(&Target::onHit, &target);
    WeaponFireRequest req{};
    req.direction   = Vector3f{ 0.0f, 0.0f, 1.0f };
    req.ownerPos    = Vector3f{ 0.0f, 0.5f, 0.0f };
    req.damageScale = 1.85f;
    EXPECT_TRUE(w.fire(req, q));
    ASSERT_EQ(target.hits.size(), 1u);
    EXPECT_NEAR(target.hits[0].damage, 16.0f * 1.85f, 1.0e-3f);
}
