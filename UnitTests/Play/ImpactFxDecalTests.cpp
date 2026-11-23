#include <gtest/gtest.h>

#include "Play/ImpactFx.h"
#include "Render/DecalBasis.h"

// Weapon hits pick the impact mark through decalImpactOf (Render does not see WeaponKind).

using namespace Dark;

TEST(ImpactFxDecal, ProjectileHitsLeaveABulletMark)
{
    EXPECT_EQ(decalImpactOf(WeaponKind::Projectile), DecalImpact::Bullet);
}

TEST(ImpactFxDecal, MeleeAndThrownHitsLeaveASlash)
{
    EXPECT_EQ(decalImpactOf(WeaponKind::Melee), DecalImpact::Slash);
    EXPECT_EQ(decalImpactOf(WeaponKind::Throwable), DecalImpact::Slash);
}

TEST(ImpactFxDecal, EachWeaponSelectsItsImpactDefinition)
{
    DecalSpawnDesc desc;
    desc.kind   = DecalKind::Impact;
    desc.impact = decalImpactOf(WeaponKind::Projectile);
    EXPECT_EQ(decalSelectDef(desc), DecalDefId::ImpactBullet);
    desc.impact = decalImpactOf(WeaponKind::Melee);
    EXPECT_EQ(decalSelectDef(desc), DecalDefId::ImpactSlash);
    desc.impact = decalImpactOf(WeaponKind::Throwable);
    EXPECT_EQ(decalSelectDef(desc), DecalDefId::ImpactSlash);
}
