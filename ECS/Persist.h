#pragma once

#include <cstdint>

namespace Dark
{
    // Type-erased save opt-in. The pool stores this pointer. JSON lives in Save/.
    // key must match the component's kTypeName and must not be empty or "Unnamed".
    struct PersistFns
    {
        const char* key      = nullptr;
        uint16_t    version  = 1;
        int         order    = 100;
        bool (*include)(const void* component)                          = nullptr;
        void (*capture)(const void* component, void* writer)            = nullptr;
        void (*apply)(void* component, void* reader, uint16_t version)  = nullptr;
        void (*bindRefs)(void* component, void* reader)                 = nullptr;
    };

} // namespace Dark
