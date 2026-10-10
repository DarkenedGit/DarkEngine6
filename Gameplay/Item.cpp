#include "Gameplay/Item.h"

namespace Dark
{

    namespace
    {
        const ItemDef kItems[] = {
            { "sword", "Sword", ItemType::Weapon, EquipSlot::None, 1, 0.0f, 0.0f },
            { "rifle", "Rifle", ItemType::Weapon, EquipSlot::None, 1, 0.0f, 0.0f },
            { "medkit", "Medkit", ItemType::Consumable, EquipSlot::None, 5, 50.0f, 0.0f },
            { "helmet", "Helmet", ItemType::Armor, EquipSlot::Head, 1, 0.0f, 25.0f },
            { "chest", "Chest Plate", ItemType::Armor, EquipSlot::Chest, 1, 0.0f, 40.0f },
        };
    }

    const ItemDef* findItemDef(std::string_view id)
    {
        for (const ItemDef& def : kItems)
        {
            if (id == def.id)
                return &def;
        }
        return nullptr;
    }

    const char* itemTypeName(ItemType type)
    {
        switch (type)
        {
        case ItemType::Weapon:     return "Weapon";
        case ItemType::Armor:      return "Armor";
        case ItemType::Consumable: return "Consumable";
        case ItemType::Key:        return "Key";
        case ItemType::Quest:      return "Quest";
        case ItemType::Resource:   return "Resource";
        }
        return "";
    }

    const char* equipSlotName(EquipSlot slot)
    {
        switch (slot)
        {
        case EquipSlot::Head:  return "Head";
        case EquipSlot::Chest: return "Chest";
        default:               return "";
        }
    }

} // namespace Dark
