#include "Gameplay/Inventory.h"

#include <gtest/gtest.h>

using namespace Dark;

TEST(Inventory, CountItemSumsBagAndEquipped)
{
    ItemDef file;
    file.id = "research_file";
    ItemDef other;
    other.id = "medkit";

    InventoryComponent inv;
    inv.bag[0]                                        = { &file, 2 };
    inv.bag[1]                                        = { &other, 4 };
    inv.equipped[static_cast<int>(EquipSlot::Chest)] = { &file, 1 };

    EXPECT_EQ(countItem(inv, "research_file"), 3);
    EXPECT_EQ(countItem(inv, "medkit"), 4);
    EXPECT_EQ(countItem(inv, "missing"), 0);
}
