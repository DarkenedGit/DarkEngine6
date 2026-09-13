#pragma once

#include "Character/PlayerMotor.h"

namespace Dark
{

    struct PlayerMotorComponent
    {
        static constexpr const char* kTypeName = "PlayerMotor";
        PlayerMotor                  motor;

        PlayerMotorComponent() :
            motor()
        {
        }
    };

} // namespace Dark
