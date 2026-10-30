#pragma once

#include "Combat/ArmorStats.h"

namespace Dark::Combat
{

    struct ArmorComponent
    {
        static constexpr const char* kTypeName = "Armor";
        ArmorStats                   stats{};
    };

    // Flat armor rating absorbs the hit. A zero rating leaves that to the pieces.
    inline bool usesFlatArmor(const ArmorComponent* armor)
    {
        return armor != nullptr && armor->stats.armor > 0.0f;
    }

} // namespace Dark::Combat
