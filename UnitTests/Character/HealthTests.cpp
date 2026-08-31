#include <gtest/gtest.h>

#include "Character/Health.h"

using namespace Dark;

TEST(Health, StartsFullAndAlive)
{
    Health h;
    EXPECT_TRUE(h.alive());
    EXPECT_FALSE(h.dead());
    EXPECT_FLOAT_EQ(h.hp(), h.maxHp());
    EXPECT_FLOAT_EQ(h.ratio(), 1.0f);
}

TEST(Health, DamageLowersHpAndBlocksRegenUntilDelay)
{
    HealthSettings s;
    s.maxHp       = 100.0f;
    s.regenPerSec = 50.0f;
    s.regenDelay  = 1.0f;
    Health h{ s };
    EXPECT_FALSE(h.applyDamage(40.0f));
    EXPECT_FLOAT_EQ(h.hp(), 60.0f);
    h.tick(0.5f);
    EXPECT_FLOAT_EQ(h.hp(), 60.0f);
    h.tick(0.6f);
    EXPECT_GT(h.hp(), 60.0f);
}

TEST(Health, KillingBlowReportsDeathAndStopsRegen)
{
    HealthSettings s;
    s.maxHp       = 10.0f;
    s.regenPerSec = 100.0f;
    s.regenDelay  = 0.0f;
    Health h{ s };
    EXPECT_TRUE(h.applyDamage(10.0f));
    EXPECT_TRUE(h.dead());
    EXPECT_FLOAT_EQ(h.hp(), 0.0f);
    h.tick(1.0f);
    EXPECT_TRUE(h.dead());
    EXPECT_FLOAT_EQ(h.hp(), 0.0f);
    h.heal(5.0f);
    EXPECT_TRUE(h.dead());
}

TEST(Health, HealDoesNotExceedMax)
{
    Health h;
    h.applyDamage(10.0f);
    h.heal(50.0f);
    EXPECT_FLOAT_EQ(h.hp(), h.maxHp());
}

TEST(Health, ReviveRestoresLife)
{
    Health h;
    h.applyDamage(h.maxHp());
    ASSERT_TRUE(h.dead());
    h.revive();
    EXPECT_TRUE(h.alive());
    EXPECT_FLOAT_EQ(h.hp(), h.maxHp());
}

TEST(Health, ExtraDamageOnCorpseDoesNothing)
{
    Health h;
    EXPECT_TRUE(h.applyDamage(h.maxHp()));
    EXPECT_FALSE(h.applyDamage(5.0f));
    EXPECT_FLOAT_EQ(h.hp(), 0.0f);
}
