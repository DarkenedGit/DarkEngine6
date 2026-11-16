#include "Gameplay/Item.h"

#include "Content/ContentDb.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "Scene/EntityMaster.h"

#include "third_party/nlohmann/json.hpp"

#include <fstream>
#include <memory>
#include <sstream>

namespace Dark
{

    namespace
    {
        using json = nlohmann::json;

        struct OwnedItem
        {
            std::string id;
            std::string name;
            ItemDef     def{};
        };

        std::vector<std::unique_ptr<OwnedItem>>& catalog()
        {
            static std::vector<std::unique_ptr<OwnedItem>> items;
            return items;
        }

        std::vector<std::unique_ptr<OwnedItem>>& retiredItems()
        {
            static std::vector<std::unique_ptr<OwnedItem>> items;
            return items;
        }

        bool& catalogLoaded()
        {
            static bool loaded = false;
            return loaded;
        }

        bool parseItemType(std::string_view text, ItemType& out)
        {
            if (text == "weapon" || text == "Weapon") { out = ItemType::Weapon; return true; }
            if (text == "armor" || text == "Armor") { out = ItemType::Armor; return true; }
            if (text == "consumable" || text == "Consumable") { out = ItemType::Consumable; return true; }
            if (text == "key" || text == "Key") { out = ItemType::Key; return true; }
            if (text == "quest" || text == "Quest") { out = ItemType::Quest; return true; }
            if (text == "resource" || text == "Resource") { out = ItemType::Resource; return true; }
            if (text == "tool" || text == "Tool") { out = ItemType::Tool; return true; }
            return false;
        }

        bool parseEquipSlot(std::string_view text, EquipSlot& out)
        {
            if (text.empty() || text == "none" || text == "None") { out = EquipSlot::None; return true; }
            if (text == "head" || text == "Head") { out = EquipSlot::Head; return true; }
            if (text == "chest" || text == "Chest") { out = EquipSlot::Chest; return true; }
            if (text == "light" || text == "Light") { out = EquipSlot::Light; return true; }
            if (text == "arms" || text == "Arms") { out = EquipSlot::Arms; return true; }
            if (text == "legs" || text == "Legs") { out = EquipSlot::Legs; return true; }
            return false;
        }

        void addOwned(std::string id, std::string name, ItemType type, EquipSlot slot, int maxStack, float heal, float armor)
        {
            for (const std::unique_ptr<OwnedItem>& existing : catalog())
            {
                if (existing && existing->id == id)
                {
                    existing->name = std::move(name);
                    existing->def.name = existing->name.c_str();
                    existing->def.type = type;
                    existing->def.slot = slot;
                    existing->def.maxStack = maxStack;
                    existing->def.heal = heal;
                    existing->def.armor = armor;
                    return;
                }
            }
            std::unique_ptr<OwnedItem> owned = std::make_unique<OwnedItem>();
            owned->id = std::move(id);
            owned->name = std::move(name);
            owned->def.id = owned->id.c_str();
            owned->def.name = owned->name.c_str();
            owned->def.type = type;
            owned->def.slot = slot;
            owned->def.maxStack = maxStack;
            owned->def.heal = heal;
            owned->def.armor = armor;
            catalog().push_back(std::move(owned));
        }

        bool loadItemsFile(const std::filesystem::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            std::ostringstream ss;
            ss << in.rdbuf();
            const json root = json::parse(ss.str(), nullptr, false);
            if (root.is_discarded() || !root.is_object() || !root.contains("items") || !root["items"].is_array())
            {
                DE_LOG_WARN(LogCategory::Core, "Items: '{}' is not an items list", path.string());
                return false;
            }
            for (const json& item : root["items"])
            {
                if (!item.is_object() || !item.contains("id") || !item["id"].is_string())
                    continue;
                const std::string id = item["id"].get<std::string>();
                const std::string name = item.contains("name") && item["name"].is_string() ? item["name"].get<std::string>() : id;
                ItemType type = ItemType::Resource;
                EquipSlot slot = EquipSlot::None;
                if (item.contains("type") && item["type"].is_string())
                    parseItemType(item["type"].get<std::string>(), type);
                if (item.contains("slot") && item["slot"].is_string())
                    parseEquipSlot(item["slot"].get<std::string>(), slot);
                int maxStack = 1;
                float heal = 0.0f;
                float armor = 0.0f;
                if (item.contains("maxStack") && item["maxStack"].is_number())
                    maxStack = item["maxStack"].get<int>();
                if (item.contains("heal") && item["heal"].is_number())
                    heal = item["heal"].get<float>();
                if (item.contains("armor") && item["armor"].is_number())
                    armor = item["armor"].get<float>();
                if (!id.empty())
                    addOwned(id, name, type, slot, maxStack, heal, armor);
            }
            return true;
        }
    }

    bool tryParseItemType(std::string_view text, ItemType& out)
    {
        if (text == "weapon" || text == "Weapon") { out = ItemType::Weapon; return true; }
        if (text == "armor" || text == "Armor") { out = ItemType::Armor; return true; }
        if (text == "consumable" || text == "Consumable") { out = ItemType::Consumable; return true; }
        if (text == "key" || text == "Key") { out = ItemType::Key; return true; }
        if (text == "quest" || text == "Quest") { out = ItemType::Quest; return true; }
        if (text == "resource" || text == "Resource") { out = ItemType::Resource; return true; }
        if (text == "tool" || text == "Tool") { out = ItemType::Tool; return true; }
        return false;
    }

    bool tryParseEquipSlot(std::string_view text, EquipSlot& out)
    {
        if (text.empty() || text == "none" || text == "None") { out = EquipSlot::None; return true; }
        if (text == "head" || text == "Head") { out = EquipSlot::Head; return true; }
        if (text == "chest" || text == "Chest") { out = EquipSlot::Chest; return true; }
        if (text == "light" || text == "Light") { out = EquipSlot::Light; return true; }
        if (text == "arms" || text == "Arms") { out = EquipSlot::Arms; return true; }
        if (text == "legs" || text == "Legs") { out = EquipSlot::Legs; return true; }
        return false;
    }

    void reloadItemCatalog()
    {
        for (std::unique_ptr<OwnedItem>& row : catalog())
            retiredItems().push_back(std::move(row));
        catalog().clear();
        int files = 0;
        auto scan = [&](const std::filesystem::path& root) {
            const std::filesystem::path dir = root / "items";
            std::error_code ec;
            if (!std::filesystem::is_directory(dir, ec) || ec)
                return;
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir, ec))
            {
                if (ec || !entry.is_regular_file())
                    continue;
                if (!entry.path().filename().string().ends_with(".items.json"))
                    continue;
                if (loadItemsFile(entry.path()))
                    ++files;
            }
        };
        for (const std::filesystem::path& root : contentRootCandidates())
            scan(root);
        const std::vector<std::filesystem::path> overlays = contentOverlayRoots();
        for (auto it = overlays.rbegin(); it != overlays.rend(); ++it)
            scan(*it);
        catalogLoaded() = true;
        if (files == 0)
            DE_LOG_ERROR(LogCategory::Core, "Items: no items json found");
    }

    bool loadStartingLoadout(std::vector<LoadoutGrant>& out)
    {
        out.clear();
        const std::filesystem::path path = resolveContentFile("player/loadout.json");
        if (path.empty())
            return false;
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        const json root = json::parse(ss.str(), nullptr, false);
        if (root.is_discarded() || !root.is_object() || !root.contains("items") || !root["items"].is_array())
            return false;
        for (const json& item : root["items"])
        {
            if (!item.is_object() || !item.contains("id") || !item["id"].is_string())
                continue;
            LoadoutGrant grant;
            grant.id = item["id"].get<std::string>();
            if (item.contains("count") && item["count"].is_number())
                grant.count = item["count"].get<int>();
            if (!grant.id.empty() && grant.count > 0)
                out.push_back(std::move(grant));
        }
        return !out.empty();
    }

    const ItemDef* tryFindItemDef(std::string_view id)
    {
        if (id.empty())
            return nullptr;
        if (!catalogLoaded())
            reloadItemCatalog();
        for (const std::unique_ptr<OwnedItem>& row : catalog())
        {
            if (row && row->id == id)
                return &row->def;
        }
        return nullptr;
    }

    const ItemDef* findItemDef(std::string_view id)
    {
        const ItemDef* def = tryFindItemDef(id);
        if (!def && !id.empty())
            DE_LOG_WARN(LogCategory::Core, "Items: '{}' is not in the catalog", std::string(id));
        return def;
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
