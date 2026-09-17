#pragma once

#include "Combat/ArmorStats.h"

namespace Dark::Combat
{

    struct ArmorComponent
    {
        static constexpr const char* kTypeName = "Armor";
        ArmorStats                   stats{};
    };

} // namespace Dark::Combat
