#pragma once

namespace Dark::Terrain
{
    struct LeafWindSample
    {
        float dirX     = 0.0f;
        float dirZ     = 1.0f;
        float strength = 0.0f;
        float yaw      = 0.0f;
    };

    // Maps noise in [-1, 1] onto strength in [0, 1]. The calm extreme is 0.
    float leafWindStrengthFromNoise(float noise);

    // Place is world XZ. Time is simulation seconds. Grass parameters are not an input.
    LeafWindSample sampleLeafWind(float worldX, float worldZ, double simTimeSec);

} // namespace Dark::Terrain
