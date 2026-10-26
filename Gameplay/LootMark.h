#pragma once

#include "ECS/Persist.h"

namespace Dark
{

    // Present after a hunter's bag has been dropped. Removed when the hunter is alive again.
    struct LootDroppedComponent
    {
        static constexpr const char* kTypeName    = "LootDropped";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;
    };

} // namespace Dark
