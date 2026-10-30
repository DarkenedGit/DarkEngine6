#include <gtest/gtest.h>

#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/Inventory.h"
#include "Play/PlayerFlashlight.h"
#include "Play/PlaySession.h"

using namespace Dark;
using namespace Dark::Math;

TEST(PlayerFlashlight, StartsEquippedAndFollowsTheBody)
{
    World world;
    Entity player = world.createEntity();
    TransformComponent body;
    body.position = Vector3f{ 4.0f, 1.0f, 2.0f };
    world.emplace<TransformComponent>(player, body);

    ensurePlayerFlashlight(world, player);
    ensurePlayerFlashlight(world, player);
    const InventoryComponent* inv = world.get<InventoryComponent>(player);
    ASSERT_NE(inv, nullptr);
    EXPECT_TRUE(flashlightEquipped(inv));
    EXPECT_TRUE(inv->bag[0].empty());

    const Vector3f look{ 0.0f, 0.0f, 1.0f };
    const Vector3f right{ 1.0f, 0.0f, 0.0f };
    const Vector3f up{ 0.0f, 1.0f, 0.0f };
    syncPlayerFlashlight(world, player, Vector3f{ 0.0f, 0.0f, 0.0f }, look, right, up, true);

    const PlayerFlashlightComponent* rig = world.get<PlayerFlashlightComponent>(player);
    ASSERT_NE(rig, nullptr);
    ASSERT_TRUE(rig->light.valid());
    ASSERT_TRUE(world.alive(rig->light));
    const LocalLightComponent* light = world.get<LocalLightComponent>(rig->light);
    ASSERT_NE(light, nullptr);
    EXPECT_TRUE(light->enabled);
    EXPECT_EQ(light->type, LocalLightType::Spot);
    EXPECT_FLOAT_EQ(light->range, 22.0f);
    EXPECT_FLOAT_EQ(light->innerConeDeg, 10.0f);
    EXPECT_FLOAT_EQ(light->outerConeDeg, 22.0f);
    EXPECT_FLOAT_EQ(light->intensity, 1571.0f);
    const TransformComponent* xf = world.get<TransformComponent>(rig->light);
    ASSERT_NE(xf, nullptr);
    EXPECT_FLOAT_EQ(xf->position.x, 4.2f);
    EXPECT_FLOAT_EQ(xf->position.y, 1.35f);
    EXPECT_FLOAT_EQ(xf->position.z, 2.6f);

    const EntityID lightId = rig->light.id();
    EXPECT_TRUE(unequipItem(*world.get<InventoryComponent>(player), EquipSlot::Light));
    syncPlayerFlashlight(world, player, Vector3f{ 0.0f, 0.0f, 0.0f }, look, right, up, true);
    EXPECT_FALSE(world.alive(Entity{ lightId }));
    EXPECT_FALSE(world.get<PlayerFlashlightComponent>(player)->light.valid());
    EXPECT_FALSE(flashlightEquipped(world.get<InventoryComponent>(player)));
}

TEST(PlayerFlashlight, WeaponsStayInTheBag)
{
    InventoryComponent inv;
    const ItemDef* sword = findItemDef("sword");
    const ItemDef* light = findItemDef("flashlight");
    ASSERT_NE(sword, nullptr);
    ASSERT_NE(light, nullptr);
    EXPECT_EQ(addItem(inv, *sword, 1), 1);
    EXPECT_EQ(addItem(inv, *light, 1), 1);
    EXPECT_FALSE(equipItem(inv, 0));
    EXPECT_TRUE(equipItem(inv, 1));
    EXPECT_TRUE(flashlightEquipped(&inv));
    EXPECT_STREQ(itemTypeName(ItemType::Tool), "Tool");
    EXPECT_STREQ(equipSlotName(EquipSlot::Light), "Light");
}

TEST(PlayerKit, StarterBagOwnsSwordAndRifle)
{
    World world;
    Entity player = world.createEntity();
    stockPlayerKit(world, player);
    stockPlayerKit(world, player);
    const InventoryComponent* inv = world.get<InventoryComponent>(player);
    ASSERT_NE(inv, nullptr);
    EXPECT_EQ(countItem(*inv, "sword"), 1);
    EXPECT_EQ(countItem(*inv, "rifle"), 1);
    EXPECT_EQ(countItem(*inv, "medkit"), 2);
    EXPECT_TRUE(flashlightEquipped(inv));

    PlayHost host{};
    host.world  = &world;
    host.player = player;
    EXPECT_TRUE(playerOwnsWeapon(host, WeaponKind::Melee));
    EXPECT_TRUE(playerOwnsWeapon(host, WeaponKind::Projectile));

    Entity packed = world.createEntity();
    InventoryComponent partial;
    const ItemDef* sword = findItemDef("sword");
    const ItemDef* rifle = findItemDef("rifle");
    ASSERT_NE(sword, nullptr);
    ASSERT_NE(rifle, nullptr);
    ASSERT_EQ(addItem(partial, *sword, 1), 1);
    ASSERT_EQ(addItem(partial, *rifle, 1), 1);
    world.emplace<InventoryComponent>(packed, partial);
    stockPlayerKit(world, packed);
    const InventoryComponent* kept = world.get<InventoryComponent>(packed);
    ASSERT_NE(kept, nullptr);
    EXPECT_EQ(countItem(*kept, "sword"), 1);
    EXPECT_EQ(countItem(*kept, "rifle"), 1);
    EXPECT_EQ(countItem(*kept, "medkit"), 0);
    EXPECT_TRUE(flashlightEquipped(kept));

    Entity lightOnly = world.createEntity();
    world.emplace<InventoryComponent>(lightOnly);
    ensurePlayerFlashlight(world, lightOnly);
    PlayHost bare{};
    bare.world  = &world;
    bare.player = lightOnly;
    EXPECT_FALSE(playerOwnsWeapon(bare, WeaponKind::Melee));
    EXPECT_FALSE(playerOwnsWeapon(bare, WeaponKind::Projectile));
    stockPlayerKit(world, lightOnly);
    EXPECT_TRUE(playerOwnsWeapon(bare, WeaponKind::Melee));
    EXPECT_TRUE(playerOwnsWeapon(bare, WeaponKind::Projectile));
    EXPECT_EQ(countItem(*world.get<InventoryComponent>(lightOnly), "medkit"), 2);
}
