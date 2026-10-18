#pragma once

#include "ECS/Entity.h"
#include "Math/Vector3f.h"

namespace Dark
{

    class World;
    struct InventoryComponent;

    // Runtime spot for the equipped flashlight item. The entity is not saved.
    struct PlayerFlashlightComponent
    {
        static constexpr const char* kTypeName = "PlayerFlashlight";

        Entity   light{};
        float    baseRange = 0.0f;
        float    baseOuter = 0.0f;
        EntityID baseId    = 0;
    };

    // True when the Light slot holds the flashlight item.
    bool flashlightEquipped(const InventoryComponent* inv);

    // Gives the player one equipped flashlight when they do not already own one.
    void ensurePlayerFlashlight(World& world, Entity player);

    // Places and scales the spot while the item is equipped. Drops the spot when it is not.
    // fallbackPos is used when the player has no transform.
    void syncPlayerFlashlight(World& world, Entity player, const Math::Vector3f& fallbackPos, const Math::Vector3f& look, const Math::Vector3f& right, const Math::Vector3f& up, bool lightOn);

    // Destroys the spot entity. The inventory item stays.
    void clearPlayerFlashlight(World& world, Entity player);

} // namespace Dark
