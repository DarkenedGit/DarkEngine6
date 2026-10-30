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
            { "arm_guards", "Arm Guards", ItemType::Armor, EquipSlot::Arms, 1, 0.0f, 40.0f },
            { "leg_guards", "Leg Guards", ItemType::Armor, EquipSlot::Legs, 1, 0.0f, 45.0f },
            { "keycard_red", "Red Keycard", ItemType::Key, EquipSlot::None, 1, 0.0f, 0.0f },
            { "flashlight", "Flashlight", ItemType::Tool, EquipSlot::Light, 1, 0.0f, 0.0f },
            { "research_file", "Research File", ItemType::Quest, EquipSlot::None, 10, 0.0f, 0.0f },
            { "bio_sample", "Bio-Sample", ItemType::Quest, EquipSlot::None, 10, 0.0f, 0.0f },
            { "research", "Research Data", ItemType::Resource, EquipSlot::None, 99, 0.0f, 0.0f },
            { "bio_material", "Bio Material", ItemType::Resource, EquipSlot::None, 99, 0.0f, 0.0f },
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
        case ItemType::Tool:       return "Tool";
        }
        return "";
    }

    const char* equipSlotName(EquipSlot slot)
    {
        switch (slot)
        {
        case EquipSlot::Head:  return "Head";
        case EquipSlot::Chest: return "Chest";
        case EquipSlot::Light: return "Light";
        case EquipSlot::Arms:  return "Arms";
        case EquipSlot::Legs:  return "Legs";
        default:               return "";
        }
    }

} // namespace Dark
