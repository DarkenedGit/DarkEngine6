#pragma once

#include "MathHelper.h"
#include <cmath>
#include <cstdint>

namespace Dark::Color
{
    enum class ColorSpace : uint8_t
    {
        Unknown = 0,
        sRGB,
        Linear
    };

    enum class TextureUsage : uint8_t
    {
        Albedo = 0,
        Emissive,
        Normal,
        Orm,
        Data,   // splat weights, masks, generic UNORM
        Height, // R32F
        Hud,    // display-referred UI / splash / 2D
        Font,
    };

    // IEC 61966-2-1. c is a single channel; clamp to [0,1].
    inline float srgbToLinear(float c)
    {
        c = Math::Clamp(c, 0.0f, 1.0f);
        return (c <= 0.04045f) ? (c / 12.92f) : powf((c + 0.055f) / 1.055f, 2.4f);
    }

    inline float linearToSrgb(float c)
    {
        c = Math::Clamp(c, 0.0f, 1.0f);
        return (c <= 0.0031308f) ? (12.92f * c) : (1.055f * powf(c, 1.0f / 2.4f) - 0.055f);
    }

    inline float srgb8ToLinear(uint8_t u)
    {
        return srgbToLinear(static_cast<float>(u) / 255.0f);
    }

    inline uint8_t linearToSrgb8(float c)
    {
        const float s = linearToSrgb(c);
        return static_cast<uint8_t>(s * 255.0f + 0.5f);
    }

    inline void srgbToLinear3(const float srgb[3], float linear[3])
    {
        linear[0] = srgbToLinear(srgb[0]);
        linear[1] = srgbToLinear(srgb[1]);
        linear[2] = srgbToLinear(srgb[2]);
    }

    inline void linearToSrgb3(const float linear[3], float srgb[3])
    {
        srgb[0] = linearToSrgb(linear[0]);
        srgb[1] = linearToSrgb(linear[1]);
        srgb[2] = linearToSrgb(linear[2]);
    }

    inline void srgb8ToLinear3(uint8_t r, uint8_t g, uint8_t b, float linear[3])
    {
        linear[0] = srgb8ToLinear(r);
        linear[1] = srgb8ToLinear(g);
        linear[2] = srgb8ToLinear(b);
    }

    inline ColorSpace inferColorSpaceForUsage(TextureUsage usage)
    {
        switch (usage)
        {
        case TextureUsage::Albedo:
        case TextureUsage::Emissive:
            return ColorSpace::sRGB;
        default:
            return ColorSpace::Linear;
        }
    }

    // GPU create: Linear usage always wins. Albedo/Emissive keep an explicit Linear Image tag (particle masks);
    // Unknown albedo defaults to sRGB.
    inline ColorSpace resolveGpuColorSpace(TextureUsage usage, ColorSpace imageSpace)
    {
        if (inferColorSpaceForUsage(usage) == ColorSpace::Linear)
            return ColorSpace::Linear;
        if (imageSpace == ColorSpace::Linear)
            return ColorSpace::Linear;
        return ColorSpace::sRGB;
    }
} // namespace Dark::Color
