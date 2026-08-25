#pragma once
#include <cstddef>

namespace Dark
{
    // Every component is a plain value type — no inheritance required.
    // Components are stored in typed pools inside World.

    using ComponentID = std::size_t;

    // Process-wide counter so each component *type* gets a unique pool id.
    // (A per-template static counter would restart at 0 for every T and alias all pools.)
    inline ComponentID allocateComponentID()
    {
        static ComponentID counter = 0;
        return counter++;
    }

    template <typename T> ComponentID componentID()
    {
        static const ComponentID id = allocateComponentID();
        return id;
    }

    template <typename T> const char* componentTypeName()
    {
        if constexpr (requires { T::kTypeName; })
            return T::kTypeName;
        else
            return "Unnamed";
    }

} // namespace Dark
