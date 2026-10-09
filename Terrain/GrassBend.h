#pragma once

#include <algorithm>
#include <cmath>

namespace Dark::Terrain
{
    struct GrassTipIn
    {
        float height = 0.55f; // metres, already mean * per-blade mul * fade
        float flex   = 0.65f; // 0 rigid, 1 full yield, already mean * per-blade mul
        float windX  = 0.0f;  // metres at flex 1, already phase-scaled on LOD 0-2
        float windZ  = 0.0f;
        float shoveX = 0.0f; // player metres at flex 1; LOD 2 and 3 pass 0
        float shoveZ = 0.0f;
    };

    struct GrassTip
    {
        float x = 0.0f;
        float z = 0.0f;
    };

    struct GrassLocal
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // LOD 2 and 3 zero shove before this call. The curve does not read shove.
    inline GrassTip grassTipOffset(const GrassTipIn& in)
    {
        GrassTip tip{};
        tip.x              = (in.windX + in.shoveX) * in.flex;
        tip.z              = (in.windZ + in.shoveZ) * in.flex;
        const float maxLen = 0.85f * in.height;
        const float len2   = tip.x * tip.x + tip.z * tip.z;
        if (maxLen > 0.0f && len2 > maxLen * maxLen)
        {
            const float s = maxLen / std::sqrt(len2);
            tip.x *= s;
            tip.z *= s;
        }
        return tip;
    }

    // lod is 0..3. t is in [0, 1]. Height is the curve length input, already combined.
    // Mesh half-width is not an input. There is no root lift here.
    inline GrassLocal grassBladeLocal(int lod, float t, const GrassTip& tip, float height)
    {
        const float tipLen2 = tip.x * tip.x + tip.z * tip.z;
        const float yFloor  = 0.20f * height;
        const float under   = height * height - tipLen2;
        const float yTip    = std::sqrt(std::max(under, yFloor * yFloor));

        const float u  = 1.0f - t;
        const float b0 = u * u;
        const float b1 = 2.0f * u * t;
        const float b2 = t * t;

        GrassLocal local{};
        if (lod <= 1)
        {
            const float p0x = 0.0f;
            const float p0y = 0.0f;
            const float p0z = 0.0f;
            const float p1x = tip.x * 0.25f;
            const float p1y = height * 0.55f;
            const float p1z = tip.z * 0.25f;
            local.x         = b0 * p0x + b1 * p1x + b2 * tip.x;
            local.y         = b0 * p0y + b1 * p1y + b2 * yTip;
            local.z         = b0 * p0z + b1 * p1z + b2 * tip.z;
            return local;
        }

        local.x = tip.x * t;
        local.y = yTip * t;
        local.z = tip.z * t;
        return local;
    }

} // namespace Dark::Terrain
