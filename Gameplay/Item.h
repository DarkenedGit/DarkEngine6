#pragma once

#include <cstdint>
#include <string_view>

namespace Dark
{

    enum class ItemType : uint8_t
    {
        Weapon,
        Armor,
        Consumable,
        Key,
        Quest,
        Resource,
    };

    enum class EquipSlot : uint8_t
    {
        None,
        Head,
        Chest,
        Count,
    };

    struct ItemDef
    {
        const char* id       = "";
        const char* name     = "";
        ItemType    type     = ItemType::Resource;
        EquipSlot   slot     = EquipSlot::None;
        int         maxStack = 1;
        float       heal     = 0.0f;
        float       armor    = 0.0f;
    };

    const ItemDef* findItemDef(std::string_view id);
    const char*    itemTypeName(ItemType type);
    const char*    equipSlotName(EquipSlot slot);

} // namespace Dark
