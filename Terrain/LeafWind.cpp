#include "Terrain/LeafWind.h"

#include "Math/MathHelper.h"
#include "Terrain/GrassWind.h"

#include <cmath>

namespace Dark::Terrain
{
    namespace
    {
        // Owned field. Grass never goes calm, and its parameters are not inputs here.
        constexpr uint32_t kSeed          = 0x1EAF5EEDu;
        constexpr float    kSpatialFreq   = 0.0035f;
        constexpr float    kTemporalFreq  = 0.02f;
        constexpr float    kBaseYaw       = 0.85f;
        constexpr float    kDeflection    = 1.05f;
        const float        kScrollCos     = std::cos(kBaseYaw);
        const float        kScrollSin     = std::sin(kBaseYaw);
    } // namespace

    float leafWindStrengthFromNoise(float noise)
    {
        const float n = Math::Clamp(noise, -1.0f, 1.0f);
        return n * 0.5f + 0.5f;
    }

    LeafWindSample sampleLeafWind(float worldX, float worldZ, double simTimeSec)
    {
        const float scroll = static_cast<float>(simTimeSec * static_cast<double>(kTemporalFreq));
        const float sx     = worldX * kSpatialFreq + kScrollCos * scroll;
        const float sz     = worldZ * kSpatialFreq + kScrollSin * scroll;
        const float n      = perlin2(sx, sz, kSeed);

        LeafWindSample sample;
        sample.yaw      = kBaseYaw + n * kDeflection;
        sample.strength = leafWindStrengthFromNoise(n);
        sample.dirX     = std::sin(sample.yaw);
        sample.dirZ     = std::cos(sample.yaw);
        return sample;
    }

} // namespace Dark::Terrain
