#pragma once

namespace Dark
{

    // Eye-to-surface ease. The volume shader uses the same travel function.
    inline constexpr float kUnderwaterEaseMeters = 0.4f;

    // Distance the ray spends below a flat water plane. Eye at or above the plane is 0.
    // A hit on or below the plane uses the full segment. A hit above, including a far sky
    // point, uses only the segment from the eye to the plane.
    inline float underwaterTravel(float camY, float hitY, float waterY, float distance)
    {
        if (!(distance > 0.0f) || !(camY < waterY))
            return 0.0f;
        if (hitY <= waterY)
            return distance;
        const float denom = hitY - camY;
        if (!(denom > 1.0e-6f))
            return distance;
        const float t = (waterY - camY) / denom;
        if (!(t > 0.0f))
            return 0.0f;
        if (t >= 1.0f)
            return distance;
        return t * distance;
    }

    // 0 at the still level, 1 at kUnderwaterEaseMeters below it. Not swimming stays 0.
    inline float underwaterAmount(bool swimming, float eyeY, float waterY)
    {
        if (!swimming)
            return 0.0f;
        const float below = waterY - eyeY;
        if (!(below > 0.0f))
            return 0.0f;
        const float t = below / kUnderwaterEaseMeters;
        if (t >= 1.0f)
            return 1.0f;
        return t;
    }

} // namespace Dark
