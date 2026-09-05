#include <gtest/gtest.h>

#include "AI/Brain.h"

using namespace Dark::AI;

TEST(Hsm, StartsInWander)
{
    Brain b;
    ASSERT_TRUE(b.start());
    EXPECT_EQ(b.leaf(), Leaf::Wander);
}

TEST(Hsm, SeeThenBriefLoseStaysChase)
{
    Brain b;
    ASSERT_TRUE(b.start());
    b.tick(0.016f, true, false);
    EXPECT_EQ(b.leaf(), Leaf::Chase);
    b.tick(0.1f, false, false);
    EXPECT_EQ(b.leaf(), Leaf::Memory);
    b.tick(0.2f, true, false);
    EXPECT_EQ(b.leaf(), Leaf::Chase);
}

TEST(Hsm, LoseLongerThanMemoryGoesWander)
{
    Brain b;
    ASSERT_TRUE(b.start());
    b.tick(0.016f, true, false);
    b.tick(0.016f, false, false);
    EXPECT_EQ(b.leaf(), Leaf::Memory);
    b.tick(Brain::kMemorySec + 0.1f, false, false);
    EXPECT_EQ(b.leaf(), Leaf::Wander);
}

TEST(Hsm, WetFromChaseGoesWander)
{
    Brain b;
    ASSERT_TRUE(b.start());
    b.tick(0.016f, true, false);
    b.tick(0.016f, true, true);
    EXPECT_EQ(b.leaf(), Leaf::Wander);
}

TEST(Hsm, WetFromMemoryGoesWander)
{
    Brain b;
    ASSERT_TRUE(b.start());
    b.tick(0.016f, true, false);
    b.tick(0.016f, false, false);
    EXPECT_EQ(b.leaf(), Leaf::Memory);
    b.tick(0.016f, false, true);
    EXPECT_EQ(b.leaf(), Leaf::Wander);
}

TEST(Hsm, IndependentBrains)
{
    Brain a;
    Brain b;
    ASSERT_TRUE(a.start());
    ASSERT_TRUE(b.start());
    a.tick(0.016f, true, false);
    b.tick(0.016f, false, false);
    EXPECT_EQ(a.leaf(), Leaf::Chase);
    EXPECT_EQ(b.leaf(), Leaf::Wander);
}
