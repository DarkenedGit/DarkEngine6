#pragma once

#include "ECS/Entity.h"
#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"

namespace Dark
{

    class World;

    struct HealthPackComponent
    {
        static constexpr const char* kTypeName = "HealthPack";
        static constexpr float       kPickupR  = 1.2f;
        static constexpr float       kHeal     = 50.0f;
        static constexpr float       kRespawn  = 16.0f;

        Math::Vector3f restPos{};
        float          spin      = 0.0f;
        float          bob       = 0.0f;
        float          respawnIn = 0.0f;
        bool           active    = true;
    };

    Math::Matrix4f healthPackWorldMatrix(const Math::Vector3f& pos, float spin, float bob);

    // Bob/spin Transform, hide Mesh primitive while inactive, heal player HealthComponent.
    // Returns number of packs collected this tick.
    int tickHealthPacks(World& world, Entity player, float dt);

} // namespace Dark
