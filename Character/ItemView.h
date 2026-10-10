#pragma once

#include "ECS/Entity.h"
#include "Gameplay/Inventory.h"
#include "Math/Vector3f.h"

namespace Dark
{
    class AssetManager;
    class AssetPinTable;
    class Renderer;
    class World;

    namespace Physics
    {
        class PhysicsWorld;
    }

    Entity spawnItemPickup(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer, Physics::PhysicsWorld& physics,
                           const ItemStack& item, const Math::Vector3f& position, const Math::Vector3f& velocity);
    Entity spawnLootBag(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer, const Math::Vector3f& position,
                        const char* label, const InventoryComponent& contents);
    Entity findPickupNear(World& world, const Math::Vector3f& position, float radius);
    void   destroyPickup(World& world, AssetPinTable& pins, Physics::PhysicsWorld& physics, Entity pickup);

} // namespace Dark
