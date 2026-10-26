#include "Gameplay/QuestRuntime.h"

#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/QuestZone.h"

namespace Dark
{

    void QuestRuntime::tick(World& world, Entity player)
    {
        if (!m_tracker.active())
            return;
        const TransformComponent* pxf = player.valid() ? world.get<TransformComponent>(player) : nullptr;
        world.each<QuestZoneComponent>([&](Entity, QuestZoneComponent& zone) {
            const bool inside = pxf
                             && pxf->position.x > zone.box.Min.x && pxf->position.x < zone.box.Max.x
                             && pxf->position.z > zone.box.Min.z && pxf->position.z < zone.box.Max.z
                             && pxf->position.y > zone.box.Min.y - 1.5f && pxf->position.y < zone.box.Max.y + 2.5f;
            if (inside && !zone.inside)
                m_tracker.reach(zone.zone);
            zone.inside = inside;
        });
        if (m_tracker.complete())
            return;
        const InventoryComponent* inv = player.valid() ? world.get<InventoryComponent>(player) : nullptr;
        for (const ObjectiveDef& o : m_tracker.def().objectives)
        {
            if (o.type != ObjectiveType::Collect)
                continue;
            m_tracker.syncCollect(o.target, inv ? countItem(*inv, o.target) : 0);
        }
    }

} // namespace Dark
