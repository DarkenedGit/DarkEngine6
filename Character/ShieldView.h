#pragma once

#include "ECS/Entity.h"
#include "Math/Vector3f.h"

namespace Dark
{
    class AssetManager;
    class AssetPinTable;
    class Renderer;
    class World;
    struct TransformComponent;

    // Small buckler parented in the player's left hand. Face normal is mesh +Y.
    Entity spawnPlayerShield(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer);
    void   destroyPlayerShield(World& world, AssetPinTable& pins, Entity& shield);

    // chargeWindup pulls the buckler back. chargeStrike punches it forward on a charged parry.
    void placePlayerShield(TransformComponent& shield, const TransformComponent& player, float raiseAlpha, float chargeWindup = 0.0f, float chargeStrike = 0.0f);

    // Same spot the sandbox flashlight already uses, just left of the camera.
    void placePlayerFlashlight(TransformComponent& light,
                               const Math::Vector3f& camPos,
                               const Math::Vector3f& look,
                               const Math::Vector3f& right,
                               const Math::Vector3f& up);

    Entity spawnPlayerFlashlight(World& world);
    void   destroyPlayerFlashlight(World& world, Entity& light);
}
