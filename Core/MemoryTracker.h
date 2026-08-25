#pragma once

#include "Debug/DebugTypes.h"

#include <cstdint>

namespace Dark
{

    class MemoryTracker
    {
    public:
        static void set(const char* name, uint64_t used, uint64_t capacity, uint32_t count);
        static void clear(const char* name);
        static void snapshot(DebugMemoryPool* out, uint32_t cap, uint32_t& count);
        static void reset();

    private:
        MemoryTracker() = default;
    };

} // namespace Dark
