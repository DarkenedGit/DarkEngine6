#pragma once

#include "Math/Vector2f.h"
#include "Math/Vector3f.h"

#include <cmath>

namespace Dark
{

    inline Math::Vector2f octWrap(const Math::Vector2f& v)
    {
        const float sx = v.x >= 0.0f ? 1.0f : -1.0f;
        const float sy = v.y >= 0.0f ? 1.0f : -1.0f;
        return Math::Vector2f((1.0f - std::fabs(v.y)) * sx, (1.0f - std::fabs(v.x)) * sy);
    }

    inline Math::Vector2f encodeOct(Math::Vector3f n)
    {
        const float sum = std::fabs(n.x) + std::fabs(n.y) + std::fabs(n.z);
        n *= 1.0f / (sum > 1.0e-6f ? sum : 1.0e-6f);
        if (n.z < 0.0f)
        {
            const Math::Vector2f w = octWrap(Math::Vector2f(n.x, n.y));
            n.x                    = w.x;
            n.y                    = w.y;
        }
        return Math::Vector2f(n.x * 0.5f + 0.5f, n.y * 0.5f + 0.5f);
    }

    inline Math::Vector3f decodeOct(Math::Vector2f f)
    {
        f.x = f.x * 2.0f - 1.0f;
        f.y = f.y * 2.0f - 1.0f;
        Math::Vector3f n(f.x, f.y, 1.0f - std::fabs(f.x) - std::fabs(f.y));
        if (n.z < 0.0f)
        {
            const Math::Vector2f w = octWrap(Math::Vector2f(n.x, n.y));
            n.x                    = w.x;
            n.y                    = w.y;
        }
        n.Normalize();
        return n;
    }

} // namespace Dark
