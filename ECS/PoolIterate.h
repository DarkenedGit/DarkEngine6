#pragma once

#include "Core/Log.h"

#include <cstdint>

namespace Dark
{

    // Test hook. Production leaves the trap on, so a mutation during each() debug-breaks.
    // A test turns the trap off, mutates, and reads the counter.
    inline uint32_t& poolIterateViolations()
    {
        static uint32_t count = 0;
        return count;
    }

    inline bool& poolIterateTrap()
    {
        static bool trap = true;
        return trap;
    }

    inline void notePoolIterateViolation()
    {
        ++poolIterateViolations();
        if (poolIterateTrap())
            DE_ASSERT(false, "component pool mutated during each");
    }

} // namespace Dark
