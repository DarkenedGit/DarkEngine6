#pragma once

#include "ECS/Entity.h"

#include <cstdint>
#include <vector>

namespace Dark
{

    // One frame of entity ids. clear() bumps a stamp. The owner keeps the set
    // across frames, so a frame does not allocate a node per inserted id.
    // A recycled slot stores the full id, so the old generation does not match.
    class FrameEntitySet
    {
    public:
        void clear()
        {
            if (++m_stamp == 0)
            {
                for (Slot& slot : m_slots)
                    slot = {};
                m_stamp = 1;
            }
        }

        void insert(EntityID id)
        {
            if (id == NULL_ENTITY)
                return;
            const uint32_t index = entityIndex(id);
            if (index >= m_slots.size())
                m_slots.resize(static_cast<size_t>(index) + 1);
            m_slots[index].stamp = m_stamp;
            m_slots[index].id    = id;
        }

        bool contains(EntityID id) const
        {
            if (id == NULL_ENTITY)
                return false;
            const uint32_t index = entityIndex(id);
            if (index >= m_slots.size())
                return false;
            const Slot& slot = m_slots[index];
            return slot.stamp == m_stamp && slot.id == id;
        }

    private:
        struct Slot
        {
            uint32_t stamp = 0;
            EntityID id    = NULL_ENTITY;
        };

        std::vector<Slot> m_slots;
        uint32_t          m_stamp = 1;
    };

} // namespace Dark
