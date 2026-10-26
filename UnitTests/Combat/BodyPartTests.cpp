#include <gtest/gtest.h>

#include "Combat/HitVolumes.h"

using namespace Dark;
using namespace Dark::Combat;

namespace
{

    void expectSlot(const HitVolumesComponent& volumes, int slot, float timer, float weight)
    {
        EXPECT_FLOAT_EQ(volumes.disableTimer[slot], timer);
        EXPECT_FLOAT_EQ(volumes.impactWeight[slot], weight);
    }

    void expectOtherSlotsZero(const HitVolumesComponent& volumes, int slot)
    {
        for (int i = 0; i < HitVolumesComponent::kSlotCount; ++i)
        {
            if (i == slot)
                continue;
            expectSlot(volumes, i, 0.0f, 0.0f);
        }
    }

} // namespace

TEST(BodyPart, LandedHeadSetsTimerAndWeight)
{
    HitVolumesComponent volumes;
    noteLandedPartHit(volumes, HitPart::Head);

    const int head = hitPartIndex(HitPart::Head);
    expectSlot(volumes, head, 3.0f, 1.0f);
    expectOtherSlotsZero(volumes, head);
}

TEST(BodyPart, RefreshDoesNotAdd)
{
    HitVolumesComponent volumes;
    const int head = hitPartIndex(HitPart::Head);
    volumes.disableTimer[head] = 1.0f;
    noteLandedPartHit(volumes, HitPart::Head);

    expectSlot(volumes, head, 3.0f, 1.0f);
    expectOtherSlotsZero(volumes, head);
}

TEST(BodyPart, NonePartIsIgnored)
{
    HitVolumesComponent volumes;
    const int chest = hitPartIndex(HitPart::Chest);
    volumes.disableTimer[chest] = 1.5f;
    noteLandedPartHit(volumes, HitPart::None);

    expectSlot(volumes, chest, 1.5f, 0.0f);
    expectOtherSlotsZero(volumes, chest);
}

TEST(BodyPart, RightArmBlocksFireAndReleaseNotJump)
{
    HitVolumesComponent volumes;
    const int arm = hitPartIndex(HitPart::RightArm);
    volumes.disableTimer[arm] = 0.01f;

    EXPECT_TRUE(hitVolumesBlockFire(&volumes));
    EXPECT_TRUE(hitVolumesBlockHunterRelease(&volumes));
    EXPECT_FALSE(hitVolumesBlockJump(&volumes));

    volumes.disableTimer[arm] = 0.0f;
    EXPECT_FALSE(hitVolumesBlockFire(&volumes));
    EXPECT_FALSE(hitVolumesBlockHunterRelease(&volumes));
    EXPECT_FALSE(hitVolumesBlockShield(&volumes));
    EXPECT_FALSE(hitVolumesBlockHunterBlock(&volumes));
    EXPECT_FALSE(hitVolumesBlockJump(&volumes));

    EXPECT_FALSE(hitVolumesBlockFire(nullptr));
    EXPECT_FALSE(hitVolumesBlockHunterRelease(nullptr));
    EXPECT_FALSE(hitVolumesBlockShield(nullptr));
    EXPECT_FALSE(hitVolumesBlockHunterBlock(nullptr));
    EXPECT_FALSE(hitVolumesBlockJump(nullptr));
}

TEST(BodyPart, LeftArmBlocksShieldAndHunterBlock)
{
    HitVolumesComponent volumes;
    const int left = hitPartIndex(HitPart::LeftArm);
    const int right = hitPartIndex(HitPart::RightArm);
    volumes.disableTimer[left] = 0.01f;
    volumes.disableTimer[right] = 0.0f;

    EXPECT_TRUE(hitVolumesBlockShield(&volumes));
    EXPECT_TRUE(hitVolumesBlockHunterBlock(&volumes));
    EXPECT_FALSE(hitVolumesBlockFire(&volumes));

    EXPECT_FALSE(hitVolumesBlockShield(nullptr));
    EXPECT_FALSE(hitVolumesBlockHunterBlock(nullptr));
    EXPECT_FALSE(hitVolumesBlockFire(nullptr));
    EXPECT_FALSE(hitVolumesBlockHunterRelease(nullptr));
    EXPECT_FALSE(hitVolumesBlockJump(nullptr));
}

TEST(BodyPart, SpeedOneLeg)
{
    HitVolumesComponent volumes;
    const int right = hitPartIndex(HitPart::RightLeg);
    const int left = hitPartIndex(HitPart::LeftLeg);
    const int waist = hitPartIndex(HitPart::Waist);

    volumes.disableTimer[right] = 1.0f;
    EXPECT_FLOAT_EQ(hitVolumesSpeedScale(&volumes, 1.0f), 0.65f);

    volumes.disableTimer[right] = 0.0f;
    volumes.disableTimer[left] = 1.0f;
    EXPECT_FLOAT_EQ(hitVolumesSpeedScale(&volumes, 1.0f), 0.65f);

    volumes.disableTimer[right] = 1.0f;
    EXPECT_FLOAT_EQ(hitVolumesSpeedScale(&volumes, 1.0f), 0.65f * 0.65f);

    volumes.disableTimer[waist] = 1.0f;
    EXPECT_FLOAT_EQ(hitVolumesSpeedScale(&volumes, 1.0f), 0.65f * 0.65f * 0.85f);
}

TEST(BodyPart, SpeedExistingScale)
{
    HitVolumesComponent volumes;
    volumes.disableTimer[hitPartIndex(HitPart::RightLeg)] = 1.0f;
    EXPECT_NEAR(hitVolumesSpeedScale(&volumes, 0.5f), 0.325f, 1.0e-5f);

    volumes.disableTimer[hitPartIndex(HitPart::RightLeg)] = 0.0f;
    EXPECT_FLOAT_EQ(hitVolumesSpeedScale(&volumes, 2.0f), 1.0f);
    EXPECT_FLOAT_EQ(hitVolumesSpeedScale(nullptr, 0.5f), 1.0f);
}

TEST(BodyPart, HeadAndChestDoNotGate)
{
    HitVolumesComponent volumes;
    volumes.disableTimer[hitPartIndex(HitPart::Head)] = 3.0f;
    volumes.disableTimer[hitPartIndex(HitPart::Chest)] = 3.0f;

    EXPECT_FLOAT_EQ(hitVolumesSpeedScale(&volumes, 0.5f), 0.5f);
    EXPECT_FALSE(hitVolumesBlockFire(&volumes));
    EXPECT_FALSE(hitVolumesBlockHunterRelease(&volumes));
    EXPECT_FALSE(hitVolumesBlockShield(&volumes));
    EXPECT_FALSE(hitVolumesBlockHunterBlock(&volumes));
    EXPECT_FALSE(hitVolumesBlockJump(&volumes));
}

TEST(BodyPart, TickDecaysWeightAndTimer)
{
    HitVolumesComponent volumes;
    for (int i = 0; i < HitVolumesComponent::kSlotCount; ++i)
    {
        volumes.disableTimer[i] = 3.0f;
        volumes.impactWeight[i] = 1.0f;
    }

    tickHitVolumes(volumes, 0.1f);
    for (int i = 0; i < HitVolumesComponent::kSlotCount; ++i)
    {
        EXPECT_NEAR(volumes.impactWeight[i], 0.65f, 1.0e-5f);
        EXPECT_NEAR(volumes.disableTimer[i], 2.9f, 1.0e-5f);
    }

    tickHitVolumes(volumes, 1.0f);
    for (int i = 0; i < HitVolumesComponent::kSlotCount; ++i)
    {
        EXPECT_FLOAT_EQ(volumes.impactWeight[i], 0.0f);
        EXPECT_NEAR(volumes.disableTimer[i], 1.9f, 1.0e-5f);
    }

    const float timers[HitVolumesComponent::kSlotCount] = {
        volumes.disableTimer[0], volumes.disableTimer[1], volumes.disableTimer[2], volumes.disableTimer[3],
        volumes.disableTimer[4], volumes.disableTimer[5], volumes.disableTimer[6],
    };
    const float weights[HitVolumesComponent::kSlotCount] = {
        volumes.impactWeight[0], volumes.impactWeight[1], volumes.impactWeight[2], volumes.impactWeight[3],
        volumes.impactWeight[4], volumes.impactWeight[5], volumes.impactWeight[6],
    };
    tickHitVolumes(volumes, -0.5f);
    for (int i = 0; i < HitVolumesComponent::kSlotCount; ++i)
    {
        EXPECT_FLOAT_EQ(volumes.disableTimer[i], timers[i]);
        EXPECT_FLOAT_EQ(volumes.impactWeight[i], weights[i]);
    }
}

TEST(BodyPart, ClearZerosState)
{
    HitVolumesComponent volumes;
    noteLandedPartHit(volumes, HitPart::Head);
    const int head = hitPartIndex(HitPart::Head);
    volumes.segment[head].radius = 0.25f;
    volumes.segment[head].active = true;

    clearHitPartState(volumes);

    for (int i = 0; i < HitVolumesComponent::kSlotCount; ++i)
        expectSlot(volumes, i, 0.0f, 0.0f);
    EXPECT_FLOAT_EQ(volumes.segment[head].radius, 0.25f);
    EXPECT_TRUE(volumes.segment[head].active);
}
