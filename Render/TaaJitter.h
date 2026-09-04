#pragma once

#include "Math/Matrix4f.h"

#include <cstdint>

namespace Dark
{

    // Radical inverse in base `base` for 1-based index (Halton).
    inline float halton(uint32_t index, uint32_t base)
    {
        if (base < 2)
            base = 2;
        float    f = 1.0f;
        float    r = 0.0f;
        uint32_t i = index;
        while (i > 0)
        {
            f /= static_cast<float>(base);
            r += f * static_cast<float>(i % base);
            i /= base;
        }
        return r;
    }

    // 8-sample Halton(2,3) jitter in [-0.5, 0.5] pixels.
    inline void taaHaltonJitter(uint32_t frameIndex, float& pixelX, float& pixelY)
    {
        const uint32_t i = (frameIndex % 8u) + 1u;
        pixelX           = halton(i, 2) - 0.5f;
        pixelY           = halton(i, 3) - 0.5f;
    }

    // Offset a row-major D3D LH projection so clip.xy/w shifts by pixelX/Y.
    inline void applyNdcJitter(Math::Matrix4f& proj, float pixelX, float pixelY, uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
            return;
        proj.m_afEntry[Math::Mat4f::m31] += 2.0f * pixelX / static_cast<float>(width);
        proj.m_afEntry[Math::Mat4f::m32] += 2.0f * pixelY / static_cast<float>(height);
    }

} // namespace Dark
