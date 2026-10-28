#include "Terrain/WindField.h"

#include "Math/MathHelper.h"
#include "Terrain/GrassWind.h"
#include "Terrain/TerrainGen.h"

#include <algorithm>
#include <cmath>

namespace Dark::Terrain
{
    WindSettings defaultWindSettings()
    {
        WindSettings s;
        s.strength             = 1.0f;
        s.headingRad           = 1.37f;
        s.deflection           = 0.55f;
        s.spatialFreq          = 0.004f;
        s.temporalFreq         = 0.015f;
        s.gustStrength         = 0.35f;
        s.gustFreq             = 0.15f;
        s.grassFollowsStrength = false;
        s.seed                 = 1337u;

        // Tree defaults
        s.kind[static_cast<int>(WindKind::Tree)].influence       = 1.0f;
        s.kind[static_cast<int>(WindKind::Tree)].stiffness       = 1.0f;
        s.kind[static_cast<int>(WindKind::Tree)].mainBendMetres  = 0.35f;
        s.kind[static_cast<int>(WindKind::Tree)].branchAmp       = 0.06f;
        s.kind[static_cast<int>(WindKind::Tree)].edgeAmp         = 0.03f;
        s.kind[static_cast<int>(WindKind::Tree)].flutterFreq     = 1.6f;
        s.kind[static_cast<int>(WindKind::Tree)].detailFadeStart = 25.0f;
        s.kind[static_cast<int>(WindKind::Tree)].detailFadeEnd   = 45.0f;
        s.kind[static_cast<int>(WindKind::Tree)].bendFadeStart   = 120.0f;
        s.kind[static_cast<int>(WindKind::Tree)].bendFadeEnd     = 180.0f;
        s.kind[static_cast<int>(WindKind::Tree)].maxSwayMetres   = 1.0f;

        // Flower: softer, smaller
        s.kind[static_cast<int>(WindKind::Flower)].influence       = 1.0f;
        s.kind[static_cast<int>(WindKind::Flower)].stiffness       = 0.6f;
        s.kind[static_cast<int>(WindKind::Flower)].mainBendMetres  = 0.05f;
        s.kind[static_cast<int>(WindKind::Flower)].branchAmp       = 0.06f;
        s.kind[static_cast<int>(WindKind::Flower)].edgeAmp         = 0.02f;
        s.kind[static_cast<int>(WindKind::Flower)].flutterFreq     = 1.6f;
        s.kind[static_cast<int>(WindKind::Flower)].detailFadeStart = 25.0f;
        s.kind[static_cast<int>(WindKind::Flower)].detailFadeEnd   = 45.0f;
        s.kind[static_cast<int>(WindKind::Flower)].bendFadeStart   = 120.0f;
        s.kind[static_cast<int>(WindKind::Flower)].bendFadeEnd     = 180.0f;
        s.kind[static_cast<int>(WindKind::Flower)].maxSwayMetres   = 1.0f;

        // Rock: no wind
        s.kind[static_cast<int>(WindKind::Rock)].influence       = 0.0f;
        s.kind[static_cast<int>(WindKind::Rock)].stiffness       = 1.0f;
        s.kind[static_cast<int>(WindKind::Rock)].mainBendMetres  = 0.0f;
        s.kind[static_cast<int>(WindKind::Rock)].branchAmp       = 0.0f;
        s.kind[static_cast<int>(WindKind::Rock)].edgeAmp         = 0.0f;
        s.kind[static_cast<int>(WindKind::Rock)].flutterFreq     = 0.0f;
        s.kind[static_cast<int>(WindKind::Rock)].detailFadeStart = 25.0f;
        s.kind[static_cast<int>(WindKind::Rock)].detailFadeEnd   = 45.0f;
        s.kind[static_cast<int>(WindKind::Rock)].bendFadeStart   = 120.0f;
        s.kind[static_cast<int>(WindKind::Rock)].bendFadeEnd     = 180.0f;
        s.kind[static_cast<int>(WindKind::Rock)].maxSwayMetres   = 0.0f;

        // GrassTuft: smaller motion
        s.kind[static_cast<int>(WindKind::GrassTuft)].influence       = 1.0f;
        s.kind[static_cast<int>(WindKind::GrassTuft)].stiffness       = 1.0f;
        s.kind[static_cast<int>(WindKind::GrassTuft)].mainBendMetres  = 0.08f;
        s.kind[static_cast<int>(WindKind::GrassTuft)].branchAmp       = 0.06f;
        s.kind[static_cast<int>(WindKind::GrassTuft)].edgeAmp         = 0.03f;
        s.kind[static_cast<int>(WindKind::GrassTuft)].flutterFreq     = 1.6f;
        s.kind[static_cast<int>(WindKind::GrassTuft)].detailFadeStart = 25.0f;
        s.kind[static_cast<int>(WindKind::GrassTuft)].detailFadeEnd   = 45.0f;
        s.kind[static_cast<int>(WindKind::GrassTuft)].bendFadeStart   = 120.0f;
        s.kind[static_cast<int>(WindKind::GrassTuft)].bendFadeEnd     = 180.0f;
        s.kind[static_cast<int>(WindKind::GrassTuft)].maxSwayMetres   = 1.0f;

        return s;
    }

    void clampWindSettings(WindSettings& s)
    {
        s.strength    = std::clamp(s.strength, 0.0f, 3.0f);
        s.deflection  = std::clamp(s.deflection, 0.0f, 1.20f);
        s.spatialFreq = std::clamp(s.spatialFreq, 0.0005f, 0.02f);
        s.temporalFreq = std::clamp(s.temporalFreq, 0.0f, 1.0f);
        s.gustStrength = std::clamp(s.gustStrength, 0.0f, 1.0f);
        s.gustFreq     = std::clamp(s.gustFreq, 0.0f, 2.0f);

        if (!std::isfinite(s.strength))
            s.strength = 1.0f;
        if (!std::isfinite(s.deflection))
            s.deflection = 0.55f;
        if (!std::isfinite(s.spatialFreq))
            s.spatialFreq = 0.004f;
        if (!std::isfinite(s.temporalFreq))
            s.temporalFreq = 0.015f;
        if (!std::isfinite(s.gustStrength))
            s.gustStrength = 0.35f;
        if (!std::isfinite(s.gustFreq))
            s.gustFreq = 0.15f;

        // Wrap heading to (-pi, pi]
        float yaw = std::fmod(s.headingRad, Math::TwoPi);
        if (yaw <= -Math::Pi)
            yaw += Math::TwoPi;
        else if (yaw > Math::Pi)
            yaw -= Math::TwoPi;
        s.headingRad = yaw;

        // Clamp per-kind parameters
        for (int i = 0; i < static_cast<int>(WindKind::Count); ++i)
        {
            TreeWindParams& k = s.kind[i];
            k.influence       = std::clamp(k.influence, 0.0f, 3.0f);
            k.stiffness       = std::clamp(k.stiffness, 0.25f, 8.0f);
            k.mainBendMetres  = std::clamp(k.mainBendMetres, 0.0f, 3.0f);
            k.branchAmp       = std::clamp(k.branchAmp, 0.0f, 0.5f);
            k.edgeAmp         = std::clamp(k.edgeAmp, 0.0f, 0.3f);
            k.flutterFreq     = std::clamp(k.flutterFreq, 0.0f, 8.0f);
            k.detailFadeStart = std::clamp(k.detailFadeStart, 0.0f, 500.0f);
            k.detailFadeEnd   = std::clamp(k.detailFadeEnd, 0.0f, 500.0f);
            k.bendFadeStart   = std::clamp(k.bendFadeStart, 0.0f, 1000.0f);
            k.bendFadeEnd     = std::clamp(k.bendFadeEnd, 0.0f, 1000.0f);
            k.maxSwayMetres   = std::clamp(k.maxSwayMetres, 0.0f, 5.0f);

            // Ensure fade ranges are valid
            if (k.detailFadeEnd < k.detailFadeStart + 0.5f)
                k.detailFadeEnd = k.detailFadeStart + 0.5f;
            if (k.bendFadeEnd < k.bendFadeStart + 0.5f)
                k.bendFadeEnd = k.bendFadeStart + 0.5f;

            // NaN handling
            if (!std::isfinite(k.influence))
                k.influence = 1.0f;
            if (!std::isfinite(k.stiffness))
                k.stiffness = 1.0f;
            if (!std::isfinite(k.mainBendMetres))
                k.mainBendMetres = 0.35f;
            if (!std::isfinite(k.branchAmp))
                k.branchAmp = 0.06f;
            if (!std::isfinite(k.edgeAmp))
                k.edgeAmp = 0.03f;
            if (!std::isfinite(k.flutterFreq))
                k.flutterFreq = 1.6f;
            if (!std::isfinite(k.detailFadeStart))
                k.detailFadeStart = 25.0f;
            if (!std::isfinite(k.detailFadeEnd))
                k.detailFadeEnd = 45.0f;
            if (!std::isfinite(k.bendFadeStart))
                k.bendFadeStart = 120.0f;
            if (!std::isfinite(k.bendFadeEnd))
                k.bendFadeEnd = 180.0f;
            if (!std::isfinite(k.maxSwayMetres))
                k.maxSwayMetres = 1.0f;
        }
    }

    GrassWindSample sampleWind(float x, float z, double t, const WindSettings& settings)
    {
        GrassParams params;
        params.windBaseYaw      = settings.headingRad;
        params.windDeflection   = settings.deflection;
        params.windSpatialFreq  = settings.spatialFreq;
        params.windTemporalFreq = settings.temporalFreq;
        params.seed             = settings.seed;
        params.windTipMetres    = 1.0f; // Not used in sampleGrassWind

        return sampleGrassWind(x, z, t, params);
    }

    float gustAt(double t, const WindSettings& settings)
    {
        if (settings.gustFreq == 0.0f || settings.gustStrength == 0.0f)
            return 1.0f;

        const float n = perlin2(static_cast<float>(t * static_cast<double>(settings.gustFreq)), 3.1f, settings.seed ^ 0x68E31DA4u);
        const float g = 1.0f + settings.gustStrength * n;
        return std::max(0.0f, g);
    }

    void applyWindToGrass(const WindSettings& wind, GrassParams& params)
    {
        params.windBaseYaw      = wind.headingRad;
        params.windDeflection   = wind.deflection;
        params.windSpatialFreq  = wind.spatialFreq;
        params.windTemporalFreq = wind.temporalFreq;
        params.seed             = wind.seed;

        if (wind.grassFollowsStrength)
        {
            params.windTipMetres *= wind.strength;
        }
    }

} // namespace Dark::Terrain
