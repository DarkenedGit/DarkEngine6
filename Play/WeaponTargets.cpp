#include "Play/WeaponTargets.h"

#include "Character/HealthComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Weapons/HittableComponent.h"

namespace Dark
{

    namespace
    {

        WeaponTargetList* listOf(void* user)
        {
            return static_cast<WeaponTargetList*>(user);
        }

        int targetCount(void* user)
        {
            return static_cast<int>(listOf(user)->targets().size());
        }

        bool targetAlive(void* user, int index)
        {
            const WeaponTargetList* list = listOf(user);
            return index >= 0 && index < static_cast<int>(list->targets().size()) && list->targets()[static_cast<size_t>(index)].alive;
        }

        Math::Vector3f targetCenter(void* user, int index)
        {
            const WeaponTargetList* list = listOf(user);
            if (index < 0 || index >= static_cast<int>(list->targets().size()))
                return Math::Vector3f{ 0.0f, 0.0f, 0.0f };
            return list->targets()[static_cast<size_t>(index)].center;
        }

        Math::Vector3f targetHalfExtentsAt(void* user, int index)
        {
            const WeaponTargetList* list = listOf(user);
            if (index < 0 || index >= static_cast<int>(list->targets().size()))
                return Math::Vector3f{ 1.0f, 1.0f, 1.0f };
            return list->targets()[static_cast<size_t>(index)].halfExtents;
        }

        Entity targetEntityAt(void* user, int index)
        {
            return listOf(user)->entityAt(index);
        }

    } // namespace

    void WeaponTargetList::gather(World& world, Entity ignore)
    {
        m_targets.clear();
        world.each<HittableComponent>([&](Entity e, HittableComponent& h) {
            if (ignore.valid() && e.id() == ignore.id())
                return;
            const TransformComponent* xf = world.get<TransformComponent>(e);
            if (!xf)
                return;
            const HealthComponent* hp = world.get<HealthComponent>(e);
            WeaponTarget           t{};
            t.entity      = e;
            t.center      = xf->position;
            t.halfExtents = h.halfExtents;
            t.alive       = hp && hp->health.alive();
            m_targets.push_back(t);
        });
    }

    void WeaponTargetList::bind(WeaponWorldQuery& query)
    {
        query.targetUser          = this;
        query.targetCount         = targetCount;
        query.targetAlive         = targetAlive;
        query.targetCenter        = targetCenter;
        query.targetHalfExtentsAt = targetHalfExtentsAt;
        query.targetEntityAt      = targetEntityAt;
    }

    Entity WeaponTargetList::entityAt(int index) const
    {
        if (index < 0 || index >= static_cast<int>(m_targets.size()))
            return {};
        return m_targets[static_cast<size_t>(index)].entity;
    }

} // namespace Dark
