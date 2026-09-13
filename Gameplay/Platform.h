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

} // namespace Dark
