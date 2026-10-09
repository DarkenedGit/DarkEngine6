#pragma once

#include "Terrain/GrassTypes.h"

#include <cstdint>

namespace Dark::Terrain
{
    float perlin2(float x, float z, uint32_t seed);

    struct GrassWindSample
    {
        float dirX     = 0.0f;
        float dirZ     = 1.0f;
        float strength = 1.0f;
        float yaw      = 0.0f;
    };

    GrassWindSample sampleGrassWind(float tileCenterX, float tileCenterZ, double playTimeSec, const GrassParams& params);

} // namespace Dark::Terrain
