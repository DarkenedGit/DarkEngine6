#pragma once

#include "Core/Log.h"
#include "Math/MathHelper.h"

#include <cmath>

namespace Dark
{

    inline constexpr float kCornerLeanSeconds    = 0.18f;
    inline constexpr float kCornerPeekPastMetres = 0.25f;
    inline constexpr float kCornerLeanSkinMetres = 0.12f;

    inline float cornerLeanTarget(bool leftDown, bool rightDown, bool allowed)
    {
        if (!allowed)
            return 0.0f;
        const float raw = (rightDown ? 1.0f : 0.0f) - (leftDown ? 1.0f : 0.0f);
        return Math::Clamp(raw, -1.0f, 1.0f);
    }

    inline float stepCornerLean(float current, float target, float dt)
    {
        if (!std::isfinite(current) || !std::isfinite(target) || !std::isfinite(dt))
        {
            DE_ASSERT(std::isfinite(current) && std::isfinite(target) && std::isfinite(dt));
            return 0.0f;
        }
        if (dt < 0.0f)
            dt = 0.0f;
        target = Math::Clamp(target, -1.0f, 1.0f);
        const float delta = target - current;
        const float step  = dt / kCornerLeanSeconds;
        if (std::fabs(delta) <= step)
            return target;
        return current + std::copysign(step, delta);
    }

    inline float leanedLateral(float boomLateral, float lean, float reach)
    {
        const float absLean = std::fabs(lean);
        if (absLean == 0.0f)
            return boomLateral;
        return boomLateral + (std::copysign(reach, lean) - boomLateral) * absLean;
    }

    inline float leanFractionForLateral(float boomLateral, float lateral, float lean, float reach)
    {
        if (lean == 0.0f)
            return 0.0f;
        const float span = std::copysign(reach, lean) - boomLateral;
        if (std::fabs(span) <= 1.0e-6f)
            return 0.0f;
        const float fraction = (lateral - boomLateral) / span;
        return std::copysign(Math::Clamp(std::fabs(fraction), 0.0f, 1.0f), lean);
    }

} // namespace Dark
