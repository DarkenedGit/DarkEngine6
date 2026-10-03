#pragma once

#include "ECS/Persist.h"
#include "Weapons/WeaponLoadout.h"

#include <memory>

namespace Dark
{

    struct WeaponLoadoutComponent
    {
        static constexpr const char* kTypeName    = "WeaponLoadout";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;

        int                            slot = 0; // 0 melee, 1 projectile
        std::unique_ptr<WeaponLoadout> loadout;
    };

} // namespace Dark
