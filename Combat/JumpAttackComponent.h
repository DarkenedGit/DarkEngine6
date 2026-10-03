#pragma once

#include "Combat/JumpAttack.h"
#include "ECS/Persist.h"

namespace Dark
{

    struct JumpAttackComponent
    {
        static constexpr const char* kTypeName    = "JumpAttack";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;
        Combat::JumpAttack           jump;

        JumpAttackComponent() :
            jump()
        {
        }
    };

} // namespace Dark
