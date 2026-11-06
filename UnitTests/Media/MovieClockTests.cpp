#include <gtest/gtest.h>

#include "Media/MovieClock.h"

using namespace Dark;

TEST(MovieClock, AudioMasterUsesSamplesAndLead)
{
    const double audio = movieAudioSeconds(48000 + 24000, 48000, 48000, 10.0);
    EXPECT_DOUBLE_EQ(audio, 10.5);

    MovieClockInput in;
    in.audioMaster   = true;
    in.audioSeconds  = audio;
    in.wallSeconds   = 0.0;
    in.leadSeconds   = kMovieAudioLeadSeconds;
    in.frameDuration = 1.0 / 30.0;

    const MovieClockOutput out = evaluateMovieClock(in);
    EXPECT_DOUBLE_EQ(out.targetPts, 10.5 - kMovieAudioLeadSeconds);
}

TEST(MovieClock, WallClockIgnoresLead)
{
    MovieClockInput in;
    in.audioMaster  = false;
    in.audioSeconds = 99.0;
    in.wallSeconds  = 3.0;
    in.leadSeconds  = 5.0;

    EXPECT_DOUBLE_EQ(evaluateMovieClock(in).targetPts, 3.0);
}

TEST(MovieClock, PauseDebtDoesNotMoveTargetPts)
{
    MovieClockInput in;
    in.audioMaster  = false;
    in.wallSeconds  = 12.0 - 7.5;
    in.audioSeconds = 99.0;
    in.leadSeconds  = kMovieAudioLeadSeconds;

    EXPECT_DOUBLE_EQ(evaluateMovieClock(in).targetPts, 4.5);
}

TEST(MovieClock, SegmentBaseKeepsARunningCounter)
{
    const double audio = movieAudioSeconds(96000 + 48000, 96000, 48000, 2.0);
    EXPECT_DOUBLE_EQ(audio, 3.0);

    MovieClockInput in;
    in.audioMaster  = true;
    in.audioSeconds = audio;
    in.leadSeconds  = kMovieAudioLeadSeconds;
    EXPECT_DOUBLE_EQ(evaluateMovieClock(in).targetPts, 3.0 - kMovieAudioLeadSeconds);
}

TEST(MovieClock, SegmentBaseZeroWhenCounterResets)
{
    const double audio = movieAudioSeconds(48000, 0, 48000, 2.0);
    EXPECT_DOUBLE_EQ(audio, 3.0);

    MovieClockInput in;
    in.audioMaster  = true;
    in.audioSeconds = audio;
    in.leadSeconds  = kMovieAudioLeadSeconds;
    EXPECT_DOUBLE_EQ(evaluateMovieClock(in).targetPts, 3.0 - kMovieAudioLeadSeconds);
}

TEST(MovieClock, DropRuleAtTwoDurations)
{
    const MovieQueuedFrame frames[] = {
        {1.0, 0},
        {1.4, 1},
        {2.4, 2},
    };

    const MovieFrameChoice dropped = selectMovieFrame(frames, 3, 2.0, 0.25, true);
    EXPECT_EQ(dropped.presentIndex, -1);
    EXPECT_TRUE(dropped.holdLast);
    EXPECT_FALSE(dropped.drawBlack);
    EXPECT_EQ(dropped.dropCount, 2);
    EXPECT_EQ(dropped.droppedIndex[0], 0);
    EXPECT_EQ(dropped.droppedIndex[1], 1);

    const MovieQueuedFrame onEdge[] = {
        {1.0, 0},
        {1.5, 1},
    };
    const MovieFrameChoice kept = selectMovieFrame(onEdge, 2, 2.0, 0.25, true);
    EXPECT_EQ(kept.presentIndex, 1);
    EXPECT_FALSE(kept.holdLast);
    EXPECT_EQ(kept.dropCount, 1);
    EXPECT_EQ(kept.droppedIndex[0], 0);
}

TEST(MovieClock, HoldWhenTheQueueIsEmpty)
{
    const MovieFrameChoice held = selectMovieFrame(nullptr, 0, 1.0, 0.25, true);
    EXPECT_TRUE(held.holdLast);
    EXPECT_FALSE(held.drawBlack);
    EXPECT_EQ(held.presentIndex, -1);
    EXPECT_EQ(held.dropCount, 0);

    const MovieFrameChoice black = selectMovieFrame(nullptr, 0, 1.0, 0.25, false);
    EXPECT_TRUE(black.drawBlack);
    EXPECT_FALSE(black.holdLast);
    EXPECT_EQ(black.presentIndex, -1);
}

TEST(MovieClock, DoNotPresentEarly)
{
    const MovieQueuedFrame frames[] = {
        {0.2, 1},
        {0.9, 2},
        {1.3, 3},
    };

    const MovieFrameChoice choice = selectMovieFrame(frames, 3, 1.0, 0.25, true);
    EXPECT_EQ(choice.presentIndex, 2);
    EXPECT_EQ(choice.dropCount, 1);
    EXPECT_EQ(choice.droppedIndex[0], 1);
    EXPECT_FALSE(choice.holdLast);

    const MovieQueuedFrame future[] = {{1.2, 4}};
    const MovieFrameChoice held     = selectMovieFrame(future, 1, 1.0, 0.25, true);
    EXPECT_EQ(held.presentIndex, -1);
    EXPECT_TRUE(held.holdLast);
    EXPECT_EQ(held.dropCount, 0);

    const MovieQueuedFrame stale[]  = {{0.0, 7}};
    const MovieFrameChoice noFrame  = selectMovieFrame(stale, 1, 1.0, 0.25, false);
    EXPECT_TRUE(noFrame.drawBlack);
    EXPECT_EQ(noFrame.presentIndex, -1);
    EXPECT_EQ(noFrame.dropCount, 1);
    EXPECT_EQ(noFrame.droppedIndex[0], 7);
}
