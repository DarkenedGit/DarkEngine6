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

TEST(Hsm, AssistFromWander)
{
    Brain b;
    ASSERT_TRUE(b.start());
    b.onAssist();
    EXPECT_EQ(b.leaf(), Leaf::Assist);
}

TEST(Hsm, AssistSeeGoesChase)
{
    Brain b;
    ASSERT_TRUE(b.start());
    b.onAssist();
    b.tick(0.016f, true, false);
    EXPECT_EQ(b.leaf(), Leaf::Chase);
}

TEST(Hsm, AssistDoneGoesWander)
{
    Brain b;
    ASSERT_TRUE(b.start());
    b.onAssist();
    ASSERT_EQ(b.leaf(), Leaf::Assist);
    b.onAssistDone();
    EXPECT_EQ(b.leaf(), Leaf::Wander);
}

TEST(Hsm, AssistIgnoresLose)
{
    Brain b;
    ASSERT_TRUE(b.start());
    b.onAssist();
    b.tick(0.5f, false, false);
    EXPECT_EQ(b.leaf(), Leaf::Assist);
}

TEST(Hsm, ChaseIgnoresAssistEvent)
{
    Brain b;
    ASSERT_TRUE(b.start());
    b.tick(0.016f, true, false);
    ASSERT_EQ(b.leaf(), Leaf::Chase);
    b.onAssist();
    EXPECT_EQ(b.leaf(), Leaf::Chase);
}

TEST(Hsm, FleeFromChaseIgnoresSee)
{
    Brain b;
    ASSERT_TRUE(b.start());
    b.tick(0.016f, true, false);
    ASSERT_EQ(b.leaf(), Leaf::Chase);
    b.onFlee();
    EXPECT_EQ(b.leaf(), Leaf::Flee);
    b.tick(0.016f, true, false);
    EXPECT_EQ(b.leaf(), Leaf::Flee);
}

TEST(Hsm, FleeOverridesAssist)
{
    Brain b;
    ASSERT_TRUE(b.start());
    b.onAssist();
    ASSERT_EQ(b.leaf(), Leaf::Assist);
    b.onFlee();
    EXPECT_EQ(b.leaf(), Leaf::Flee);
    b.onAssist();
    EXPECT_EQ(b.leaf(), Leaf::Flee);
}

TEST(Hsm, FleeDoneGoesWander)
{
    Brain b;
    ASSERT_TRUE(b.start());
    b.onFlee();
    ASSERT_EQ(b.leaf(), Leaf::Flee);
    b.onFleeDone();
    EXPECT_EQ(b.leaf(), Leaf::Wander);
}

TEST(Hsm, WetFromAssistGoesWander)
{
    Brain b;
    ASSERT_TRUE(b.start());
    b.onAssist();
    b.tick(0.016f, false, true);
    EXPECT_EQ(b.leaf(), Leaf::Wander);
}

TEST(Hsm, WetFromFleeGoesWander)
{
    Brain b;
    ASSERT_TRUE(b.start());
    b.onFlee();
    b.tick(0.016f, true, true);
    EXPECT_EQ(b.leaf(), Leaf::Wander);
}

TEST(Hsm, MemoryCanAssist)
{
    Brain b;
    ASSERT_TRUE(b.start());
    b.tick(0.016f, true, false);
    b.tick(0.016f, false, false);
    ASSERT_EQ(b.leaf(), Leaf::Memory);
    b.onAssist();
    EXPECT_EQ(b.leaf(), Leaf::Assist);
}

TEST(Hsm, AssistDoneThenSeeChases)
{
    Brain b;
    ASSERT_TRUE(b.start());
    b.onAssist();
    b.onAssistDone();
    ASSERT_EQ(b.leaf(), Leaf::Wander);
    b.tick(0.016f, true, false);
    EXPECT_EQ(b.leaf(), Leaf::Chase);
}
