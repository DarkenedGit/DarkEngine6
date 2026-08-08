#include "ECS/World.h"

namespace Dark
{
    World::World() = default;

    Entity World::createEntity() 
    {
        EntityID id;
        if (!m_free.empty()) 
        {
            id = m_free.back();
            m_free.pop_back();
        }
        else 
        {
            id = m_nextID++;
        }
        return Entity{id};
    }

    void World::destroyEntity(Entity e) 
    {
        // Remove all components from every pool
        for (auto& [cid, pool] : m_pools)
            pool->remove(e.id());

        m_free.push_back(e.id());
    }

    bool World::alive(Entity e) const 
    {
        // An entity is alive if its id is in range and not in the free list
        if (!e.valid() || e.id() >= m_nextID) 
            return false;
        for (const auto& fid : m_free)
        {
            if (fid == e.id())
                return false;
        }
        return true;
    }

} // namespace Dark
