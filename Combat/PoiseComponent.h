#pragma once

#include "Math/MathHelper.h"

namespace Dark::Combat
{

    struct PoiseComponent
    {
        static constexpr const char* kTypeName = "Poise";

        float maxPoise       = 100.0f;
        float poise          = 100.0f;
        float regenPerSec    = 25.0f;
        float regenDelay     = 1.5f;
        float sinceDamage    = 1.5f;
        bool  hyperArmor     = false; // skip poise/stagger while set

        void reset()
        {
            poise       = maxPoise;
            sinceDamage = regenDelay;
        }

        // Returns true if this hit broke poise (crossed <= 0).
        bool applyPoiseDamage(float amount)
        {
            if (amount <= 0.0f || hyperArmor)
                return false;
            const bool wasUp = poise > 0.0f;
            poise -= amount;
            sinceDamage = 0.0f;
            if (poise > 0.0f)
                return false;
            poise = 0.0f;
            return wasUp;
        }

        void tick(float dt)
        {
            if (dt < 0.0f)
                dt = 0.0f;
            sinceDamage += dt;
            if (poise >= maxPoise || sinceDamage < regenDelay)
                return;
            poise += regenPerSec * dt;
            if (poise > maxPoise)
                poise = maxPoise;
        }
    };

} // namespace Dark::Combat
