#include "Core/ContentRoots.h"
#include "Gameplay/Item.h"
#include "Gameplay/Upgrades.h"

#include <gtest/gtest.h>

using namespace Dark;

TEST(Upgrades, PayTakesTheCostOnlyWhenAffordable)
{
    std::vector<UpgradeDef> defs;
    ASSERT_TRUE(parseUpgrades(R"({ "upgrades": [ { "id": "u", "effect": "speed", "value": 1.2,
        "cost": [ { "item": "research", "count": 3 }, { "item": "bio_material", "count": 1 } ] } ] })", defs));
    ASSERT_EQ(defs.size(), 1u);
    InventoryComponent inv;
    addItem(inv, *findItemDef("research"), 2);
    addItem(inv, *findItemDef("bio_material"), 1);
    EXPECT_FALSE(canAfford(inv, defs[0]));
    EXPECT_FALSE(payFor(inv, defs[0]));
    EXPECT_EQ(heldCount(inv, "research"), 2);
    addItem(inv, *findItemDef("research"), 2);
    EXPECT_TRUE(payFor(inv, defs[0]));
    EXPECT_EQ(heldCount(inv, "research"), 1);
    EXPECT_EQ(heldCount(inv, "bio_material"), 0);
}

TEST(Upgrades, EffectsStackFromOwnedOnly)
{
    std::vector<UpgradeDef> defs;
    ASSERT_TRUE(parseUpgrades(R"({ "upgrades": [ { "id": "a", "effect": "health", "value": 25 }, { "id": "b", "effect": "quiet", "value": 0.5 },
        { "id": "c", "effect": "speed", "value": 1.1 } ] })", defs));
    const UpgradeEffects fx = combineUpgrades(defs, { "a", "b" });
    EXPECT_FLOAT_EQ(fx.health, 25.0f);
    EXPECT_FLOAT_EQ(fx.quiet, 0.5f);
    EXPECT_FLOAT_EQ(fx.speed, 1.0f);
}

TEST(Upgrades, WrongTypeFailsAndShippedFileLoads)
{
    std::vector<UpgradeDef> defs;
    std::string             error;
    EXPECT_FALSE(parseUpgrades(R"({ "upgrades": [ { "id": "a", "effect": "health", "value": "lots" } ] })", defs, &error));
    bool loaded = false;
    for (const std::filesystem::path& root : contentRootCandidates())
    {
        if (loadUpgrades(root / "upgrades.json", defs))
        {
            loaded = true;
            break;
        }
    }
    ASSERT_TRUE(loaded);
    EXPECT_GE(defs.size(), 5u);
    for (const UpgradeDef& up : defs)
        for (const UpgradeCost& c : up.cost)
            EXPECT_NE(findItemDef(c.item), nullptr) << c.item;
}
