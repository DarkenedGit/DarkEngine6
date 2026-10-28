#pragma once

#include "Terrain/GrassTypes.h"
#include "Terrain/WindTypes.h"

namespace Dark::Terrain
{
    struct WindClock
    {
        double time     = 0.0;
        double prevTime = 0.0;

        void tick(float dt)
        {
            prevTime = time;
            time += dt;
        }
    };

    GrassWindSample sampleWind(float x, float z, double t, const WindSettings& settings);

    float gustAt(double t, const WindSettings& settings);

    void applyWindToGrass(const WindSettings& wind, GrassParams& params);

} // namespace Dark::Terrain
