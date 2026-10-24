#include <gtest/gtest.h>

#include "Core/AssetPinTable.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/Item.h"
#include "Gameplay/ThrowableBelt.h"
#include "Physics/PhysicsWorld.h"
#include "Weapons/WeaponLoadout.h"

using namespace Dark;

namespace
{

    const ItemDef kRockDef{ "rock", "Rock", ItemType::Resource, EquipSlot::None, 1, 0.0f, 0.0f };
    const ItemDef kBottleDef{ "bottle", "Bottle", ItemType::Resource, EquipSlot::None, 1, 0.0f, 0.0f };

    Entity makePickup(World& world, const ItemDef& def, int count)
    {
        Entity e = world.createEntity();
        world.emplace<TransformComponent>(e);
        world.emplace<ItemPickupComponent>(e, ItemPickupComponent{ ItemStack{ &def, count } });
        return e;
    }

    int bagStacks(const InventoryComponent& inv)
    {
        return itemCount(inv);
    }

    bool fillBag(InventoryComponent& inv)
    {
        const ItemDef* sword = findItemDef("sword");
        if (!sword)
            return false;
        for (int i = 0; i < InventoryComponent::kBagSize; ++i)
        {
            if (addItem(inv, *sword, 1) != 1)
                return false;
        }
        return bagStacks(inv) == InventoryComponent::kBagSize && addItem(inv, *sword, 1) == 0;
    }

} // namespace

TEST(ThrowableBelt, PickupRockLeavesSwordSelectedAndBagUnchanged)
{
    World            world;
    AssetPinTable    pins;
    Physics::PhysicsWorld physics;
    ThrowableBelt    belt;
    WeaponLoadout    weapons;
    InventoryComponent bag;
    const ItemDef* sword = findItemDef("sword");
    ASSERT_NE(sword, nullptr);
    ASSERT_EQ(addItem(bag, *sword, 1), 1);
    ASSERT_EQ(weapons.slot(), 0);
    ASSERT_TRUE(belt.hand().empty());

    const int stacks = bagStacks(bag);
    const Entity rock = makePickup(world, kRockDef, 1);
    const CarryPickupResult took = takeCarryPickup(world, pins, physics, rock, belt, &bag);

    EXPECT_TRUE(took.throwable);
    EXPECT_EQ(took.bagAdded, 0);
    EXPECT_EQ(belt.count("rock"), 1);
    EXPECT_EQ(belt.count("bottle"), 0);
    EXPECT_TRUE(belt.hand().empty());
    EXPECT_EQ(weapons.slot(), 0);
    EXPECT_EQ(bagStacks(bag), stacks);
    EXPECT_FALSE(world.alive(rock));
}

TEST(ThrowableBelt, PickupOfTypeInHandAddsOneAndStays)
{
    World         world;
    AssetPinTable pins;
    Physics::PhysicsWorld physics;
    ThrowableBelt belt;
    WeaponLoadout weapons;
    InventoryComponent bag;
    ASSERT_EQ(weapons.slot(), 0);
    ASSERT_EQ(belt.add("bottle"), 1);
    ASSERT_TRUE(belt.setHand("bottle"));

    const int stacks = bagStacks(bag);
    const Entity bottle = makePickup(world, kBottleDef, 1);
    const CarryPickupResult took = takeCarryPickup(world, pins, physics, bottle, belt, &bag);

    EXPECT_TRUE(took.throwable);
    EXPECT_EQ(took.bagAdded, 0);
    EXPECT_EQ(belt.count("bottle"), 2);
    EXPECT_EQ(belt.count("rock"), 0);
    EXPECT_EQ(belt.hand(), "bottle");
    EXPECT_EQ(weapons.slot(), 0);
    EXPECT_EQ(bagStacks(bag), stacks);
    EXPECT_FALSE(world.alive(bottle));
}

TEST(ThrowableBelt, FullBagStillAcceptsThrowable)
{
    World         world;
    AssetPinTable pins;
    Physics::PhysicsWorld physics;
    ThrowableBelt belt;
    WeaponLoadout weapons;
    InventoryComponent bag;
    ASSERT_TRUE(fillBag(bag));
    ASSERT_EQ(weapons.slot(), 0);

    const Entity rock = makePickup(world, kRockDef, 1);
    const CarryPickupResult took = takeCarryPickup(world, pins, physics, rock, belt, &bag);

    EXPECT_TRUE(took.throwable);
    EXPECT_EQ(took.bagAdded, 0);
    EXPECT_EQ(belt.count("rock"), 1);
    EXPECT_EQ(bagStacks(bag), InventoryComponent::kBagSize);
    EXPECT_EQ(weapons.slot(), 0);
    EXPECT_FALSE(world.alive(rock));
}

TEST(ThrowableBelt, UnknownItemUsesBagPath)
{
    World         world;
    AssetPinTable pins;
    Physics::PhysicsWorld physics;
    ThrowableBelt belt;
    WeaponLoadout weapons;
    InventoryComponent bag;
    const ItemDef* medkit = findItemDef("medkit");
    ASSERT_NE(medkit, nullptr);
    ASSERT_EQ(weapons.slot(), 0);

    const Entity item = makePickup(world, *medkit, 1);
    const CarryPickupResult took = takeCarryPickup(world, pins, physics, item, belt, &bag);

    EXPECT_FALSE(took.throwable);
    EXPECT_EQ(took.bagAdded, 1);
    EXPECT_EQ(bagStacks(bag), 1);
    EXPECT_TRUE(hasItem(bag, "medkit"));
    EXPECT_EQ(belt.count("rock"), 0);
    EXPECT_EQ(belt.count("medkit"), 0);
    EXPECT_TRUE(belt.hand().empty());
    EXPECT_EQ(weapons.slot(), 0);
    EXPECT_FALSE(world.alive(item));
}

TEST(ThrowableBelt, EditorPickupAddsBeltAndCreatesNoBagStack)
{
    World         world;
    AssetPinTable pins;
    Physics::PhysicsWorld physics;
    ThrowableBelt belt;
    WeaponLoadout weapons;
    const Entity player = world.createEntity();
    ASSERT_FALSE(world.has<InventoryComponent>(player));
    ASSERT_EQ(weapons.slot(), 0);

    const Entity rock = makePickup(world, kRockDef, 1);
    const CarryPickupResult took = takeCarryPickup(world, pins, physics, rock, belt, nullptr);

    EXPECT_TRUE(took.throwable);
    EXPECT_EQ(took.bagAdded, 0);
    EXPECT_EQ(belt.count("rock"), 1);
    EXPECT_EQ(belt.count("bottle"), 0);
    EXPECT_TRUE(belt.hand().empty());
    EXPECT_EQ(weapons.slot(), 0);
    EXPECT_FALSE(world.alive(rock));
    EXPECT_FALSE(world.has<InventoryComponent>(player));

    int stacks = 0;
    world.each<InventoryComponent>([&](Entity, InventoryComponent& inv) { stacks += bagStacks(inv); });
    EXPECT_EQ(stacks, 0);
}

TEST(ThrowableBelt, CaptureKeepsThrowablePlacementAndSkipsOtherItems)
{
    World world;
    const Entity rock = makePickup(world, kRockDef, 1);
    TransformComponent* rockXf = world.get<TransformComponent>(rock);
    ASSERT_NE(rockXf, nullptr);
    rockXf->position = Math::Vector3f{ 4.0f, 1.0f, -2.0f };

    const ItemDef* medkit = findItemDef("medkit");
    ASSERT_NE(medkit, nullptr);
    const Entity kit = makePickup(world, *medkit, 1);
    TransformComponent* kitXf = world.get<TransformComponent>(kit);
    ASSERT_NE(kitXf, nullptr);
    kitXf->position = Math::Vector3f{ 9.0f, 0.0f, 9.0f };

    const std::vector<ThrowablePickupSpot> spots = captureThrowablePickups(world);
    ASSERT_EQ(spots.size(), 1u);
    EXPECT_EQ(spots[0].id, "rock");
    EXPECT_FLOAT_EQ(spots[0].position.x, 4.0f);
    EXPECT_FLOAT_EQ(spots[0].position.y, 1.0f);
    EXPECT_FLOAT_EQ(spots[0].position.z, -2.0f);
}
