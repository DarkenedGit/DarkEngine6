#pragma once

#include "Terrain/GrassWind.h"
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
            time += static_cast<double>(dt);
        }
    };

    GrassWindSample sampleWind(float x, float z, double timeSec, const WindSettings& settings);

    float gustAt(double timeSec, const WindSettings& settings);

    // World-space AABB margin that encloses TreeWindOffset at this kind's clamps.
    float kindSwayMetres(const WindSettings& settings, int kindIndex);

    // Copies the shared field onto grass. Tip length changes only when grassFollowsStrength is set.
    void applyWindToGrass(const WindSettings& wind, GrassParams& params);

} // namespace Dark::Terrain
