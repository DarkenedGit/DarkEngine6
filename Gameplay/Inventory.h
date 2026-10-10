#pragma once

#include "ECS/Entity.h"
#include "Gameplay/Item.h"

#include <string_view>

namespace Dark
{

    class World;

    struct ItemStack
    {
        const ItemDef* def   = nullptr;
        int            count = 0;

        bool empty() const { return !def || count <= 0; }
    };

    struct InventoryComponent
    {
        static constexpr const char* kTypeName = "Inventory";
        static constexpr int         kBagSize  = 20;

        ItemStack bag[kBagSize]{};
        ItemStack equipped[static_cast<int>(EquipSlot::Count)]{};
    };

    struct ItemPickupComponent
    {
        static constexpr const char* kTypeName = "ItemPickup";

        ItemStack item{};
    };

    struct LootComponent
    {
        static constexpr const char* kTypeName = "Loot";

        const char*        label = "Loot";
        InventoryComponent contents{};
    };

    int itemCount(const InventoryComponent& inv);

    int       addItem(InventoryComponent& inv, const ItemDef& def, int count);
    bool      hasItem(const InventoryComponent& inv, std::string_view id);
    bool      equipItem(InventoryComponent& inv, int bagIndex);
    bool      unequipItem(InventoryComponent& inv, EquipSlot slot);
    bool      useItem(World& world, Entity owner, InventoryComponent& inv, int bagIndex);
    ItemStack takeItem(InventoryComponent& inv, int bagIndex);
    float     equippedArmor(const InventoryComponent& inv);

} // namespace Dark
