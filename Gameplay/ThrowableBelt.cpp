#include "Gameplay/ThrowableBelt.h"

#include "Character/ItemView.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/ThrowableCatalog.h"
#include "Physics/PhysicsWorld.h"

#include <deque>
#include <string>

namespace Dark
{

    namespace
    {

        const Math::Vector3f kRockOffset{ 1.25f, 0.15f, 1.10f };
        const Math::Vector3f kBottleOffset{ -1.25f, 0.15f, 1.10f };

        bool isThrowableId(std::string_view id)
        {
            return id.data() != nullptr && throwableCatalog().find(id) != nullptr;
        }

        Entity spawnCatalogPickup(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer, Physics::PhysicsWorld& physics,
                                  std::string_view id, const Math::Vector3f& position)
        {
            const ItemDef* def = throwablePickupDef(id);
            if (!def)
                return {};
            return spawnItemPickup(world, pins, assets, renderer, physics, ItemStack{ def, 1 }, position, Math::Vector3f{});
        }

        void warnIfSpawnFailed(Entity spawned, std::string_view id)
        {
            if (!spawned.valid())
                DE_LOG_WARN(LogCategory::Core, "ThrowableBelt: could not spawn '{}'", id);
        }

    } // namespace

    int ThrowableBelt::count(std::string_view id) const
    {
        for (const Count& row : m_counts)
        {
            if (row.id == id)
                return row.count;
        }
        return 0;
    }

    bool ThrowableBelt::any() const
    {
        for (const Count& row : m_counts)
        {
            if (row.count > 0)
                return true;
        }
        return false;
    }

    int ThrowableBelt::consume(std::string_view id)
    {
        for (auto it = m_counts.begin(); it != m_counts.end(); ++it)
        {
            if (it->id != id)
                continue;
            if (it->count <= 0)
            {
                m_counts.erase(it);
                return -1;
            }
            const int left = --it->count;
            if (left <= 0)
                m_counts.erase(it);
            return left;
        }
        return -1;
    }

    std::string_view ThrowableBelt::hand() const
    {
        return m_hand;
    }

    bool ThrowableBelt::setHand(std::string_view id)
    {
        if (id.empty())
        {
            m_hand.clear();
            return true;
        }
        if (!isThrowableId(id))
            return false;
        m_hand.assign(id.data(), id.size());
        return true;
    }

    int ThrowableBelt::add(std::string_view id)
    {
        if (!isThrowableId(id))
            return 0;
        for (Count& row : m_counts)
        {
            if (row.id == id)
                return ++row.count;
        }
        m_counts.push_back(Count{ std::string(id), 1 });
        return 1;
    }

    bool ThrowableBelt::setCount(std::string_view id, int count)
    {
        if (!isThrowableId(id))
            return false;
        for (auto it = m_counts.begin(); it != m_counts.end(); ++it)
        {
            if (it->id != id)
                continue;
            if (count <= 0)
                m_counts.erase(it);
            else
                it->count = count;
            return true;
        }
        if (count <= 0)
            return true;
        m_counts.push_back(Count{ std::string(id), count });
        return true;
    }

    ThrowableBelt& throwableBelt(World& world, Entity actor)
    {
        if (ThrowableBeltComponent* existing = world.get<ThrowableBeltComponent>(actor))
            return existing->belt;
        return world.emplace<ThrowableBeltComponent>(actor).belt;
    }

    CarryPickupResult takeCarryPickup(World& world, AssetPinTable& pins, Physics::PhysicsWorld& physics, Entity pickup, ThrowableBelt& belt, InventoryComponent* bag)
    {
        CarryPickupResult result;
        ItemPickupComponent* itemPickup = pickup.valid() ? world.get<ItemPickupComponent>(pickup) : nullptr;
        if (!itemPickup || itemPickup->item.empty() || !itemPickup->item.def)
            return result;

        const std::string_view id = itemPickup->item.def->id;
        if (isThrowableId(id))
        {
            if (belt.add(id) <= 0)
                return result;
            destroyPickup(world, pins, physics, pickup);
            result.throwable = true;
            return result;
        }

        if (!bag)
            return result;

        const int added = addItem(*bag, *itemPickup->item.def, itemPickup->item.count);
        itemPickup->item.count -= added;
        if (itemPickup->item.count <= 0)
            destroyPickup(world, pins, physics, pickup);
        result.bagAdded = added;
        return result;
    }

    bool worldHasThrowablePickup(World& world, std::string_view id)
    {
        if (!isThrowableId(id))
            return false;
        bool found = false;
        world.each<ItemPickupComponent>([&](Entity, ItemPickupComponent& pickup) {
            if (found || pickup.item.empty() || !pickup.item.def)
                return;
            if (id == pickup.item.def->id)
                found = true;
        });
        return found;
    }

    std::vector<ThrowablePickupSpot> captureThrowablePickups(World& world)
    {
        std::vector<ThrowablePickupSpot> spots;
        world.each<ItemPickupComponent>([&](Entity e, ItemPickupComponent& pickup) {
            if (pickup.item.empty() || !pickup.item.def || !isThrowableId(pickup.item.def->id))
                return;
            const TransformComponent* xf = world.get<TransformComponent>(e);
            if (!xf)
                return;
            ThrowablePickupSpot spot;
            spot.id       = pickup.item.def->id;
            spot.position = xf->position;
            spots.push_back(std::move(spot));
        });
        return spots;
    }

    void respawnThrowablePickups(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer, Physics::PhysicsWorld& physics,
                                 const std::vector<ThrowablePickupSpot>& spots)
    {
        for (const ThrowablePickupSpot& spot : spots)
            warnIfSpawnFailed(spawnCatalogPickup(world, pins, assets, renderer, physics, spot.id, spot.position), spot.id);
    }

    const ItemDef* throwablePickupDef(std::string_view id)
    {
        if (!isThrowableId(id))
            return nullptr;

        struct Held
        {
            std::string id;
            std::string name;
            ItemDef     def{};
        };
        static std::deque<Held> held;
        for (Held& row : held)
        {
            if (row.id == id)
                return &row.def;
        }
        Held& row = held.emplace_back();
        row.id.assign(id.data(), id.size());
        row.name   = row.id;
        row.def.id = row.id.c_str();
        row.def.name = row.name.c_str();
        row.def.type = ItemType::Resource;
        row.def.slot = EquipSlot::None;
        row.def.maxStack = 1;
        return &row.def;
    }

    Entity findThrowablePickupNear(World& world, const Math::Vector3f& position, float radius)
    {
        Entity best{};
        float  bestD = radius * radius;
        world.each<ItemPickupComponent>([&](Entity e, ItemPickupComponent& pickup) {
            if (pickup.item.empty() || !pickup.item.def || !isThrowableId(pickup.item.def->id))
                return;
            const TransformComponent* xf = world.get<TransformComponent>(e);
            if (!xf)
                return;
            const float d = (xf->position - position).MagnitudeSqrd();
            if (d <= bestD)
            {
                best  = e;
                bestD = d;
            }
        });
        return best;
    }

    void stockMissingThrowablePickups(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer, Physics::PhysicsWorld& physics,
                                      const Math::Vector3f& origin, bool haveRock, bool haveBottle)
    {
        if (!haveRock)
            warnIfSpawnFailed(spawnCatalogPickup(world, pins, assets, renderer, physics, "rock", origin + kRockOffset), "rock");
        if (!haveBottle)
            warnIfSpawnFailed(spawnCatalogPickup(world, pins, assets, renderer, physics, "bottle", origin + kBottleOffset), "bottle");
    }

} // namespace Dark
