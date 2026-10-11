#include "Gameplay/Inventory.h"
#include "Character/HealthComponent.h"
#include "ECS/World.h"

#include <algorithm>

namespace Dark
{

    namespace
    {
        bool validBagIndex(int i)
        {
            return i >= 0 && i < InventoryComponent::kBagSize;
        }

        bool hasFreeSlot(const InventoryComponent& inv)
        {
            for (const ItemStack& s : inv.bag)
            {
                if (s.empty())
                    return true;
            }
            return false;
        }
    }

    int addItem(InventoryComponent& inv, const ItemDef& def, int count)
    {
        int left = count;
        for (ItemStack& s : inv.bag)
        {
            if (left <= 0)
                break;
            if (s.def == &def && s.count < def.maxStack)
            {
                const int n = std::min(left, def.maxStack - s.count);
                s.count += n;
                left -= n;
            }
        }
        for (ItemStack& s : inv.bag)
        {
            if (left <= 0)
                break;
            if (s.empty())
            {
                const int n = std::min(left, def.maxStack);
                s           = { &def, n };
                left -= n;
            }
        }
        return count - left;
    }

    bool hasItem(const InventoryComponent& inv, std::string_view id)
    {
        for (const ItemStack& s : inv.bag)
        {
            if (!s.empty() && id == s.def->id)
                return true;
        }
        for (const ItemStack& s : inv.equipped)
        {
            if (!s.empty() && id == s.def->id)
                return true;
        }
        return false;
    }

    bool equipItem(InventoryComponent& inv, int bagIndex)
    {
        if (!validBagIndex(bagIndex) || inv.bag[bagIndex].empty())
            return false;
        const ItemDef* def = inv.bag[bagIndex].def;
        if (def->type != ItemType::Armor || def->slot == EquipSlot::None)
            return false;

        ItemStack&      slot = inv.equipped[static_cast<int>(def->slot)];
        const ItemStack old  = slot;
        if (!old.empty() && inv.bag[bagIndex].count > 1 && !hasFreeSlot(inv))
            return false;

        slot = { def, 1 };
        if (--inv.bag[bagIndex].count <= 0)
            inv.bag[bagIndex] = {};
        if (!old.empty())
            addItem(inv, *old.def, old.count);
        return true;
    }

    bool unequipItem(InventoryComponent& inv, EquipSlot slot)
    {
        ItemStack& s = inv.equipped[static_cast<int>(slot)];
        if (s.empty() || addItem(inv, *s.def, s.count) == 0)
            return false;
        s = {};
        return true;
    }

    bool useItem(World& world, Entity owner, InventoryComponent& inv, int bagIndex)
    {
        if (!validBagIndex(bagIndex) || inv.bag[bagIndex].empty())
            return false;
        const ItemDef* def = inv.bag[bagIndex].def;
        if (def->type != ItemType::Consumable)
            return false;

        HealthComponent* hp = world.get<HealthComponent>(owner);
        if (!hp || !hp->health.alive() || hp->health.hp() >= hp->health.maxHp())
            return false;
        hp->health.heal(def->heal);
        if (--inv.bag[bagIndex].count <= 0)
            inv.bag[bagIndex] = {};
        return true;
    }

    ItemStack takeItem(InventoryComponent& inv, int bagIndex)
    {
        if (!validBagIndex(bagIndex))
            return {};
        const ItemStack s = inv.bag[bagIndex];
        inv.bag[bagIndex] = {};
        return s;
    }

    int itemCount(const InventoryComponent& inv)
    {
        int n = 0;
        for (const ItemStack& s : inv.bag)
        {
            if (!s.empty())
                ++n;
        }
        return n;
    }

    float equippedArmor(const InventoryComponent& inv)
    {
        float armor = 0.0f;
        for (const ItemStack& s : inv.equipped)
        {
            if (!s.empty())
                armor += s.def->armor;
        }
        return armor;
    }

} // namespace Dark
