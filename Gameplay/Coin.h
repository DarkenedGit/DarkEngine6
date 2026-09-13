#pragma once

#include "Math/Vector2f.h"

namespace Dark
{

    struct CoinComponent
    {
        static constexpr const char* kTypeName = "Coin";

        Math::Vector2f pos;
        bool           collected = false;
    };

} // namespace Dark
