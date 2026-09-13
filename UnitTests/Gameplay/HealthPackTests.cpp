#include <gtest/gtest.h>

#include "Character/Health.h"
#include "Gameplay/HealthPack.h"
#include "Math/Vector3f.h"

using Dark::Health;
using Dark::HealthPackSet;
using Dark::Math::Vector3f;
using Dark::healthPackWorldMatrix;

TEST(HealthPackSet, TryAddCapsAtMax)
{
    HealthPackSet packs;
    for (int i = 0; i < HealthPackSet::kMax; ++i)
        EXPECT_TRUE(packs.tryAdd(Vector3f{ static_cast<float>(i), 1.0f, 0.0f }));
    EXPECT_FALSE(packs.tryAdd(Vector3f{ 9.0f, 1.0f, 0.0f }));
    EXPECT_EQ(packs.count(), HealthPackSet::kMax);
    EXPECT_TRUE(packs[0].active);
}

TEST(HealthPackSet, PickupHealsAndRespawns)
{
    HealthPackSet packs;
    ASSERT_TRUE(packs.tryAdd(Vector3f{ 0.0f, 0.0f, 0.0f }));

    Health health;
    health.applyDamage(80.0f);
    EXPECT_NEAR(health.hp(), 20.0f, 1.0e-3f);

    EXPECT_EQ(packs.tryPickup(Vector3f{ 0.2f, 0.0f, 0.0f }, health), 1);
    EXPECT_FALSE(packs[0].active);
    EXPECT_NEAR(health.hp(), 70.0f, 1.0e-3f);
    EXPECT_EQ(packs.tryPickup(Vector3f{ 0.0f, 0.0f, 0.0f }, health), 0);

    packs.tick(HealthPackSet::kRespawn + 0.01f);
    EXPECT_TRUE(packs[0].active);
}

TEST(HealthPackSet, NoPickupAtFullHealthOrOutOfRange)
{
    HealthPackSet packs;
    ASSERT_TRUE(packs.tryAdd(Vector3f{ 0.0f, 0.0f, 0.0f }));

    Health full;
    EXPECT_EQ(packs.tryPickup(Vector3f{ 0.0f, 0.0f, 0.0f }, full), 0);
    EXPECT_TRUE(packs[0].active);

    Health hurt;
    hurt.applyDamage(40.0f);
    EXPECT_EQ(packs.tryPickup(Vector3f{ 10.0f, 0.0f, 0.0f }, hurt), 0);
    EXPECT_TRUE(packs[0].active);
}

TEST(HealthPack, WorldMatrixTranslates)
{
    const auto m = healthPackWorldMatrix(Vector3f{ 3.0f, 4.0f, 5.0f }, 0.0f, 0.0f);
    EXPECT_NEAR(m.m_afEntry[12], 3.0f, 1.0e-4f);
    EXPECT_NEAR(m.m_afEntry[13], 4.0f, 1.0e-4f);
    EXPECT_NEAR(m.m_afEntry[14], 5.0f, 1.0e-4f);
}
