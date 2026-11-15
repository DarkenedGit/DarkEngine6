#pragma once

#include "ECS/Entity.h"
#include "Math/AABox2f.h"

namespace Dark
{

    // 2D solid. Hosts keep physics handles (Box2D body ids) on their own types.
    struct Platform
    {
        Math::AABox2f box;
        float         z      = 2.0f;
        Entity        entity{};
    };

    // Placed by the platform archetype. The host builds the Box2D body from the transform.
    struct PlatformMarkerComponent
    {
        static constexpr const char* kTypeName = "PlatformMarker";
    };

    // Placed by the spawn archetype. The first one in a 2D scene is the player start.
    struct SpawnPointComponent
    {
        static constexpr const char* kTypeName = "SpawnPoint";
    };

} // namespace Dark
