#pragma once
#include <cstdint>

namespace Dark
{
    using EntityID = uint32_t;
    constexpr EntityID NULL_ENTITY = 0;

    // Thin handle — all data lives in World.
    class Entity 
    {
    public:
        Entity() = default;
        explicit Entity(EntityID id) : m_id(id) {}

        EntityID id()    const { return m_id; }
        bool     valid() const { return m_id != NULL_ENTITY; }

        bool operator==(const Entity& o) const = default;

    private:
        EntityID m_id = NULL_ENTITY;
    };

} // namespace Dark
