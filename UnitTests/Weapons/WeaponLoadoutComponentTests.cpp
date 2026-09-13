#include <gtest/gtest.h>

#include "ECS/World.h"
#include "Weapons/WeaponLoadoutComponent.h"

#include <memory>

using Dark::Entity;
using Dark::WeaponKind;
using Dark::WeaponLoadout;
using Dark::WeaponLoadoutComponent;
using Dark::World;

TEST(WeaponLoadoutComponent, EmplaceSelectAndDestroy)
{
    World world;
    Entity e = world.createEntity();
    auto& wlc = world.emplace<WeaponLoadoutComponent>(e);
    wlc.loadout = std::make_unique<WeaponLoadout>();
    ASSERT_NE(wlc.loadout, nullptr);
    EXPECT_EQ(wlc.loadout->activeKind(), WeaponKind::Melee);
    EXPECT_TRUE(wlc.loadout->selectProjectile());
    wlc.slot = wlc.loadout->slot();
    EXPECT_EQ(wlc.slot, 1);
    world.destroyEntity(e);
    EXPECT_FALSE(world.has<WeaponLoadoutComponent>(e));
}
