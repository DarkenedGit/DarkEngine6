#include <gtest/gtest.h>

#include "Play/PlaySession.h"

using namespace Dark;

TEST(PlaySession, ScaleDtSlowsWhileHitStop)
{
    PlaySession play;
    play.runtime().hitStop = 0.2f;
    const float scaled     = play.scaleDt(0.05f);
    EXPECT_FLOAT_EQ(scaled, 0.0025f);
    EXPECT_FLOAT_EQ(play.runtime().hitStop, 0.15f);
}

TEST(PlaySession, ResetPawnKeepsLookAndClearsHitStop)
{
    PlaySession play;
    play.runtime().lookYaw = 1.25f;
    play.runtime().hitStop = 0.4f;
    play.resetPawn();
    EXPECT_FLOAT_EQ(play.runtime().lookYaw, 1.25f);
    EXPECT_FLOAT_EQ(play.runtime().hitStop, 0.0f);
}

TEST(PlaySession, TickPawnWithoutHostIsSafe)
{
    PlaySession play;
    play.tickPawn(0.016f);
}
