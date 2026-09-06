#pragma once

#include "Math/MathHelper.h"

namespace Dark
{

    struct LightingConstants;

    namespace Sky
    {
    class Environment;
    }

    namespace Terrain
    {
    class HeightMap;
    }

    // Twilight gate in [0,1]: peaks when the sun is on the horizon (dawn/dusk).
    inline float heightFogAmount(float sunElevationRadians)
    {
        return Math::SmoothStep(0.34f, 0.05f, fabsf(sunElevationRadians));
    }

    // Optical depth of density(y) = d0 * exp(-falloff * (y - baseY)) along a segment of length dist.
    inline float exponentialHeightOpticalDepth(float camY, float dist, float dirY, float density, float falloff, float baseY)
    {
        if (density <= 1.0e-8f || dist <= 0.0f)
            return 0.0f;
        const float b    = Math::Max(falloff, 1.0e-4f);
        const float base = density * expf(-b * (camY - baseY));
        const float a    = b * dirY;
        if (fabsf(a) > 1.0e-4f)
            return base * (1.0f - expf(-a * dist)) / a;
        return base * dist;
    }

    // Gaussian slab around waterLevel, gated by terrain vs water (1 = flooded valley).
    inline float valleyFogDensity(float y, float terrainY, float waterLevel, float slabHeight, float density)
    {
        if (density <= 1.0e-8f)
            return 0.0f;
        const float h     = Math::Max(slabHeight, 0.1f);
        const float dy    = (y - waterLevel) / h;
        const float yBand = expf(-dy * dy);
        const float wet   = Math::Clamp((waterLevel + h - terrainY) / h, 0.0f, 1.0f);
        return density * yBand * wet;
    }

    struct FogGpu
    {
        float fogColor[3]{ 0.55f, 0.62f, 0.72f };
        float fogDensity             = 0.0f;
        float heightFogDensity       = 0.0f;
        float heightFogFalloff       = 0.06f;
        float heightFogHeight        = 0.0f;
        float volumetricFogDensity   = 0.0f;
        float volumetricHeight       = 14.0f;
        float waterLevel             = 0.0f;
        float heightOriginX          = 0.0f;
        float heightOriginZ          = 0.0f;
        float heightCellSize         = 0.0f;
        float heightWorldSizeX       = 0.0f;
        float heightWorldSizeZ       = 0.0f;
    };

    FogGpu makeFogGpu(const Sky::Environment* env, float waterLevel, bool lighting);
    void   fillFogHeightMap(FogGpu& fog, const Terrain::HeightMap* heightMap);
    void   applyFogToLighting(LightingConstants& lc, const FogGpu& fog);

} // namespace Dark
