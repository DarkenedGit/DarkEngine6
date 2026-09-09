#include "ECS/World.h"

namespace Dark
{
    World::World()
    {
        // Slot 0 is reserved so packed NULL_ENTITY (0) never aliases a live entity.
        m_generations.push_back(0);
    }

    Entity World::createEntity()
    {
        EntityID index;
        if (!m_free.empty())
        {
            index = m_free.back();
            m_free.pop_back();
        }
        else
        {
            index = static_cast<EntityID>(m_generations.size());
            if (index > kEntityIndexMask)
            {
                // Hard cap — return null rather than overflowing the packed layout.
                return Entity{};
            }
            m_generations.push_back(0);
        }

        return Entity{ makeEntityID(index, m_generations[index]) };
    }

    void World::destroyEntity(Entity e)
    {
        if (!alive(e))
            return;

        const EntityID id    = e.id();
        const EntityID index = entityIndex(id);

        for (auto& [cid, pool] : m_pools)
        {
            (void)cid;
            pool->remove(id);
        }

        // Bump generation so stale handles fail alive() even after the slot is reused.
        uint32_t gen = m_generations[index];
        gen          = (gen + 1u) & kEntityGenerationMask;
        m_generations[index] = gen;

        m_free.push_back(index);
    }

    bool World::alive(Entity e) const
    {
        if (!e.valid())
            return false;

        const EntityID index = entityIndex(e.id());
        if (index == 0 || index >= m_generations.size())
            return false;

        return m_generations[index] == entityGeneration(e.id());
    }

} // namespace Dark
