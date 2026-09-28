#include <gtest/gtest.h>

#include "AI/AttackPattern.h"

using namespace Dark::AI;

TEST(AttackPattern, ChainThenPause)
{
    const char* text = R"({
        "version": 1,
        "id": "hunter",
        "gapSeconds": 1.0,
        "chainCount": 2,
        "afterChain": "pause",
        "pauseSeconds": 2.5,
        "packGapSeconds": 0.4,
        "attacks": [
            { "id": "swipe", "kind": "melee", "minRange": 0, "maxRange": 2.4, "windup": 0.4, "damage": 12, "stunSeconds": 0.28 },
            { "id": "pounce", "kind": "jump", "minRange": 4, "maxRange": 9, "windup": 0.4 }
        ]
    })";
    AttackPattern pattern;
    ASSERT_TRUE(parseAttackPattern(text, pattern));
    EXPECT_EQ(pickAttack(pattern, 6.0f, false, -1), 1);
    EXPECT_EQ(pickAttack(pattern, 1.5f, false, -1), 0);
    EXPECT_EQ(pickAttack(pattern, 3.0f, false, -1), -1);

    AttackClock clock;
    float pack = 0.0f;
    EXPECT_TRUE(attackClockReady(clock, pack));
    noteAttackStarted(pattern, clock, pack);
    EXPECT_EQ(clock.chain, 1);
    EXPECT_FLOAT_EQ(clock.gap, 1.0f);
    EXPECT_FLOAT_EQ(pack, 0.4f);
    EXPECT_FALSE(attackClockReady(clock, pack));

    tickAttackClock(clock, 1.0f);
    pack = 0.0f;
    EXPECT_TRUE(attackClockReady(clock, pack));
    noteAttackStarted(pattern, clock, pack);
    EXPECT_EQ(clock.chain, 0);
    EXPECT_FLOAT_EQ(clock.pause, 2.5f);
    EXPECT_FALSE(attackClockReady(clock, 0.0f));
    tickAttackClock(clock, 2.5f);
    EXPECT_TRUE(attackClockReady(clock, 0.0f));
}

TEST(AttackPattern, ChainThenRepositionPrefersOtherMove)
{
    const char* text = R"({
        "version": 1,
        "id": "wolf",
        "gapSeconds": 0.5,
        "chainCount": 1,
        "afterChain": "reposition",
        "repositionSeconds": 1.3,
        "repositionDistance": 4.5,
        "attacks": [
            { "id": "bite", "kind": "melee", "minRange": 0, "maxRange": 8, "windup": 0.2 },
            { "id": "pounce", "kind": "jump", "minRange": 0, "maxRange": 8, "windup": 0.2 }
        ]
    })";
    AttackPattern pattern;
    ASSERT_TRUE(parseAttackPattern(text, pattern));
    AttackClock clock;
    float pack = 0.0f;
    clock.lastAttack = 0;
    noteAttackStarted(pattern, clock, pack);
    EXPECT_GT(clock.reposition, 0.0f);
    EXPECT_TRUE(clock.preferOther);
    EXPECT_EQ(pickAttack(pattern, 2.0f, clock.preferOther, 0), 1);
}

TEST(AttackPattern, ContentFiles)
{
    const AttackPattern hunter = loadAttackPattern("ai/hunter.attacks.json");
    const AttackPattern wolf = loadAttackPattern("ai/wolf.attacks.json");
    ASSERT_TRUE(hunter.loaded);
    ASSERT_TRUE(wolf.loaded);
    EXPECT_EQ(hunter.afterChain, AttackAfterChain::Pause);
    EXPECT_EQ(wolf.afterChain, AttackAfterChain::Reposition);
    EXPECT_EQ(hunter.chainCount, 2);
    EXPECT_EQ(wolf.chainCount, 2);
    EXPECT_EQ(hunter.attacks.size(), 2u);
    EXPECT_EQ(wolf.attacks[0].id, "bite");
}
