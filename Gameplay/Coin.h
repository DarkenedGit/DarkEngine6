#pragma once

#include "ECS/Entity.h"
#include "Math/Vector2f.h"

namespace Dark
{

    struct Coin
    {
        Math::Vector2f pos;
        bool           collected = false;
        Entity         entity{};
    };

} // namespace Dark
