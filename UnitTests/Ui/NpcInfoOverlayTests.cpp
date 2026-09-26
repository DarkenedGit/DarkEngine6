#include <gtest/gtest.h>

#include "Ui/NpcInfoOverlay.h"

using Dark::NpcInfoOverlaySettings;
using Dark::NpcInfoSample;
using Dark::formatNpcInfoBox;

TEST(NpcInfoOverlay, DefaultIsStateOnly)
{
    NpcInfoOverlaySettings s{};
    EXPECT_TRUE(s.enabled);
    EXPECT_TRUE(s.showState);
    EXPECT_FALSE(s.showHealth);
    EXPECT_FALSE(s.showTarget);

    NpcInfoSample n{};
    n.state = "Chase";
    char buf[64];
    EXPECT_GT(formatNpcInfoBox(s, n, buf, sizeof(buf)), 0);
    EXPECT_STREQ(buf, "Chase");
}

TEST(NpcInfoOverlay, HealthAndTargetLines)
{
    NpcInfoOverlaySettings s{};
    s.showHealth = true;
    s.showTarget = true;
    NpcInfoSample n{};
    n.state     = "Wander";
    n.hasHealth = true;
    n.hp        = 40.0f;
    n.maxHp     = 100.0f;
    n.hasTarget = true;
    char buf[96];
    ASSERT_GT(formatNpcInfoBox(s, n, buf, sizeof(buf)), 0);
    EXPECT_STREQ(buf, "Wander\nHP 40/100\nTarget yes");
}

TEST(NpcInfoOverlay, TargetNoWhenUnset)
{
    NpcInfoOverlaySettings s{};
    s.showState  = false;
    s.showTarget = true;
    NpcInfoSample n{};
    n.hasTarget = false;
    char buf[32];
    ASSERT_GT(formatNpcInfoBox(s, n, buf, sizeof(buf)), 0);
    EXPECT_STREQ(buf, "Target no");
}

TEST(NpcInfoOverlay, DisabledOrEmptyIsBlank)
{
    NpcInfoOverlaySettings s{};
    s.enabled = false;
    NpcInfoSample n{};
    n.state = "Flee";
    char buf[32] = { 'x', '\0' };
    EXPECT_EQ(formatNpcInfoBox(s, n, buf, sizeof(buf)), 0);
    EXPECT_STREQ(buf, "");

    s.enabled   = true;
    s.showState = false;
    n.state     = "Flee";
    EXPECT_EQ(formatNpcInfoBox(s, n, buf, sizeof(buf)), 0);
}
