#pragma once

#include "Gameplay/Inventory.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Dark
{

    struct UpgradeCost
    {
        std::string item;
        int         count = 1;
    };

    // content/upgrades.json. effect is one of: breath, quiet, health, speed, exposure.
    struct UpgradeDef
    {
        std::string              id;
        std::string              name;
        std::string              description;
        std::string              effect;
        float                    value = 1.0f;
        std::vector<UpgradeCost> cost;
    };

    bool parseUpgrades(std::string_view jsonText, std::vector<UpgradeDef>& out, std::string* error = nullptr);
    bool loadUpgrades(const std::filesystem::path& path, std::vector<UpgradeDef>& out, std::string* error = nullptr);

    int  heldCount(const InventoryComponent& inv, std::string_view itemId);
    bool canAfford(const InventoryComponent& inv, const UpgradeDef& up);
    // Takes the cost out of the bag. Returns false and changes nothing when the bag is short.
    bool payFor(InventoryComponent& inv, const UpgradeDef& up);

    // Multipliers and bonuses from every owned upgrade. 1 or 0 when nothing is owned.
    struct UpgradeEffects
    {
        float breath   = 1.0f;
        float quiet    = 1.0f;
        float health   = 0.0f;
        float speed    = 1.0f;
        float exposure = 1.0f;
    };
    UpgradeEffects combineUpgrades(const std::vector<UpgradeDef>& defs, const std::vector<std::string>& owned);

} // namespace Dark
