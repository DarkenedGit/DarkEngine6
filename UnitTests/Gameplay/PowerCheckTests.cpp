#include "Gameplay/PowerCheck.h"

#include <gtest/gtest.h>

using namespace Dark;

TEST(PowerCheck, InvestigatingStaysOnTheTask)
{
    EXPECT_FALSE(powerCheckerShouldClear(AI::Alertness::Investigating, false));
    EXPECT_FALSE(powerCheckerShouldClear(AI::Alertness::Investigating, true));
    EXPECT_FALSE(powerCheckerShouldClear(AI::Alertness::Calm, false));
    EXPECT_FALSE(powerCheckerShouldClear(AI::Alertness::Calm, true));
    EXPECT_FALSE(powerCheckerShouldClear(AI::Alertness::Suspicious, true));
}

TEST(PowerCheck, AlertSearchAndVisualNoticeClear)
{
    EXPECT_TRUE(powerCheckerShouldClear(AI::Alertness::Suspicious, false));
    EXPECT_TRUE(powerCheckerShouldClear(AI::Alertness::Alert, false));
    EXPECT_TRUE(powerCheckerShouldClear(AI::Alertness::Alert, true));
    EXPECT_TRUE(powerCheckerShouldClear(AI::Alertness::Searching, false));
    EXPECT_TRUE(powerCheckerShouldClear(AI::Alertness::Searching, true));
}
