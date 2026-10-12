#pragma once

#include "ECS/Entity.h"

namespace Dark
{
    class AssetManager;
    class AssetPinTable;
    class Renderer;
    class World;

    struct SwordComponent
    {
        static constexpr const char* kTypeName = "Sword";

        Entity blade{};
        Entity guard{};
        int    hand  = -1;
        bool   shown = true;
    };

    void equipSword(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer, Entity owner);
    void syncSwordVisuals(World& world);
    void removeSword(World& world, AssetPinTable& pins, Entity owner);

} // namespace Dark
