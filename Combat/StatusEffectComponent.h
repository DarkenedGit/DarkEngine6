#pragma once

#include "Math/MathHelper.h"

#include <cstdint>

namespace Dark::Combat
{

    enum class CcCategory : uint8_t
    {
        Stun = 0,
        Root,
        Fear,
        Knockdown,
        Count
    };

    inline constexpr int   kCcCategoryCount     = static_cast<int>(CcCategory::Count);
    inline constexpr float kCcDrWindowSeconds   = 18.0f;
    inline constexpr float kHardCcMaxSeconds    = 3.0f;

    struct StatusInstance
    {
        uint8_t    id        = 0;
        float      magnitude = 0.f;
        float      remaining = 0.f;
        bool       hard      = false;
        CcCategory category  = CcCategory::Stun;
    };

    struct CcDrState
    {
        int   applications = 0;
        float resetAt      = 0.f;
    };

    struct StatusEffectComponent
    {
        static constexpr const char* kTypeName  = "StatusEffect";
        static constexpr int         kMaxStatus = 8;

        StatusInstance slots[kMaxStatus]{};
        int            count = 0;
        CcDrState      dr[kCcCategoryCount]{};
        float          now   = 0.f;

        void reset()
        {
            count = 0;
            now   = 0.f;
            for (int i = 0; i < kMaxStatus; ++i)
                slots[i] = StatusInstance{};
            for (int i = 0; i < kCcCategoryCount; ++i)
                dr[i] = CcDrState{};
        }

        void tick(float dt)
        {
            if (dt < 0.0f)
                dt = 0.0f;
            now += dt;
            for (int i = 0; i < kCcCategoryCount; ++i)
            {
                if (dr[i].applications > 0 && now >= dr[i].resetAt)
                {
                    dr[i].applications = 0;
                    dr[i].resetAt      = 0.f;
                }
            }
            for (int i = 0; i < count;)
            {
                slots[i].remaining -= dt;
                if (slots[i].remaining <= 0.0f)
                {
                    slots[i] = slots[count - 1];
                    --count;
                    continue;
                }
                ++i;
            }
        }

        // 1st = 100%, 2nd = 50%, 3rd+ = immune within the DR window.
        float applyCc(CcCategory cat, float duration, bool hard, uint8_t statusId, float magnitude)
        {
            if (duration <= 0.0f)
                return 0.0f;
            const int idx = static_cast<int>(cat);
            if (idx < 0 || idx >= kCcCategoryCount)
                return 0.0f;

            if (dr[idx].applications == 0)
                dr[idx].resetAt = now + kCcDrWindowSeconds;

            const int n = dr[idx].applications;
            float mul = 1.0f;
            if (n == 1)
                mul = 0.5f;
            else if (n >= 2)
                mul = 0.0f;

            ++dr[idx].applications;
            dr[idx].resetAt = now + kCcDrWindowSeconds;

            float eff = duration * mul;
            if (hard)
                eff = Math::Min(eff, kHardCcMaxSeconds);
            if (eff <= 0.0f)
                return 0.0f;

            for (int i = 0; i < count; ++i)
            {
                if (slots[i].category == cat)
                {
                    if (eff > slots[i].remaining)
                    {
                        slots[i].remaining = eff;
                        slots[i].magnitude = magnitude;
                        slots[i].hard      = hard;
                        slots[i].id        = statusId;
                    }
                    return eff;
                }
            }

            if (count >= kMaxStatus)
                return eff;
            slots[count++] = StatusInstance{ statusId, magnitude, eff, hard, cat };
            return eff;
        }

        bool hasHardCc() const
        {
            for (int i = 0; i < count; ++i)
            {
                if (slots[i].hard && slots[i].remaining > 0.0f)
                    return true;
            }
            return false;
        }

        bool hasCategory(CcCategory cat) const
        {
            for (int i = 0; i < count; ++i)
            {
                if (slots[i].category == cat && slots[i].remaining > 0.0f)
                    return true;
            }
            return false;
        }

        bool knockedDown() const { return hasCategory(CcCategory::Knockdown); }
    };

} // namespace Dark::Combat
