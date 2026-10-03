#pragma once

#include "Character/SkillLimits.h"

namespace Dark
{

    struct SenseQuery
    {
        float range   = 25.0f;
        float coneDeg = 70.0f;
    };

    inline SenseQuery scaleNpcSight(SenseQuery base, float preySightRangeScale, float seeRangeScale, float seeConeScale)
    {
        const float prey = preySightRangeScale > 0.0f ? preySightRangeScale : 1.0f;
        SenseQuery out = base;
        out.range   = base.range * prey * clampSkill(seeRangeScale, kSeeRangeScaleMin, kSeeRangeScaleMax);
        out.coneDeg = base.coneDeg * clampSkill(seeConeScale, kSeeConeScaleMin, kSeeConeScaleMax);
        if (out.coneDeg > kNpcConeDegCap)
            out.coneDeg = kNpcConeDegCap;
        if (out.coneDeg < 1.0f)
            out.coneDeg = 1.0f;
        return out;
    }

    inline float scaleHearRange(float preyHearRange, float hearScale)
    {
        if (preyHearRange <= 0.0f)
            return 0.0f;
        return preyHearRange * clampSkill(hearScale, kHearScaleMin, kHearScaleMax);
    }

    // hearScale is the skill scalar (1 .. 1.30), not an X3DAudio curve value.
    inline float spatialDistanceScaler(float maxDistance, float hearScale)
    {
        const float base = maxDistance > 1.0f ? maxDistance : 1.0f;
        return base * clampSkill(hearScale, kHearScaleMin, kHearScaleMax);
    }

    inline void scaleFlashlight(float baseRange, float baseOuterDeg, float seeRangeScale, float seeConeScale, float& outRange, float& outOuter)
    {
        outRange = baseRange * clampSkill(seeRangeScale, kSeeRangeScaleMin, kSeeRangeScaleMax);
        outOuter = baseOuterDeg * clampSkill(seeConeScale, kSeeConeScaleMin, kSeeConeScaleMax);
        if (outOuter > kFlashlightOuterCap)
            outOuter = kFlashlightOuterCap;
    }

} // namespace Dark
