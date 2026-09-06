#pragma once

#include "Math/Vector2f.h"
#include "Math/Vector3f.h"

namespace Dark
{
namespace Sky
{

// Cloud / haze / rain knobs. Coverage is the master weather slider.
struct WeatherState
{
    float          cloudCoverage = 0.20f; // 0 = clear, 1 = overcast
    float          turbidity     = 2.2f;  // 1..8, haze / Mie
    float          windSpeed     = 0.04f;
    Math::Vector2f windDir       = Math::Vector2f(1.0f, 0.2f);
    float          rain          = 0.0f;  // 0..1, darkens and raises haze

    static WeatherState Clear();
    static WeatherState PartlyCloudy();
    static WeatherState Overcast();
    static WeatherState Storm();
};

// Shared celestial + weather state. Call evaluate() after changing inputs
// (tick() does this). Terrain, water, and the sky pass all read the outputs.
class Environment
{
public:
    float        timeOfDay  = 15.5f; // hours, [0, 24)
    float        dayOfYear  = 172.0f; // 1..365
    float        latitude   = 47.6f;  // degrees
    float        timeScale  = 0.0f;   // hours advanced per real second
    WeatherState weather    = WeatherState::PartlyCloudy();

    // Fog art direction. When fogAuto is true, evaluate() writes densities from
    // weather/time then multiplies by the scales. When false, setFog* sticks.
    bool  fogAuto             = true;
    float fogDistanceScale    = 1.0f;
    float heightFogScale      = 1.0f;
    float volumetricFogScale  = 1.0f;

    void tick(float dt);
    void evaluate();

    // Direction *toward* the body (same convention as existing lightDirWS).
    const Math::Vector3f& sunDir() const { return m_sunDir; }
    const Math::Vector3f& moonDir() const { return m_moonDir; }
    const Math::Vector3f& lightDir() const { return m_lightDir; }

    const Math::Vector3f& sunColor() const { return m_sunColor; }
    const Math::Vector3f& moonColor() const { return m_moonColor; }
    const Math::Vector3f& lightColor() const { return m_lightColor; }
    const Math::Vector3f& ambientColor() const { return m_ambientColor; }
    const Math::Vector3f& fogColor() const { return m_fogColor; }
    const Math::Vector3f& skyZenith() const { return m_skyZenith; }
    const Math::Vector3f& skyHorizon() const { return m_skyHorizon; }

    float sunElevation() const { return m_sunElevation; } // radians, negative = below horizon
    float fogDensity() const { return m_fogDensity; }
    float heightFogDensity() const { return m_heightFogDensity; }
    float heightFogFalloff() const { return m_heightFogFalloff; }
    float volumetricFogDensity() const { return m_volumetricFogDensity; }
    float volumetricFogHeight() const { return m_volumetricFogHeight; }
    float exposure() const { return m_exposure; }

    void setFogColor(const Math::Vector3f& c) { m_fogColor = c; }
    void setFogDensity(float v) { m_fogDensity = v; }
    void setHeightFogDensity(float v) { m_heightFogDensity = v; }
    void setHeightFogFalloff(float v) { m_heightFogFalloff = v; }
    void setVolumetricFogDensity(float v) { m_volumetricFogDensity = v; }
    void setVolumetricFogHeight(float v) { m_volumetricFogHeight = v; }
    void resetFogTune();

    // Analytic sky / reflection color for a world-space view direction.
    Math::Vector3f evaluateSky(const Math::Vector3f& viewDir) const;

    // Apparent solar half-angle in radians. Grows near the horizon (refraction / airlight).
    static float sunAngularRadius(float sunElevationRadians);

    static void sunMoonDirections(
        float timeOfDayHours,
        float dayOfYear,
        float latitudeDeg,
        Math::Vector3f& outSunDir,
        Math::Vector3f& outMoonDir,
        float& outSunElevation);

private:
    Math::Vector3f m_sunDir{ 0.35f, 0.85f, -0.35f };
    Math::Vector3f m_moonDir{ -0.2f, 0.4f, 0.3f };
    Math::Vector3f m_lightDir{ 0.35f, 0.85f, -0.35f };
    Math::Vector3f m_sunColor{ 1.0f, 0.96f, 0.88f };
    Math::Vector3f m_moonColor{ 0.15f, 0.18f, 0.28f };
    Math::Vector3f m_lightColor{ 1.0f, 0.96f, 0.88f };
    Math::Vector3f m_ambientColor{ 0.18f, 0.20f, 0.24f };
    Math::Vector3f m_fogColor{ 0.55f, 0.62f, 0.72f };
    Math::Vector3f m_skyZenith{ 0.22f, 0.40f, 0.62f };
    Math::Vector3f m_skyHorizon{ 0.62f, 0.72f, 0.82f };
    float          m_sunElevation          = 0.8f;
    float          m_fogDensity            = 0.004f;
    float          m_heightFogDensity      = 0.0f;
    float          m_heightFogFalloff      = 0.06f;
    float          m_volumetricFogDensity  = 0.012f;
    float          m_volumetricFogHeight   = 14.0f;
    float          m_exposure              = 1.0f;
};

} // namespace Sky
} // namespace Dark
