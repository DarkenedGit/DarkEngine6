#include "Terrain/WindField.h"

#include "Math/MathHelper.h"
#include "Terrain/GrassWind.h"
#include "Terrain/TerrainGen.h"

#include <algorithm>
#include <cmath>

namespace Dark::Terrain
{
    namespace
    {
        void repairFinite(float& value, float fallback)
        {
            if (!std::isfinite(value))
                value = fallback;
        }

        void repairKind(TreeWindParams& kind, const TreeWindParams& fallback)
        {
            repairFinite(kind.influence, fallback.influence);
            repairFinite(kind.stiffness, fallback.stiffness);
            repairFinite(kind.mainBendMetres, fallback.mainBendMetres);
            repairFinite(kind.branchAmp, fallback.branchAmp);
            repairFinite(kind.edgeAmp, fallback.edgeAmp);
            repairFinite(kind.flutterFreq, fallback.flutterFreq);
            repairFinite(kind.detailFadeStart, fallback.detailFadeStart);
            repairFinite(kind.detailFadeEnd, fallback.detailFadeEnd);
            repairFinite(kind.bendFadeStart, fallback.bendFadeStart);
            repairFinite(kind.bendFadeEnd, fallback.bendFadeEnd);
            repairFinite(kind.maxSwayMetres, fallback.maxSwayMetres);
        }
    } // namespace

    WindSettings defaultWindSettings()
    {
        WindSettings settings;
        settings.strength             = 1.0f;
        settings.headingRad           = 1.37f;
        settings.deflection           = 0.55f;
        settings.spatialFreq          = 0.004f;
        settings.temporalFreq         = 0.015f;
        settings.gustStrength         = 0.35f;
        settings.gustFreq             = 0.15f;
        settings.grassFollowsStrength = false;
        settings.seed                 = 1337u;

        TreeWindParams& tree = settings.kind[static_cast<int>(WindKind::Tree)];
        tree.influence       = 1.0f;
        tree.stiffness       = 1.0f;
        tree.mainBendMetres  = 0.35f;
        tree.branchAmp       = 0.06f;
        tree.edgeAmp         = 0.03f;
        tree.flutterFreq     = 1.6f;
        tree.detailFadeStart = 25.0f;
        tree.detailFadeEnd   = 45.0f;
        tree.bendFadeStart   = 120.0f;
        tree.bendFadeEnd     = 180.0f;
        tree.maxSwayMetres   = 1.0f;

        TreeWindParams& flower = settings.kind[static_cast<int>(WindKind::Flower)];
        flower.influence       = 1.0f;
        flower.stiffness       = 0.6f;
        flower.mainBendMetres  = 0.05f;
        flower.branchAmp       = 0.06f;
        flower.edgeAmp         = 0.02f;
        flower.flutterFreq     = 1.6f;
        flower.detailFadeStart = 25.0f;
        flower.detailFadeEnd   = 45.0f;
        flower.bendFadeStart   = 120.0f;
        flower.bendFadeEnd     = 180.0f;
        flower.maxSwayMetres   = 1.0f;

        TreeWindParams& rock = settings.kind[static_cast<int>(WindKind::Rock)];
        rock.influence       = 0.0f;
        rock.stiffness       = 1.0f;
        rock.mainBendMetres  = 0.0f;
        rock.branchAmp       = 0.0f;
        rock.edgeAmp         = 0.0f;
        rock.flutterFreq     = 0.0f;
        rock.detailFadeStart = 25.0f;
        rock.detailFadeEnd   = 45.0f;
        rock.bendFadeStart   = 120.0f;
        rock.bendFadeEnd     = 180.0f;
        rock.maxSwayMetres   = 0.0f;

        TreeWindParams& tuft = settings.kind[static_cast<int>(WindKind::GrassTuft)];
        tuft.influence       = 1.0f;
        tuft.stiffness       = 1.0f;
        tuft.mainBendMetres  = 0.08f;
        tuft.branchAmp       = 0.06f;
        tuft.edgeAmp         = 0.03f;
        tuft.flutterFreq     = 1.6f;
        tuft.detailFadeStart = 25.0f;
        tuft.detailFadeEnd   = 45.0f;
        tuft.bendFadeStart   = 120.0f;
        tuft.bendFadeEnd     = 180.0f;
        tuft.maxSwayMetres   = 1.0f;

        return settings;
    }

    void clampWindSettings(WindSettings& settings)
    {
        const WindSettings fallback = defaultWindSettings();
        repairFinite(settings.strength, fallback.strength);
        repairFinite(settings.deflection, fallback.deflection);
        repairFinite(settings.spatialFreq, fallback.spatialFreq);
        repairFinite(settings.temporalFreq, fallback.temporalFreq);
        repairFinite(settings.gustStrength, fallback.gustStrength);
        repairFinite(settings.gustFreq, fallback.gustFreq);
        repairFinite(settings.headingRad, fallback.headingRad);

        settings.strength     = std::clamp(settings.strength, 0.0f, 3.0f);
        settings.deflection   = std::clamp(settings.deflection, 0.0f, 1.20f);
        settings.spatialFreq  = std::clamp(settings.spatialFreq, 0.0005f, 0.02f);
        settings.temporalFreq = std::clamp(settings.temporalFreq, 0.0f, 1.0f);
        settings.gustStrength = std::clamp(settings.gustStrength, 0.0f, 1.0f);
        settings.gustFreq     = std::clamp(settings.gustFreq, 0.0f, 2.0f);

        float yaw = std::fmod(settings.headingRad, Math::TwoPi);
        if (yaw <= -Math::Pi)
            yaw += Math::TwoPi;
        else if (yaw > Math::Pi)
            yaw -= Math::TwoPi;
        settings.headingRad = yaw;

        for (int i = 0; i < static_cast<int>(WindKind::Count); ++i)
        {
            TreeWindParams& kind = settings.kind[i];
            repairKind(kind, fallback.kind[i]);
            kind.influence       = std::clamp(kind.influence, 0.0f, 3.0f);
            kind.stiffness       = std::clamp(kind.stiffness, 0.25f, 8.0f);
            kind.mainBendMetres  = std::clamp(kind.mainBendMetres, 0.0f, 3.0f);
            kind.branchAmp       = std::clamp(kind.branchAmp, 0.0f, 0.5f);
            kind.edgeAmp         = std::clamp(kind.edgeAmp, 0.0f, 0.3f);
            kind.flutterFreq     = std::clamp(kind.flutterFreq, 0.0f, 8.0f);
            kind.detailFadeStart = std::clamp(kind.detailFadeStart, 0.0f, 500.0f);
            kind.detailFadeEnd   = std::clamp(kind.detailFadeEnd, 0.0f, 500.0f);
            kind.bendFadeStart   = std::clamp(kind.bendFadeStart, 0.0f, 1000.0f);
            kind.bendFadeEnd     = std::clamp(kind.bendFadeEnd, 0.0f, 1000.0f);
            kind.maxSwayMetres   = std::clamp(kind.maxSwayMetres, 0.0f, 5.0f);
            if (kind.detailFadeEnd < kind.detailFadeStart + 0.5f)
                kind.detailFadeEnd = kind.detailFadeStart + 0.5f;
            if (kind.bendFadeEnd < kind.bendFadeStart + 0.5f)
                kind.bendFadeEnd = kind.bendFadeStart + 0.5f;
        }
    }

    GrassWindSample sampleWind(float x, float z, double timeSec, const WindSettings& settings)
    {
        GrassParams params;
        params.windBaseYaw      = settings.headingRad;
        params.windDeflection   = settings.deflection;
        params.windSpatialFreq  = settings.spatialFreq;
        params.windTemporalFreq = settings.temporalFreq;
        params.seed             = settings.seed;
        params.windTipMetres    = 1.0f;
        return sampleGrassWind(x, z, timeSec, params);
    }

    float gustAt(double timeSec, const WindSettings& settings)
    {
        if (settings.gustFreq == 0.0f || settings.gustStrength == 0.0f)
            return 1.0f;
        const float n = perlin2(static_cast<float>(timeSec * static_cast<double>(settings.gustFreq)), 3.1f, settings.seed ^ 0x68E31DA4u);
        return std::max(0.0f, 1.0f + settings.gustStrength * n);
    }

    float kindSwayMetres(const WindSettings& settings, int kindIndex)
    {
        if (kindIndex < 0 || kindIndex >= static_cast<int>(WindKind::Count))
            return 0.0f;
        const TreeWindParams& kind = settings.kind[kindIndex];
        const float amp           = std::max(settings.strength, 0.0f) * (1.0f + std::max(settings.gustStrength, 0.0f));
        const float stiff         = std::max(kind.stiffness, 0.25f);
        const float bend          = (kind.mainBendMetres * kind.influence / stiff) * amp;
        const float flutter       = 2.0f * kind.influence * amp * (kind.edgeAmp + kind.branchAmp);
        return std::max(0.0f, bend + flutter);
    }

    void applyWindToGrass(const WindSettings& wind, GrassParams& params)
    {
        params.windBaseYaw      = wind.headingRad;
        params.windDeflection   = wind.deflection;
        params.windSpatialFreq  = wind.spatialFreq;
        params.windTemporalFreq = wind.temporalFreq;
        params.seed             = wind.seed;
        if (wind.grassFollowsStrength)
            params.windTipMetres *= wind.strength;
    }

} // namespace Dark::Terrain
