#pragma once

#include <cstdint>

namespace Dark::Combat
{

    // None is not a slot. The seven parts are the later tie order: head, arms, legs, chest, waist.
    enum class HitPart : uint8_t
    {
        None = 0,
        Head,
        RightArm,
        LeftArm,
        RightLeg,
        LeftLeg,
        Chest,
        Waist,
    };

    inline constexpr int kHitPartCount = 7;

    constexpr int hitPartIndex(HitPart part)
    {
        const int index = static_cast<int>(part) - static_cast<int>(HitPart::Head);
        if (index < 0 || index >= kHitPartCount)
            return -1;
        return index;
    }

} // namespace Dark::Combat
