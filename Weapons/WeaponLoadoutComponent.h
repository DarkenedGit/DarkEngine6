#pragma once

#include "ECS/Entity.h"
#include "ECS/Persist.h"
#include "Weapons/ThrowableFlight.h"
#include "Weapons/WeaponLoadout.h"

#include <memory>
#include <string>
#include <vector>

namespace Dark
{

    class World;

    struct ThrowableCountRecord
    {
        std::string id;
        int         count = 0;
    };

    enum class ThrowableLoadoutSync : uint8_t
    {
        OntoRecord,
        FromRecord,
    };

    struct WeaponLoadoutComponent
    {
        static constexpr const char* kTypeName    = "WeaponLoadout";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;

        int                                  slot = 0; // 0 melee, 1 projectile, 2 throwable
        std::unique_ptr<WeaponLoadout>       loadout;
        std::vector<ThrowableCountRecord>    throwCounts;
        std::string                          throwHand;
        std::vector<ThrowableFlightSnapshot> throwFlights;
    };

    // captureLoadout only sees the component. Both hosts copy the belt and live throws onto this record before capture, and push them back after load.
    void syncThrowableLoadout(World& world, Entity actor, ThrowableFlights& flights, ThrowableLoadoutSync dir);

} // namespace Dark
