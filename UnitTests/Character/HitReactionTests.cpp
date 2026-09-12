#include <gtest/gtest.h>

#include "Character/HitReaction.h"

using namespace Dark;
using namespace Dark::Math;

TEST(HitReaction, ApplyStunsForConfiguredDuration)
{
    HitReactionSettings s;
    s.stunSeconds       = 0.40f;
    s.knockbackDistance = 0.0f;
    HitReaction r{ s };
    r.apply(Vector3f{ 0.0f, 0.0f, 1.0f });
    EXPECT_TRUE(r.stunned());
    EXPECT_NEAR(r.stunRemaining(), 0.40f, 1.0e-4f);
    r.tick(0.25f);
    EXPECT_TRUE(r.stunned());
    r.tick(0.20f);
    EXPECT_FALSE(r.stunned());
}

TEST(HitReaction, ZeroStunDoesNotLock)
{
    HitReactionSettings s;
    s.stunSeconds       = 0.0f;
    s.knockbackDistance = 1.0f;
    HitReaction r{ s };
    r.apply(Vector3f{ 1.0f, 0.0f, 0.0f });
    EXPECT_FALSE(r.stunned());
    EXPECT_TRUE(r.knockingBack());
}

TEST(HitReaction, InstantKnockbackCoversFullDistance)
{
    HitReactionSettings s;
    s.stunSeconds       = 0.0f;
    s.knockbackDistance = 2.0f;
    s.knockbackSeconds  = 0.0f;
    s.horizontalOnly    = true;
    HitReaction r{ s };
    r.apply(Vector3f{ 0.0f, 4.0f, 3.0f });
    const Vector3f d = r.tick(1.0f / 60.0f);
    EXPECT_NEAR(d.x, 0.0f, 1.0e-4f);
    EXPECT_NEAR(d.y, 0.0f, 1.0e-4f);
    EXPECT_NEAR(d.z, 2.0f, 1.0e-4f);
    EXPECT_FALSE(r.knockingBack());
    const Vector3f none = r.tick(1.0f / 60.0f);
    EXPECT_NEAR(none.Magnitude(), 0.0f, 1.0e-5f);
}

TEST(HitReaction, KnockbackSlidesOverTimeAlongHitDirection)
{
    HitReactionSettings s;
    s.stunSeconds       = 0.0f;
    s.knockbackDistance = 4.0f;
    s.knockbackSeconds  = 0.40f;
    s.horizontalOnly    = true;
    HitReaction r{ s };
    r.apply(Vector3f{ 2.0f, 9.0f, 0.0f });
    Vector3f total{ 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < 8; ++i)
        total += r.tick(0.05f);
    EXPECT_NEAR(total.x, 4.0f, 1.0e-3f);
    EXPECT_NEAR(total.y, 0.0f, 1.0e-4f);
    EXPECT_NEAR(total.z, 0.0f, 1.0e-4f);
    EXPECT_FALSE(r.knockingBack());
}

TEST(HitReaction, ZeroKnockbackOnlyStuns)
{
    HitReactionSettings s;
    s.stunSeconds       = 0.20f;
    s.knockbackDistance = 0.0f;
    HitReaction r{ s };
    r.apply(Vector3f{ 1.0f, 0.0f, 0.0f });
    EXPECT_TRUE(r.stunned());
    const Vector3f d = r.tick(0.05f);
    EXPECT_NEAR(d.Magnitude(), 0.0f, 1.0e-5f);
}

TEST(HitReaction, NewHitReplacesKnockbackAndRefreshesStun)
{
    HitReactionSettings s;
    s.stunSeconds       = 0.50f;
    s.knockbackDistance = 3.0f;
    s.knockbackSeconds  = 0.0f;
    HitReaction r{ s };
    r.apply(Vector3f{ 1.0f, 0.0f, 0.0f });
    r.tick(0.20f);
    EXPECT_NEAR(r.stunRemaining(), 0.30f, 1.0e-3f);
    r.apply(Vector3f{ 0.0f, 0.0f, 1.0f });
    EXPECT_NEAR(r.stunRemaining(), 0.50f, 1.0e-4f);
    const Vector3f d = r.tick(1.0f);
    EXPECT_NEAR(d.x, 0.0f, 1.0e-4f);
    EXPECT_NEAR(d.z, 3.0f, 1.0e-4f);
}

TEST(HitReaction, ResetClearsStunAndPush)
{
    HitReaction r;
    r.apply(Vector3f{ 1.0f, 0.0f, 0.0f });
    r.reset();
    EXPECT_FALSE(r.stunned());
    EXPECT_FALSE(r.knockingBack());
    const Vector3f d = r.tick(0.1f);
    EXPECT_NEAR(d.Magnitude(), 0.0f, 1.0e-5f);
}
