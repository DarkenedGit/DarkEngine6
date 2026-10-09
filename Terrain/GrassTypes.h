#pragma once

#include "Math/MathHelper.h"

#include <cmath>
#include <cstdint>

namespace Dark::Terrain
{
    struct GrassParams
    {
        bool     enabled          = false;
        float    heightMetres     = 0.55f;
        float    flexibility      = 0.65f;
        float    densityScale     = 1.0f;
        float    windBaseYaw      = 1.37f;
        float    windDeflection   = 0.55f;
        float    windTipMetres    = 0.40f;
        float    windSpatialFreq  = 0.004f;
        float    windTemporalFreq = 0.015f;
        uint32_t seed             = 1337u;
    };

    // -pi and +pi are one heading; keep +pi so yaw stays in (-pi, pi].
    inline void clampGrassParams(GrassParams& params)
    {
        params.heightMetres     = Math::Clamp(params.heightMetres, 0.05f, 1.50f);
        params.flexibility      = Math::Clamp(params.flexibility, 0.0f, 1.0f);
        params.densityScale     = Math::Clamp(params.densityScale, 0.0f, 1.5f);
        params.windDeflection   = Math::Clamp(params.windDeflection, 0.0f, 1.20f);
        params.windTipMetres    = Math::Clamp(params.windTipMetres, 0.0f, 1.50f);
        params.windSpatialFreq  = Math::Clamp(params.windSpatialFreq, 0.0005f, 0.02f);
        params.windTemporalFreq = Math::Clamp(params.windTemporalFreq, 0.0f, 0.10f);

        float yaw = std::fmod(params.windBaseYaw, Math::TwoPi);
        if (yaw <= -Math::Pi)
            yaw += Math::TwoPi;
        else if (yaw > Math::Pi)
            yaw -= Math::TwoPi;
        params.windBaseYaw = yaw;
    }

} // namespace Dark::Terrain
