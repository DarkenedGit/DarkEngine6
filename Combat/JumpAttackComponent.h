#pragma once

#include "Combat/JumpAttack.h"

namespace Dark
{

    struct JumpAttackComponent
    {
        static constexpr const char* kTypeName = "JumpAttack";
        Combat::JumpAttack           jump;

        JumpAttackComponent() :
            jump()
        {
        }
    };

} // namespace Dark
