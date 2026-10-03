#include <gtest/gtest.h>

#include "Audio/AudioSystem.h"
#include "Character/SkillCatalog.h"
#include "Character/SkillSense.h"

#include <cmath>
#include <limits>

using namespace Dark;

TEST(SkillSense, CrouchScalesChaseOnceAndNotAllyLos)
{
    const float seeRange = skillScalar(SkillId::See, SkillScalar::SeeRangeScale, 10);
    const float seeCone  = skillScalar(SkillId::See, SkillScalar::SeeConeScale, 10);
    EXPECT_NEAR(seeRange, 1.25f, 1e-5f);
    EXPECT_NEAR(seeCone, 1.15f, 1e-5f);

    SenseQuery base;
    base.range   = 25.0f;
    base.coneDeg = 70.0f;

    const SenseQuery level1Chase = scaleNpcSight(base, 0.55f, 1.0f, 1.0f);
    const SenseQuery level1Ally  = scaleNpcSight(base, 1.0f, 1.0f, 1.0f);
    EXPECT_FLOAT_EQ(level1Chase.range, 25.0f * 0.55f);
    EXPECT_FLOAT_EQ(level1Ally.range, 25.0f);
    EXPECT_FLOAT_EQ(level1Ally.coneDeg, 70.0f);
    EXPECT_FLOAT_EQ(level1Chase.coneDeg, 70.0f);

    const SenseQuery chase = scaleNpcSight(base, 0.55f, seeRange, seeCone);
    const SenseQuery ally  = scaleNpcSight(base, 1.0f, seeRange, seeCone);
    EXPECT_NEAR(chase.range, 25.0f * 0.55f * seeRange, 1e-4f);
    EXPECT_NEAR(ally.range, 25.0f * seeRange, 1e-4f);
    EXPECT_GT(std::fabs(chase.range - (25.0f * 0.55f * 0.55f * seeRange)), 1.0f);
    EXPECT_NEAR(chase.coneDeg, 70.0f * seeCone, 1e-4f);
    EXPECT_FLOAT_EQ(ally.coneDeg, chase.coneDeg);

    const SenseQuery standing = scaleNpcSight(base, 1.0f, seeRange, seeCone);
    EXPECT_NEAR(standing.range, 31.25f, 1e-3f);
    EXPECT_NEAR(standing.coneDeg, 80.5f, 1e-3f);

    const SenseQuery noPrey = scaleNpcSight(base, 0.0f, seeRange, seeCone);
    EXPECT_FLOAT_EQ(noPrey.range, ally.range);
    const SenseQuery negativePrey = scaleNpcSight(base, -0.55f, seeRange, seeCone);
    EXPECT_FLOAT_EQ(negativePrey.range, ally.range);
}

TEST(SkillSense, NpcConeCapsAt120AndFloorAt1)
{
    SenseQuery wide;
    wide.range   = 25.0f;
    wide.coneDeg = 200.0f;
    const SenseQuery capped = scaleNpcSight(wide, 1.0f, kSeeRangeScaleMax, kSeeConeScaleMax);
    EXPECT_FLOAT_EQ(capped.coneDeg, kNpcConeDegCap);
    EXPECT_NEAR(capped.range, 31.25f, 1e-3f);

    SenseQuery narrow;
    narrow.range   = 25.0f;
    narrow.coneDeg = 0.2f;
    const SenseQuery raised = scaleNpcSight(narrow, 1.0f, 1.0f, 1.0f);
    EXPECT_FLOAT_EQ(raised.coneDeg, 1.0f);

    const SenseQuery nanScale = scaleNpcSight(
        SenseQuery{ 25.0f, 70.0f },
        1.0f,
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(nanScale.range, 25.0f);
    EXPECT_FLOAT_EQ(nanScale.coneDeg, 70.0f);
}

TEST(SkillSense, HearSprintRadiusAndDistanceScaler)
{
    const float hear = skillScalar(SkillId::Hear, SkillScalar::HearScale, 10);
    EXPECT_FLOAT_EQ(hear, 1.30f);
    EXPECT_FLOAT_EQ(hear, kHearScaleMax);
    EXPECT_NE(hear, 83.2f);

    EXPECT_NEAR(scaleHearRange(24.0f, hear), 31.2f, 1e-3f);
    EXPECT_FLOAT_EQ(scaleHearRange(24.0f, hear), 24.0f * hear);
    EXPECT_FLOAT_EQ(scaleHearRange(0.0f, hear), 0.0f);
    EXPECT_FLOAT_EQ(scaleHearRange(-1.25f, hear), 0.0f);
    EXPECT_NEAR(scaleHearRange(1.25f, hear), 1.625f, 1e-4f);

    EXPECT_FLOAT_EQ(spatialDistanceScaler(64.0f, 1.30f), 64.0f * 1.30f);
    EXPECT_NEAR(spatialDistanceScaler(64.0f, 1.30f), 83.2f, 1e-3f);
    EXPECT_FLOAT_EQ(spatialDistanceScaler(64.0f, 83.2f), spatialDistanceScaler(64.0f, 1.30f));
    EXPECT_GT(std::fabs(spatialDistanceScaler(64.0f, 83.2f) - (64.0f * 83.2f)), 1.0f);
    EXPECT_FLOAT_EQ(spatialDistanceScaler(64.0f, 1.0f), 64.0f);
    EXPECT_FLOAT_EQ(spatialDistanceScaler(0.5f, 1.30f), 1.30f);
}

TEST(SkillSense, FlashlightReappliesFromBaseAndCapsOuter)
{
    const float seeRange = skillScalar(SkillId::See, SkillScalar::SeeRangeScale, 10);
    const float seeCone  = skillScalar(SkillId::See, SkillScalar::SeeConeScale, 10);
    float range = 0.0f;
    float outer = 0.0f;
    scaleFlashlight(22.0f, 22.0f, 1.0f, 1.0f, range, outer);
    EXPECT_FLOAT_EQ(range, 22.0f);
    EXPECT_FLOAT_EQ(outer, 22.0f);

    scaleFlashlight(22.0f, 22.0f, seeRange, seeCone, range, outer);
    EXPECT_NEAR(range, 27.5f, 1e-3f);
    EXPECT_NEAR(outer, 25.3f, 1e-3f);
    EXPECT_FLOAT_EQ(range, 22.0f * seeRange);
    EXPECT_FLOAT_EQ(outer, 22.0f * seeCone);

    scaleFlashlight(22.0f, 40.0f, 1.0f, kSeeConeScaleMax, range, outer);
    EXPECT_FLOAT_EQ(range, 22.0f);
    EXPECT_FLOAT_EQ(outer, kFlashlightOuterCap);
}

TEST(SkillSense, UnattributedAndOwnSourceDoNotTrainHear)
{
    Audio::AudioSystem audio;
    EXPECT_EQ(audio.liveForeignSpatialVoices(7u), 0);
    EXPECT_EQ(audio.liveForeignSpatialVoices(0u), 0);
}
