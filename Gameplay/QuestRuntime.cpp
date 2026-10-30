#include "Gameplay/QuestRuntime.h"

#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/PowerComponents.h"
#include "Gameplay/QuestZone.h"

#include <filesystem>

namespace Dark
{
    namespace
    {
        std::filesystem::path findQuestFile(std::string_view id)
        {
            const std::filesystem::path rel{ questVirtualPath(id) };
            for (const std::filesystem::path& root : contentRootCandidates())
            {
                if (root.empty())
                    continue;
                const std::filesystem::path path = root / rel;
                std::error_code             ec;
                if (std::filesystem::is_regular_file(path, ec) && !ec)
                    return path;
            }
            return {};
        }

    } // namespace

    bool QuestRuntime::start(std::string_view id)
    {
        if (id.empty())
            return false;
        QuestDef    def;
        std::string error;
        const std::filesystem::path path = findQuestFile(id);
        if (path.empty() || !loadQuestFile(path, def, &error))
        {
            if (error.empty())
                error = "file not found";
            DE_LOG_ERROR("Quest: could not load '{}': {}", id, error);
            return false;
        }
        DE_LOG_INFO("Quest: started '{}' ({} objectives)", def.title, def.objectives.size());
        start(std::move(def));
        return true;
    }

    void QuestRuntime::start(QuestDef def)
    {
        m_rewardsGranted = false;
        m_tracker.start(std::move(def));
    }

    void QuestRuntime::clear()
    {
        m_rewardsGranted = false;
        m_tracker.clear();
    }

    bool QuestRuntime::objectiveTarget(World& world, Entity player, Math::Vector3f& out) const
    {
        const int focus = m_tracker.focus();
        if (focus < 0)
            return false;
        const ObjectiveDef&       o   = m_tracker.def().objectives[static_cast<size_t>(focus)];
        const TransformComponent* pxf = player.valid() ? world.get<TransformComponent>(player) : nullptr;
        if (!pxf)
            return false;
        if (o.type == ObjectiveType::Reach)
        {
            bool found = false;
            world.each<QuestZoneComponent>([&](Entity, QuestZoneComponent& zone) {
                if (found || zone.zone != o.target)
                    return;
                out   = zone.box.Center();
                found = true;
            });
            return found;
        }
        float best  = 1.0e12f;
        bool  found = false;
        auto  consider = [&](const Math::Vector3f& at) {
            const float d = (at - pxf->position).MagnitudeSqrd();
            if (d < best)
            {
                best  = d;
                out   = at;
                found = true;
            }
        };
        if (o.type == ObjectiveType::Collect)
        {
            world.each<ItemPickupComponent>([&](Entity e, ItemPickupComponent& pickup) {
                const TransformComponent* xf = world.get<TransformComponent>(e);
                if (xf && pickup.item.def && o.target == pickup.item.def->id)
                    consider(xf->position);
            });
        }
        else if (o.target == "power_off")
        {
            world.each<PowerBoxComponent>([&](Entity e, PowerBoxComponent& box) {
                if (!box.on)
                    return;
                const TransformComponent* xf = world.get<TransformComponent>(e);
                consider(xf ? xf->position : box.pos);
            });
        }
        return found;
    }

    int QuestRuntime::grantRewards(World& world, Entity player)
    {
        if (!m_tracker.complete() || m_rewardsGranted)
            return 0;
        InventoryComponent* inv = player.valid() ? world.get<InventoryComponent>(player) : nullptr;
        if (!inv)
            return 0;
        int granted = 0;
        for (const QuestReward& reward : m_tracker.def().rewards)
        {
            const ItemDef* def = findItemDef(reward.item);
            if (!def)
            {
                DE_LOG_WARN("Quest: unknown reward item '{}'", reward.item);
                continue;
            }
            const int added = addItem(*inv, *def, reward.count);
            if (added <= 0)
            {
                DE_LOG_INFO("Inventory: bag full");
                continue;
            }
            DE_LOG_INFO("Inventory: picked up {} x{}", def->name, added);
            const bool wearable = def->slot != EquipSlot::None && (def->type == ItemType::Armor || def->type == ItemType::Tool);
            if (wearable && inv->equipped[static_cast<int>(def->slot)].empty())
            {
                for (int i = 0; i < InventoryComponent::kBagSize; ++i)
                {
                    if (inv->bag[i].def == def && equipItem(*inv, i))
                    {
                        DE_LOG_INFO("Inventory: wearing {}", def->name);
                        break;
                    }
                }
            }
            ++granted;
        }
        m_rewardsGranted = true;
        return granted;
    }

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
