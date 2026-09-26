#include <gtest/gtest.h>

#include "Character/Health.h"
#include "Combat/ArmorComponent.h"
#include "Combat/CombatSystem.h"
#include "Combat/DefenseComponent.h"
#include "Combat/Shield.h"
#include "ECS/World.h"
#include "Math/Vector3f.h"

using namespace Dark;
using namespace Dark::Combat;
using namespace Dark::Math;

namespace
{

    DamageEvent frontalHit(float amount)
    {
        DamageEvent ev{};
        ev.amount = amount;
        ev.type   = DamageType::Slash;
        ev.hitDir = Vector3f{ 0.0f, 0.0f, -1.0f };
        ev.flags  = DamageFlags::CanBlock;
        return ev;
    }

    ResolveResult swing(const Shield& shield, const DamageEvent& ev)
    {
        CombatSystem     sys;
        Health           hp{ HealthSettings{ 100.0f, 0.0f, 99.0f } };
        DefenseComponent defense{};
        ArmorComponent   armor{};
        armor.stats.blockMitigation = 0.0f;
        syncShieldDefense(defense, shield, 0.0f);
        const Vector3f facing = facingFromYaw(defense.facingYawRad);
        return sys.resolveDirect(ev, &hp, nullptr, &defense, &armor, nullptr, nullptr, &facing, 100.0f);
    }

} // namespace

TEST(Shield, DefaultsAreDownAndFullSpeed)
{
    Shield shield;
    EXPECT_FLOAT_EQ(shield.settings().raiseSeconds, 0.2f);
    EXPECT_FLOAT_EQ(shield.settings().moveSpeedScale, 0.55f);
    EXPECT_FLOAT_EQ(shield.settings().blockArcDeg, 120.0f);
    EXPECT_FALSE(shield.blocking());
    EXPECT_FLOAT_EQ(shield.speedScale(), 1.0f);
}

TEST(Shield, RaiseBlocksOnlyWhenFinished)
{
    Shield shield;
    shield.tick(0.1f, true);
    EXPECT_FALSE(shield.blocking());
    EXPECT_NEAR(shield.alpha(), 0.5f, 1.0e-4f);
    EXPECT_NEAR(shield.speedScale(), 0.775f, 1.0e-4f);

    const ResolveResult mid = swing(shield, frontalHit(40.0f));
    EXPECT_FALSE(mid.blocked);
    EXPECT_NEAR(mid.finalDamage, 40.0f, 1.0e-3f);

    shield.tick(0.1f, true);
    EXPECT_TRUE(shield.blocking());
    EXPECT_FLOAT_EQ(shield.alpha(), 1.0f);
    EXPECT_NEAR(shield.speedScale(), 0.55f, 1.0e-4f);

    const ResolveResult blocked = swing(shield, frontalHit(40.0f));
    EXPECT_TRUE(blocked.blocked);
    EXPECT_NEAR(blocked.finalDamage, 0.0f, 1.0e-3f);
}

TEST(Shield, LowerRestoresSpeed)
{
    Shield shield;
    shield.tick(0.2f, true);
    shield.tick(0.2f, false);
    EXPECT_EQ(shield.phase(), ShieldPhase::Down);
    EXPECT_FALSE(shield.blocking());
    EXPECT_FLOAT_EQ(shield.speedScale(), 1.0f);
}

TEST(Shield, ZeroRaiseSnapsUp)
{
    Shield shield;
    shield.settings().raiseSeconds = 0.0f;
    shield.tick(0.016f, true);
    EXPECT_TRUE(shield.blocking());
    shield.tick(0.016f, false);
    EXPECT_FALSE(shield.blocking());
}

TEST(Shield, ArcUsesFacing)
{
    Shield shield;
    shield.settings().raiseSeconds = 0.0f;
    shield.settings().blockArcDeg  = 120.0f;
    shield.tick(0.0f, true);

    DamageEvent side = frontalHit(30.0f);
    side.hitDir      = Vector3f{ -1.0f, 0.0f, 0.0f };
    const ResolveResult outside = swing(shield, side);
    EXPECT_FALSE(outside.blocked);
    EXPECT_NEAR(outside.finalDamage, 30.0f, 1.0e-3f);

    DamageEvent rear = frontalHit(30.0f);
    rear.hitDir      = Vector3f{ 0.0f, 0.0f, 1.0f };
    EXPECT_FALSE(swing(shield, rear).blocked);

    DamageEvent glancing = frontalHit(30.0f);
    glancing.hitDir      = Vector3f{ -0.5f, 0.0f, -0.8660254f };
    EXPECT_TRUE(swing(shield, glancing).blocked);
}

TEST(Shield, OffhandLightAndShieldExcludeEachOther)
{
    OffhandState hand;
    EXPECT_TRUE(hand.lightOn);

    hand.tick(0.2f, true, true);
    EXPECT_TRUE(hand.shield.blocking());
    EXPECT_FALSE(hand.lightOn);
    EXPECT_FALSE(hand.lightPending);

    hand.tick(0.1f, false, true);
    EXPECT_FALSE(hand.shield.blocking());
    EXPECT_FALSE(hand.lightOn);
    EXPECT_TRUE(hand.lightPending);

    hand.tick(0.1f, false, false);
    EXPECT_EQ(hand.shield.phase(), ShieldPhase::Down);
    EXPECT_TRUE(hand.lightOn);
    EXPECT_FALSE(hand.lightPending);

    hand.tick(0.0f, false, true);
    EXPECT_FALSE(hand.lightOn);

    hand.shield.settings().raiseSeconds = 0.0f;
    hand.tick(0.0f, true, true);
    EXPECT_TRUE(hand.shield.blocking());
    EXPECT_FALSE(hand.lightOn);
}

TEST(Shield, RaisedPoseSitsLeftAndFacesForward)
{
    const ShieldLocalPose down = shieldLocalPose(0.0f);
    const ShieldLocalPose up   = shieldLocalPose(1.0f);
    EXPECT_LT(down.position.x, -0.3f);
    EXPECT_LT(down.position.y, up.position.y);
    EXPECT_GT(up.position.z, down.position.z);

    const Vector3f face = up.rotation.Rotate(Vector3f::Y_AXIS);
    EXPECT_GT(face.z, 0.7f);
    EXPECT_GT(face.x, 0.2f);

    const Vector3f hip = down.rotation.Rotate(Vector3f::Y_AXIS);
    EXPECT_LT(hip.x, -0.8f);
}

TEST(Shield, EquipGivesFullBlock)
{
    World  world;
    Entity player = world.createEntity();
    equipPlayerShield(world, player);
    const ArmorComponent* armor = world.get<ArmorComponent>(player);
    ASSERT_NE(armor, nullptr);
    EXPECT_FLOAT_EQ(armor->stats.blockMitigation, 0.0f);
    EXPECT_NE(world.get<DefenseComponent>(player), nullptr);
}
