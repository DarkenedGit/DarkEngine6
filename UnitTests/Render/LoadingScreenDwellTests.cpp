#include <gtest/gtest.h>

#include "Render/LoadingScreen.h"

using namespace Dark;

TEST(LoadingScreenDwell, RemainingMatchesEngineMinSeconds)
{
    LoadingScreen ls;
    ls.setPhase(LoadingPhase::Engine);
    const float rem = ls.remainingDwell();
    EXPECT_GT(rem, 1.5f);
    EXPECT_LE(rem, 2.0f);
}

TEST(LoadingScreenDwell, SkipZerosRemaining)
{
    LoadingScreen ls;
    ls.setPhase(LoadingPhase::Engine);
    EXPECT_GT(ls.remainingDwell(), 0.0f);
    ls.skipCurrentPhaseDwell();
    EXPECT_FLOAT_EQ(ls.remainingDwell(), 0.0f);
}

TEST(LoadingScreenDwell, SetPhaseRestartsClock)
{
    LoadingScreen ls;
    ls.setPhase(LoadingPhase::Engine);
    ls.skipCurrentPhaseDwell();
    EXPECT_FLOAT_EQ(ls.remainingDwell(), 0.0f);

    ls.setPhase(LoadingPhase::Host);
    const float rem = ls.remainingDwell();
    EXPECT_GT(rem, 1.0f);
    EXPECT_LE(rem, 1.5f);
    EXPECT_EQ(ls.phase(), LoadingPhase::Host);
}

TEST(LoadingScreenDwell, FadeOutHasNoDwell)
{
    LoadingScreen ls;
    ls.setPhase(LoadingPhase::FadeOut);
    EXPECT_FLOAT_EQ(ls.remainingDwell(), 0.0f);
    ls.skipCurrentPhaseDwell();
    EXPECT_FLOAT_EQ(ls.remainingDwell(), 0.0f);
}

TEST(LoadingScreenDwell, HostSkipIndependentOfEngine)
{
    LoadingScreen ls;
    ls.setPhase(LoadingPhase::Host);
    EXPECT_GT(ls.remainingDwell(), 0.0f);
    ls.skipCurrentPhaseDwell();
    EXPECT_FLOAT_EQ(ls.remainingDwell(), 0.0f);
}
