#pragma once

#include "Weapons/WeaponLoadout.h"

#include <memory>

namespace Dark
{

    struct WeaponLoadoutComponent
    {
        static constexpr const char* kTypeName = "WeaponLoadout";

        int                            slot = 0; // 0 melee, 1 projectile
        std::unique_ptr<WeaponLoadout> loadout;
    };

} // namespace Dark
