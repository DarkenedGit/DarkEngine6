#pragma once

#include <cstdint>

namespace Dark::Terrain
{
    enum class WindKind : uint8_t
    {
        Tree = 0,
        Flower,
        Rock,
        GrassTuft,
        Count
    };

    struct TreeWindParams
    {
        float influence       = 1.0f;
        float stiffness       = 1.0f;
        float mainBendMetres  = 0.35f;
        float branchAmp       = 0.06f;
        float edgeAmp         = 0.03f;
        float flutterFreq     = 1.6f;
        float detailFadeStart = 25.0f;
        float detailFadeEnd   = 45.0f;
        float bendFadeStart   = 120.0f;
        float bendFadeEnd     = 180.0f;
        float maxSwayMetres   = 1.0f;
    };

    struct WindSettings
    {
        float          strength             = 1.0f;
        float          headingRad           = 1.37f;
        float          deflection           = 0.55f;
        float          spatialFreq          = 0.004f;
        float          temporalFreq         = 0.015f;
        float          gustStrength         = 0.35f;
        float          gustFreq             = 0.15f;
        bool           grassFollowsStrength = false;
        uint32_t       seed                 = 1337u;
        TreeWindParams kind[static_cast<int>(WindKind::Count)]{};
    };

    WindSettings defaultWindSettings();
    void         clampWindSettings(WindSettings& settings);

} // namespace Dark::Terrain
