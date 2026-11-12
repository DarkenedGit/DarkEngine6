#include <gtest/gtest.h>

#include "AI/AttackPattern.h"
#include "AI/HunterJump.h"

using namespace Dark;

TEST(HunterJumpTune, JsonJumpMoveSetsWindupDamageAndRange)
{
    AI::AttackPattern pattern;
    pattern.loaded = true;
    AI::AttackMove move;
    move.kind     = AI::AttackKind::Jump;
    move.minRange = 4.0f;
    move.maxRange = 9.0f;
    move.windup   = 0.40f;
    move.damage   = 22.0f;
    move.stunSeconds = 0.85f;
    move.poise    = 16.0f;
    pattern.attacks.push_back(move);

    const Combat::JumpAttackDef def = hunterJumpDefFromPattern(&pattern);
    EXPECT_FLOAT_EQ(def.telegraphSeconds, 0.40f);
    EXPECT_FLOAT_EQ(def.connectDamage, 22.0f);
    EXPECT_FLOAT_EQ(def.cooldown, 3.5f);
    EXPECT_FLOAT_EQ(def.poundDamage, 14.0f);
    EXPECT_FLOAT_EQ(def.knockdownDuration, 0.8f);
    EXPECT_FLOAT_EQ(def.homingRate, 1.5f);

    float minRange = 0.0f;
    float maxRange = 0.0f;
    hunterJumpRanges(&pattern, minRange, maxRange);
    EXPECT_FLOAT_EQ(minRange, 4.0f);
    EXPECT_FLOAT_EQ(maxRange, 9.0f);
}

TEST(HunterJumpTune, MissingPatternKeepsTheBuiltInPounce)
{
    const Combat::JumpAttackDef def = hunterJumpDefFromPattern(nullptr);
    EXPECT_FLOAT_EQ(def.telegraphSeconds, 0.40f);
    EXPECT_FLOAT_EQ(def.connectDamage, 22.0f);
    EXPECT_FLOAT_EQ(def.leapVerticalSpeed, 10.0f);
    EXPECT_FLOAT_EQ(def.gravity, 24.0f);

    float minRange = 0.0f;
    float maxRange = 0.0f;
    hunterJumpRanges(nullptr, minRange, maxRange);
    EXPECT_FLOAT_EQ(minRange, 4.0f);
    EXPECT_FLOAT_EQ(maxRange, 9.0f);
}
