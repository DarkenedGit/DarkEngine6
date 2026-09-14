#include <gtest/gtest.h>

#include "AI/HsmGraph.h"
#include "AI/HsmStatechartLayout.h"

using namespace Dark;

TEST(HsmStatechartLayout, HunterLeavesSitInsideRoot)
{
    HsmGraphDef def = makeHunterHsmGraph();
    HsmChartLayout layout;
    ASSERT_TRUE(layoutHsmStatechart(def, layout));
    const HsmChartBox* root = findHsmChartBox(layout, def.findStateIndex("Root"));
    const HsmChartBox* wander = findHsmChartBox(layout, def.findStateIndex("Wander"));
    const HsmChartBox* chase  = findHsmChartBox(layout, def.findStateIndex("Chase"));
    ASSERT_NE(root, nullptr);
    ASSERT_NE(wander, nullptr);
    ASSERT_NE(chase, nullptr);
    EXPECT_EQ(root->depth, 0);
    EXPECT_EQ(wander->depth, 1);
    EXPECT_GE(wander->x, root->x);
    EXPECT_LE(wander->x + wander->w, root->x + root->w + 0.01f);
    EXPECT_GE(wander->y, root->y);
    EXPECT_LE(wander->y + wander->h, root->y + root->h + 0.01f);
    EXPECT_GT(chase->x, wander->x);
    EXPECT_EQ(hitTestHsmChart(layout, wander->x + 4.0f, wander->y + 4.0f), def.findStateIndex("Wander"));
}

TEST(HsmStatechartLayout, PlayerNestsAliveChildren)
{
    HsmGraphDef def = makePlayerHsmGraph();
    HsmChartLayout layout;
    ASSERT_TRUE(layoutHsmStatechart(def, layout));
    const HsmChartBox* alive    = findHsmChartBox(layout, def.findStateIndex("Alive"));
    const HsmChartBox* grounded = findHsmChartBox(layout, def.findStateIndex("Grounded"));
    const HsmChartBox* dead     = findHsmChartBox(layout, def.findStateIndex("Dead"));
    ASSERT_NE(alive, nullptr);
    ASSERT_NE(grounded, nullptr);
    ASSERT_NE(dead, nullptr);
    EXPECT_EQ(alive->depth, 1);
    EXPECT_EQ(grounded->depth, 2);
    EXPECT_GE(grounded->x, alive->x);
    EXPECT_LE(grounded->x + grounded->w, alive->x + alive->w + 0.01f);
    EXPECT_LE(grounded->y + grounded->h, alive->y + alive->h + 0.01f);
    EXPECT_GT(dead->x, alive->x);
    EXPECT_EQ(dead->depth, 1);
}
