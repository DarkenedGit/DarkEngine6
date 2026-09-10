#pragma once
#include <cstdint>

namespace Dark
{
    using EntityID                 = uint32_t;
    constexpr EntityID NULL_ENTITY = 0;

    // Packed handle: low bits = slot index, high bits = generation.
    // Index 0 is reserved for NULL_ENTITY. Generations invalidate recycled slots.
    constexpr uint32_t kEntityIndexBits      = 20;
    constexpr uint32_t kEntityIndexMask      = (1u << kEntityIndexBits) - 1u;
    constexpr uint32_t kEntityGenerationMask = (1u << (32u - kEntityIndexBits)) - 1u;

    inline EntityID entityIndex(EntityID id)
    {
        return id & kEntityIndexMask;
    }

    inline uint32_t entityGeneration(EntityID id)
    {
        return id >> kEntityIndexBits;
    }

    inline EntityID makeEntityID(EntityID index, uint32_t generation)
    {
        return (index & kEntityIndexMask) | ((generation & kEntityGenerationMask) << kEntityIndexBits);
    }

    // Thin handle — all data lives in World.
    class Entity
    {
    public:
        Entity() = default;
        explicit Entity(EntityID id) :
            m_id(id)
        {
        }

        EntityID id() const
        {
            return m_id;
        }
        bool valid() const
        {
            return m_id != NULL_ENTITY;
        }

        bool operator==(const Entity& o) const = default;

    private:
        EntityID m_id = NULL_ENTITY;
    };

} // namespace Dark
