#pragma once

#include "Math/MathHelper.h"

namespace Dark::Combat
{

    struct DefenseComponent
    {
        static constexpr const char* kTypeName = "Defense";

        bool  blocking          = false;
        bool  parrying          = false;
        float blockArcDeg       = 140.0f; // total frontal arc
        float blockStaminaCost  = 12.0f;
        float stamina           = 100.0f;
        float staminaMax        = 100.0f;
        float staminaRegenPerSec = 18.0f;
        float iframeSecondsLeft = 0.0f;
        float parryWindowLeft   = 0.0f; // seconds remaining of perfect-parry window
        float facingYawRad      = 0.0f; // optional; Transform used when present
        uint8_t team            = 0;
        bool  invulnerable      = false;

        void tick(float dt)
        {
            if (dt < 0.0f)
                dt = 0.0f;
            if (iframeSecondsLeft > 0.0f)
                iframeSecondsLeft = Math::Max(0.0f, iframeSecondsLeft - dt);
            if (parryWindowLeft > 0.0f)
                parryWindowLeft = Math::Max(0.0f, parryWindowLeft - dt);
            if (!blocking && stamina < staminaMax)
            {
                stamina += staminaRegenPerSec * dt;
                if (stamina > staminaMax)
                    stamina = staminaMax;
            }
        }

        void beginIFrame(float seconds)
        {
            if (seconds > iframeSecondsLeft)
                iframeSecondsLeft = seconds;
        }

        void beginParryWindow(float seconds)
        {
            parrying        = true;
            parryWindowLeft = seconds > 0.0f ? seconds : 0.0f;
        }

        bool inIFrame() const { return invulnerable || iframeSecondsLeft > 0.0f; }
        bool inParryWindow() const { return parrying && parryWindowLeft > 0.0f; }
    };

} // namespace Dark::Combat
