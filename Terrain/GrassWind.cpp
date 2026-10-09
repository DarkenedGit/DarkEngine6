#include "Terrain/GrassWind.h"

#include "Math/MathHelper.h"
#include "Terrain/TerrainGen.h"

#include <cmath>
#include <cstdint>

namespace Dark::Terrain
{
    namespace
    {
        // Diagonals are not unit length until scaled; cardinals already are.
        constexpr float kInvSqrt2 = 0.7071067811865476f;
        constexpr float kGradX[8] = { kInvSqrt2, -kInvSqrt2, kInvSqrt2, -kInvSqrt2, 1.0f, -1.0f, 0.0f, 0.0f };
        constexpr float kGradZ[8] = { kInvSqrt2, kInvSqrt2, -kInvSqrt2, -kInvSqrt2, 0.0f, 0.0f, 1.0f, -1.0f };

        float fade(float t)
        {
            return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
        }

        float cornerDot(int ix, int iz, float dx, float dz, uint32_t seed)
        {
            const uint32_t index = static_cast<uint32_t>(hash21(ix, iz, seed) * 16777216.0f) & 7u;
            return kGradX[index] * dx + kGradZ[index] * dz;
        }
    } // namespace

    float perlin2(float x, float z, uint32_t seed)
    {
        const int   ix = static_cast<int>(std::floor(x));
        const int   iz = static_cast<int>(std::floor(z));
        const float fx = x - static_cast<float>(ix);
        const float fz = z - static_cast<float>(iz);
        const float u  = fade(fx);
        const float v  = fade(fz);

        const float n00 = cornerDot(ix, iz, fx, fz, seed);
        const float n10 = cornerDot(ix + 1, iz, fx - 1.0f, fz, seed);
        const float n01 = cornerDot(ix, iz + 1, fx, fz - 1.0f, seed);
        const float n11 = cornerDot(ix + 1, iz + 1, fx - 1.0f, fz - 1.0f, seed);

        const float nx0 = n00 + (n10 - n00) * u;
        const float nx1 = n01 + (n11 - n01) * u;
        const float n   = nx0 + (nx1 - nx0) * v;
        return Math::Clamp(n, -1.0f, 1.0f);
    }

    GrassWindSample sampleGrassWind(float tileCenterX, float tileCenterZ, double playTimeSec, const GrassParams& params)
    {
        GrassParams clamped = params;
        clampGrassParams(clamped);

        // A zero frequency must not touch time, including a non-finite play time.
        float scroll = 0.0f;
        if (clamped.windTemporalFreq != 0.0f)
            scroll = static_cast<float>(playTimeSec * static_cast<double>(clamped.windTemporalFreq));

        const float sx = tileCenterX * clamped.windSpatialFreq + std::cos(clamped.windBaseYaw) * scroll;
        const float sz = tileCenterZ * clamped.windSpatialFreq + std::sin(clamped.windBaseYaw) * scroll;
        const float n  = perlin2(sx, sz, clamped.seed);
        const float n2 = perlin2(sx + 19.2f, sz + 7.7f, clamped.seed ^ 0xB5297A4Du);

        GrassWindSample sample;
        sample.yaw      = clamped.windBaseYaw + Math::Clamp(n, -1.0f, 1.0f) * clamped.windDeflection;
        const float w   = Math::Clamp(n2, -1.0f, 1.0f) * 0.5f + 0.5f;
        sample.strength = Math::Lerp(0.45f, 1.0f, w);
        sample.dirX     = std::sin(sample.yaw);
        sample.dirZ     = std::cos(sample.yaw);
        return sample;
    }

} // namespace Dark::Terrain
