#pragma once

#include "Math/Vector3f.h"

#include <cstddef>

// What render passes read of the sky and weather. The Sky module implements it (Sky::Environment),
// so Render code never includes Sky headers. CPU only.

namespace Dark
{

    // HLSL SkyEvalParams (SkyEval.hlsli): the analytic sky that SSR misses and water reflections
    // evaluate, with the distant cloud deck appended.
    struct SkyEvalParams
    {
        float sunDir[3];
        float coverage;
        float sunColor[3];
        float turbidity;
        float moonDir[3];
        float rain;
        float moonColor[3];
        float windSpeed;
        float windDir[2];
        float sunElevation;
        float exposure;
        float cloudTime;
        float _pad[3];
        float clLightColor[4];
        float clLightDir[4];
        float clSkyTop[4];
        float clSkyBottom[4];
        float clWind[4];
    };
    static_assert(sizeof(SkyEvalParams) == 44 * sizeof(float), "SkyEvalParams is 44 floats");
    static_assert(offsetof(SkyEvalParams, cloudTime) == 20 * sizeof(float), "cloudTime stays at float 20");
    static_assert(offsetof(SkyEvalParams, clLightColor) == 24 * sizeof(float), "cloud tail starts at float 24");
    static_assert(offsetof(SkyEvalParams, clWind) == offsetof(SkyEvalParams, clLightColor) + 16 * sizeof(float), "cloud tail is contiguous");

    // The light the scene is lit by this frame (the sun, or the moon at night) and the sky gradient.
    struct EnvironmentLighting
    {
        Math::Vector3f lightDir{ 0.35f, 0.85f, -0.35f }; // toward the light
        Math::Vector3f lightColor{ 1.0f, 0.96f, 0.88f };
        Math::Vector3f ambientColor{ 0.18f, 0.20f, 0.24f };
        Math::Vector3f skyZenith{ 0.22f, 0.40f, 0.62f };
        Math::Vector3f skyHorizon{ 0.62f, 0.72f, 0.82f };
        float          cloudCoverage = 0.0f; // 0 clear, 1 overcast
        float          rain          = 0.0f;
    };

    // Distance, height and valley fog densities after weather and art scales.
    struct EnvironmentFog
    {
        Math::Vector3f color{ 0.55f, 0.62f, 0.72f };
        float          density           = 0.0f;
        float          heightDensity     = 0.0f;
        float          heightFalloff     = 0.06f;
        float          volumetricDensity = 0.0f;
        float          volumetricHeight  = 14.0f;
    };

    class IEnvironmentProvider
    {
    public:
        virtual EnvironmentLighting lighting() const = 0;
        virtual EnvironmentFog      fog() const      = 0;
        // Overwrites every field.
        virtual void skyEval(SkyEvalParams& out) const = 0;

    protected:
        IEnvironmentProvider()                                       = default;
        IEnvironmentProvider(const IEnvironmentProvider&)            = default;
        IEnvironmentProvider& operator=(const IEnvironmentProvider&) = default;
        ~IEnvironmentProvider()                                      = default;
    };

} // namespace Dark
