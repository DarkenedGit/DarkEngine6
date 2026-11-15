#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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
        Tool,
    };

    enum class EquipSlot : uint8_t
    {
        None,
        Head,
        Chest,
        Light,
        Arms,
        Legs,
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
    bool           tryParseItemType(std::string_view text, ItemType& out);
    bool           tryParseEquipSlot(std::string_view text, EquipSlot& out);

    // Replaces the compiled table with content/items/*.items.json. Missing files keep the compiled rows.
    void reloadItemCatalog();

    struct LoadoutGrant
    {
        std::string id;
        int         count = 1;
    };

    // content/player/loadout.json. False leaves `out` empty so the caller can use its built-in list.
    bool loadStartingLoadout(std::vector<LoadoutGrant>& out);

} // namespace Dark
