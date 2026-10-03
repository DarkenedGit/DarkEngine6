#pragma once

#include <cmath>

namespace Dark
{

    inline constexpr int   kSkillMaxLevel      = 10;
    inline constexpr float kSkillXpDtCap       = 0.10f;
    inline constexpr float kRunScaleMin        = 1.00f;
    inline constexpr float kRunScaleMax        = 1.20f;
    inline constexpr float kSwimScaleMin       = 1.00f;
    inline constexpr float kSwimScaleMax       = 1.25f;
    inline constexpr float kJumpScaleMin       = 1.00f;
    inline constexpr float kJumpScaleMax       = 1.15f;
    inline constexpr float kRecoilScaleMin     = 0.70f;
    inline constexpr float kRecoilScaleMax     = 1.00f;
    inline constexpr float kCooldownScaleMin   = 0.85f;
    inline constexpr float kCooldownScaleMax   = 1.00f;
    inline constexpr float kHearScaleMin       = 1.00f;
    inline constexpr float kHearScaleMax       = 1.30f;
    inline constexpr float kSeeRangeScaleMin   = 1.00f;
    inline constexpr float kSeeRangeScaleMax   = 1.25f;
    inline constexpr float kSeeConeScaleMin    = 1.00f;
    inline constexpr float kSeeConeScaleMax    = 1.15f;
    inline constexpr float kNpcConeDegCap      = 120.0f;
    inline constexpr float kFlashlightOuterCap = 44.0f;
    inline constexpr float kSkillIdentity      = 1.0f;
    inline constexpr float kShootGrantInterval = 0.125f; // Caps shoot grants at 8/s.

    // Non-finite input is identity, not the low end of an inverted range.
    inline float clampSkill(float v, float lo, float hi)
    {
        if (!std::isfinite(v))
            return kSkillIdentity;
        if (v < lo)
            return lo;
        if (v > hi)
            return hi;
        return v;
    }

} // namespace Dark
