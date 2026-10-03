#pragma once

#include "Character/PlayerMotor.h"
#include "ECS/Persist.h"

namespace Dark
{

    struct PlayerMotorComponent
    {
        static constexpr const char* kTypeName    = "PlayerMotor";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;

        PlayerMotor motor;

        PlayerMotorComponent() :
            motor()
        {
        }
    };

} // namespace Dark
