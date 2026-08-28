#include <gtest/gtest.h>

#include "AI/HunterBrain.h"

using namespace Dark::AI;

TEST(HunterHsm, StartsInWander)
{
    HunterBrain b;
    ASSERT_TRUE(b.start());
    EXPECT_EQ(b.leaf(), HunterLeaf::Wander);
}

TEST(HunterHsm, SeeThenBriefLoseStaysChase)
{
    HunterBrain b;
    ASSERT_TRUE(b.start());
    b.tick(0.016f, true, false);
    EXPECT_EQ(b.leaf(), HunterLeaf::Chase);
    b.tick(0.1f, false, false);
    EXPECT_EQ(b.leaf(), HunterLeaf::Memory);
    b.tick(0.2f, true, false);
    EXPECT_EQ(b.leaf(), HunterLeaf::Chase);
}

TEST(HunterHsm, LoseLongerThanMemoryGoesWander)
{
    HunterBrain b;
    ASSERT_TRUE(b.start());
    b.tick(0.016f, true, false);
    b.tick(0.016f, false, false);
    EXPECT_EQ(b.leaf(), HunterLeaf::Memory);
    b.tick(HunterBrain::kMemorySec + 0.1f, false, false);
    EXPECT_EQ(b.leaf(), HunterLeaf::Wander);
}

TEST(HunterHsm, WetFromChaseGoesWander)
{
    HunterBrain b;
    ASSERT_TRUE(b.start());
    b.tick(0.016f, true, false);
    b.tick(0.016f, true, true);
    EXPECT_EQ(b.leaf(), HunterLeaf::Wander);
}

TEST(HunterHsm, WetFromMemoryGoesWander)
{
    HunterBrain b;
    ASSERT_TRUE(b.start());
    b.tick(0.016f, true, false);
    b.tick(0.016f, false, false);
    EXPECT_EQ(b.leaf(), HunterLeaf::Memory);
    b.tick(0.016f, false, true);
    EXPECT_EQ(b.leaf(), HunterLeaf::Wander);
}

TEST(HunterHsm, IndependentBrains)
{
    HunterBrain a;
    HunterBrain b;
    ASSERT_TRUE(a.start());
    ASSERT_TRUE(b.start());
    a.tick(0.016f, true, false);
    b.tick(0.016f, false, false);
    EXPECT_EQ(a.leaf(), HunterLeaf::Chase);
    EXPECT_EQ(b.leaf(), HunterLeaf::Wander);
}
