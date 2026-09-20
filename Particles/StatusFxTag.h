#pragma once

#include "ECS/Entity.h"

#include <cstdint>

namespace Dark
{

    struct StatusFxTag
    {
        static constexpr const char* kTypeName = "StatusFx";

        Entity  follow{};
        uint8_t statusId = 0;
    };

} // namespace Dark
