#pragma once

#include "Combat/ArmorPieces.h"
#include "ECS/Entity.h"

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

    void equipHunterArmor(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer, Entity hunter);
    void syncArmorVisuals(World& world);
    void knockOffArmor(World& world, Physics::PhysicsWorld& physics, Entity owner, const Combat::ArmorBreak& broke);
    void removeArmor(World& world, AssetPinTable& pins, Physics::PhysicsWorld& physics, Entity owner);

} // namespace Dark
