#pragma once

#include "ECS/Entity.h"

#include <cstddef>
#include <cstdint>

namespace Dark::Combat
{

    // Per-active-window anti-double-hit set. Clear on Active begin.
    class HitSet
    {
    public:
        static constexpr size_t kCapacity = 32;

        void clear()
        {
            m_count = 0;
        }

        bool contains(Entity e) const
        {
            if (!e.valid())
                return false;
            const EntityID id = e.id();
            for (size_t i = 0; i < m_count; ++i)
            {
                if (m_ids[i] == id)
                    return true;
            }
            return false;
        }

        // Returns true if newly recorded (first hit this window).
        bool tryAdd(Entity e)
        {
            if (!e.valid())
                return false;
            if (contains(e))
                return false;
            if (m_count >= kCapacity)
                return false;
            m_ids[m_count++] = e.id();
            return true;
        }

        size_t count() const { return m_count; }

    private:
        EntityID m_ids[kCapacity]{};
        size_t   m_count = 0;
    };

} // namespace Dark::Combat
