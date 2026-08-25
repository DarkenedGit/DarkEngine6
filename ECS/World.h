#pragma once
#include "ECS/Entity.h"
#include "ECS/Component.h"
#include <unordered_map>
#include <vector>
#include <memory>
#include <cassert>
#include <functional>

namespace Dark
{

    // ─── Component storage pool ──────────────────────────────────────────────────

    struct IComponentPool
    {
        virtual ~IComponentPool()           = default;
        virtual void        remove(EntityID id) = 0;
        virtual const char* typeName() const    = 0;
        virtual uint32_t    count() const       = 0;
        virtual uint32_t    stride() const      = 0;
        virtual uint64_t    bytesUsed() const   = 0;
        virtual uint64_t    bytesCapacity() const = 0;
    };

    template <typename T> class ComponentPool : public IComponentPool
    {
    public:
        void insert(EntityID id, T comp)
        {
            m_sparse[id] = static_cast<uint32_t>(m_dense.size());
            m_dense.push_back(id);
            m_components.push_back(std::move(comp));
        }

        void remove(EntityID id) override
        {
            auto it = m_sparse.find(id);
            if (it == m_sparse.end())
                return;

            uint32_t idx  = it->second;
            uint32_t last = static_cast<uint32_t>(m_dense.size()) - 1;

            if (idx != last)
            {
                m_dense[idx]           = m_dense[last];
                m_components[idx]      = std::move(m_components[last]);
                m_sparse[m_dense[idx]] = idx;
            }
            m_dense.pop_back();
            m_components.pop_back();
            m_sparse.erase(it);
        }

        bool has(EntityID id) const
        {
            return m_sparse.contains(id);
        }

        T* get(EntityID id)
        {
            auto it = m_sparse.find(id);
            return it != m_sparse.end() ? &m_components[it->second] : nullptr;
        }

        // Iterate all components alongside their entity
        const std::vector<EntityID>& entities() const
        {
            return m_dense;
        }
        std::vector<T>& components()
        {
            return m_components;
        }

        const char* typeName() const override { return componentTypeName<T>(); }
        uint32_t    count() const override { return static_cast<uint32_t>(m_components.size()); }
        uint32_t    stride() const override { return static_cast<uint32_t>(sizeof(T)); }
        uint64_t    bytesUsed() const override { return static_cast<uint64_t>(m_components.size()) * sizeof(T); }
        uint64_t    bytesCapacity() const override { return static_cast<uint64_t>(m_components.capacity()) * sizeof(T); }

    private:
        std::unordered_map<EntityID, uint32_t> m_sparse;
        std::vector<EntityID>                  m_dense;
        std::vector<T>                         m_components;
    };

    // ─── World ───────────────────────────────────────────────────────────────────

    class World
    {
    public:
        World();

        Entity createEntity();
        void   destroyEntity(Entity e);
        bool   alive(Entity e) const;

        template <typename T, typename... Args> T& emplace(Entity e, Args&&... args)
        {
            auto& pool = getOrCreatePool<T>();
            pool.insert(e.id(), T{ std::forward<Args>(args)... });
            return *pool.get(e.id());
        }

        template <typename T> void remove(Entity e)
        {
            if (auto* pool = getPool<T>())
                pool->remove(e.id());
        }

        template <typename T> bool has(Entity e) const
        {
            const auto* pool = getPool<T>();
            return pool && pool->has(e.id());
        }

        template <typename T> T* get(Entity e)
        {
            auto* pool = getPool<T>();
            return pool ? pool->get(e.id()) : nullptr;
        }

        template <typename Fn> void forEachPool(Fn&& fn) const
        {
            for (const auto& [cid, pool] : m_pools)
            {
                (void)cid;
                if (pool)
                    fn(*pool);
            }
        }

        // Single-component view — iterate entities that have T
        template <typename T> void each(std::function<void(Entity, T&)> fn)
        {
            auto* pool = getPool<T>();
            if (!pool)
                return;
            auto& entities = pool->entities();
            auto& comps    = pool->components();
            for (size_t i = 0; i < entities.size(); ++i)
                fn(Entity{ entities[i] }, comps[i]);
        }

    private:
        template <typename T> ComponentPool<T>& getOrCreatePool()
        {
            const auto cid = componentID<T>();
            if (!m_pools.contains(cid))
                m_pools.emplace(cid, std::make_unique<ComponentPool<T>>());
            return static_cast<ComponentPool<T>&>(*m_pools.at(cid));
        }

        template <typename T> ComponentPool<T>* getPool()
        {
            const auto cid = componentID<T>();
            auto       it  = m_pools.find(cid);
            return it != m_pools.end() ? static_cast<ComponentPool<T>*>(it->second.get()) : nullptr;
        }

        template <typename T> const ComponentPool<T>* getPool() const
        {
            const auto cid = componentID<T>();
            auto       it  = m_pools.find(cid);
            return it != m_pools.end() ? static_cast<const ComponentPool<T>*>(it->second.get()) : nullptr;
        }

        std::unordered_map<ComponentID, std::unique_ptr<IComponentPool>> m_pools;
        std::vector<EntityID>                                            m_free;
        EntityID                                                         m_nextID = 1;
    };

} // namespace Dark
