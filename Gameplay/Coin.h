#pragma once

#include "ECS/Persist.h"
#include "Math/Vector2f.h"

namespace Dark
{

    struct CoinComponent
    {
        static constexpr const char* kTypeName    = "Coin";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;

        Math::Vector2f pos;
        bool           collected = false;
    };

} // namespace Dark
