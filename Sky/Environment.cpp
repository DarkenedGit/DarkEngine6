#include "Sky/Environment.h"
#include "Math/MathDefines.h"
#include "Math/MathHelper.h"

#include <cmath>

namespace Dark
{
namespace Sky
{

using namespace Math;

namespace
{

float WrapHours(float h)
{
    h = fmodf(h, 24.0f);
    if (h < 0.0f)
        h += 24.0f;
    return h;
}

float WrapDay(float d)
{
    d = fmodf(d, 365.0f);
    if (d < 1.0f)
        d += 365.0f;
    return d;
}

float HenyeyGreenstein(float cosTheta, float g)
{
    const float g2 = g * g;
    const float denom = 1.0f + g2 - 2.0f * g * cosTheta;
    return (1.0f - g2) / (4.0f * Pi * powf(Max(denom, 1.0e-4f), 1.5f));
}

float RayleighPhase(float cosTheta)
{
    return 0.75f * (1.0f + cosTheta * cosTheta);
}

Vector3f SaturateColor(const Vector3f& c)
{
    return Vector3f(Max(c.x, 0.0f), Max(c.y, 0.0f), Max(c.z, 0.0f));
}

} // namespace

WeatherState WeatherState::Clear()
{
    WeatherState w;
    w.cloudCoverage = 0.05f;
    w.turbidity     = 1.6f;
    w.rain          = 0.0f;
    return w;
}

WeatherState WeatherState::PartlyCloudy()
{
    WeatherState w;
    w.cloudCoverage = 0.35f;
    w.turbidity     = 2.4f;
    w.rain          = 0.0f;
    return w;
}

WeatherState WeatherState::Overcast()
{
    WeatherState w;
    w.cloudCoverage = 0.82f;
    w.turbidity     = 3.8f;
    w.rain          = 0.15f;
    return w;
}

WeatherState WeatherState::Storm()
{
    WeatherState w;
    w.cloudCoverage = 0.96f;
    w.turbidity     = 5.5f;
    w.rain          = 0.85f;
    w.windSpeed     = 0.10f;
    return w;
}

void Environment::sunMoonDirections(
    float timeOfDayHours,
    float dayOfYear,
    float latitudeDeg,
    Vector3f& outSunDir,
    Vector3f& outMoonDir,
    float& outSunElevation)
{
    const float lat  = latitudeDeg * DegToRad;
    const float day  = WrapDay(dayOfYear);
    const float hour = WrapHours(timeOfDayHours);

    // Solar declination (approx). Day 81 ≈ March 22.
    const float decl = 0.4093f * sinf(TwoPi / 365.0f * (day - 81.0f));
    const float ha   = (hour - 12.0f) * 15.0f * DegToRad;

    const float sinAlt = sinf(lat) * sinf(decl) + cosf(lat) * cosf(decl) * cosf(ha);
    const float alt    = asinf(Clamp(sinAlt, -1.0f, 1.0f));
    const float cosAlt = cosf(alt);

    float cosAz = 0.0f;
    if (cosAlt > 1.0e-4f)
        cosAz = (sinf(decl) - sinf(alt) * sinf(lat)) / (cosAlt * cosf(lat) + 1.0e-5f);
    cosAz = Clamp(cosAz, -1.0f, 1.0f);
    float az = acosf(cosAz);
    if (ha > 0.0f)
        az = TwoPi - az;

    // Y-up, +X east, +Z north.
    const float cAlt = cosf(alt);
    outSunDir        = Vector3f(sinf(az) * cAlt, sinf(alt), cosf(az) * cAlt);
    outSunDir.Normalize();
    outSunElevation = alt;

    // Moon: opposite hour angle, shallower path.
    const float moonHour = WrapHours(timeOfDayHours + 12.0f);
    const float haM      = (moonHour - 12.0f) * 15.0f * DegToRad;
    const float moonDecl = -decl * 0.35f;
    const float sinAltM  = sinf(lat) * sinf(moonDecl) + cosf(lat) * cosf(moonDecl) * cosf(haM);
    const float altM     = asinf(Clamp(sinAltM, -1.0f, 1.0f));
    float       cosAzM   = 0.0f;
    const float cosAltM  = cosf(altM);
    if (cosAltM > 1.0e-4f)
        cosAzM = (sinf(moonDecl) - sinf(altM) * sinf(lat)) / (cosAltM * cosf(lat) + 1.0e-5f);
    cosAzM = Clamp(cosAzM, -1.0f, 1.0f);
    float azM = acosf(cosAzM);
    if (haM > 0.0f)
        azM = TwoPi - azM;
    outMoonDir = Vector3f(sinf(azM) * cosf(altM), sinf(altM), cosf(azM) * cosf(altM));
    outMoonDir.Normalize();
}

void Environment::tick(float dt)
{
    if (timeScale != 0.0f)
    {
        timeOfDay = WrapHours(timeOfDay + timeScale * dt);
        if (timeOfDay < 0.001f && timeScale > 0.0f)
            dayOfYear = WrapDay(dayOfYear + 1.0f);
    }
    evaluate();
}

void Environment::evaluate()
{
    sunMoonDirections(timeOfDay, dayOfYear, latitude, m_sunDir, m_moonDir, m_sunElevation);

    const float cover = Clamp(weather.cloudCoverage, 0.0f, 1.0f);
    const float rain  = Clamp(weather.rain, 0.0f, 1.0f);
    const float turb  = Clamp(weather.turbidity + rain * 2.0f, 1.0f, 8.0f);

    const float elev = m_sunElevation;
    const float dayF = SmoothStep(-0.10f, 0.18f, elev);

    // Extinction: long path at the horizon reddens and dims the sun.
    const float airMass    = 1.0f / Max(sinf(Max(elev, 0.0f)) + 0.15f, 0.08f);
    const float extinction = expf(-0.09f * turb * airMass * (1.0f - 0.35f * Max(-elev, 0.0f)));

    const Vector3f noonSun(1.00f, 0.97f, 0.90f);
    const Vector3f duskSun(1.00f, 0.48f, 0.18f);
    const float    warm    = SmoothStep(0.35f, -0.02f, elev);
    m_sunColor             = SaturateColor((noonSun * (1.0f - warm) + duskSun * warm) * (1.35f * extinction * dayF));
    m_sunColor             = m_sunColor * (1.0f - 0.82f * cover) * (1.0f - 0.35f * rain);

    const float moonUp = SmoothStep(-0.05f, 0.12f, m_moonDir.y);
    m_moonColor        = Vector3f(0.18f, 0.22f, 0.34f) * (0.22f * moonUp * (1.0f - 0.6f * cover));

    // Dominant illuminant: sun while up, blend through twilight, moon at night.
    const float sunW = SmoothStep(-0.04f, 0.08f, elev);
    m_lightDir       = m_sunDir * sunW + m_moonDir * (1.0f - sunW);
    m_lightDir.Normalize();
    m_lightColor = m_sunColor * sunW + m_moonColor * (1.0f - sunW);

    m_skyZenith  = evaluateSky(Vector3f(0.0f, 1.0f, 0.0f));
    m_skyHorizon = evaluateSky(Vector3f(1.0f, 0.05f, 0.0f));

    const float ambDay = 0.16f + 0.22f * cover;
    const float ambNgt = 0.03f + 0.02f * cover;
    const float amb    = Lerp(ambNgt, ambDay, dayF);
    m_ambientColor     = (m_skyZenith * 0.55f + m_skyHorizon * 0.45f) * amb;

    m_fogColor   = m_skyHorizon * (0.75f + 0.25f * cover);
    m_fogDensity = (0.0035f + 0.012f * cover + 0.02f * rain) * (1.15f - 0.4f * dayF);
    m_exposure   = Lerp(0.55f, 1.05f, dayF) * (1.0f - 0.15f * cover);

    (void)turb;
}

Vector3f Environment::evaluateSky(const Vector3f& viewDir) const
{
    Vector3f v = viewDir;
    const float mag = v.Magnitude();
    if (mag > 1.0e-5f)
        v *= (1.0f / mag);
    else
        v = Vector3f(0.0f, 1.0f, 0.0f);

    const float cover = Clamp(weather.cloudCoverage, 0.0f, 1.0f);
    const float rain  = Clamp(weather.rain, 0.0f, 1.0f);
    const float turb  = Clamp(weather.turbidity + rain * 2.0f, 1.0f, 8.0f);
    const float elev  = m_sunElevation;
    const float dayF  = SmoothStep(-0.12f, 0.16f, elev);

    const Vector3f dayZen(0.10f, 0.32f, 0.68f);
    const Vector3f dayHor(0.62f, 0.74f, 0.88f);
    const Vector3f duskZen(0.18f, 0.12f, 0.22f);
    const Vector3f duskHor(0.92f, 0.38f, 0.16f);
    const Vector3f nightZen(0.01f, 0.02f, 0.05f);
    const Vector3f nightHor(0.03f, 0.04f, 0.08f);

    const float dusk = SmoothStep(0.32f, -0.02f, elev);
    Vector3f    zen  = dayZen * (1.0f - dusk) + duskZen * dusk;
    Vector3f    hor  = dayHor * (1.0f - dusk) + duskHor * dusk;
    zen              = zen * dayF + nightZen * (1.0f - dayF);
    hor              = hor * dayF + nightHor * (1.0f - dayF);

    const float vy = v.y;
    if (vy < 0.0f)
    {
        const float t = Clamp(-vy, 0.0f, 1.0f);
        Vector3f    ground = hor * 0.35f;
        return SaturateColor(hor * (1.0f - t) + ground * t);
    }

    const float h = powf(1.0f - Clamp(vy, 0.0f, 1.0f), 0.45f);
    Vector3f    base = zen * (1.0f - h) + hor * h;

    const float cosS = Clamp(v.Dot(m_sunDir), -1.0f, 1.0f);
    const float cosM = Clamp(v.Dot(m_moonDir), -1.0f, 1.0f);
    const float mieS = HenyeyGreenstein(cosS, 0.86f);
    const float mieM = HenyeyGreenstein(cosM, 0.80f);
    const float ray  = RayleighPhase(cosS);

    const float sunVis = SmoothStep(-0.02f, 0.06f, elev) * (1.0f - 0.88f * cover);
    base.x += m_sunColor.x * (0.12f * ray + 0.35f * mieS) * sunVis;
    base.y += m_sunColor.y * (0.12f * ray + 0.35f * mieS) * sunVis;
    base.z += m_sunColor.z * (0.12f * ray + 0.35f * mieS) * sunVis;

    const float moonVis = SmoothStep(-0.02f, 0.08f, m_moonDir.y) * (1.0f - dayF) * (1.0f - 0.7f * cover);
    base = base + m_moonColor * (1.2f * mieM * moonVis);

    // ~0.8° disc (half-angle ~0.4°).
    const float ang  = acosf(cosS);
    const float disc = (1.0f - SmoothStep(0.007f, 0.016f, ang)) * sunVis;
    base             = base + Vector3f(1.0f, 0.96f, 0.88f) * (3.0f * disc);

    const float lum = 0.2126f * base.x + 0.7152f * base.y + 0.0722f * base.z;
    Vector3f    overcast(lum * 0.92f, lum * 0.95f, lum * 1.00f);
    overcast        = overcast * (0.45f + 0.55f * Clamp(vy, 0.0f, 1.0f));
    const Vector3f stormTint(0.18f, 0.20f, 0.22f);
    overcast        = overcast * (1.0f - 0.45f * rain) + stormTint * (0.45f * rain);

    base = base * (1.0f - cover) + overcast * cover;
    (void)turb;
    return SaturateColor(base);
}

} // namespace Sky
} // namespace Dark
