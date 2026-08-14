#include "Water/WaterWaves.h"
#include "Math/MathDefines.h"
#include "Math/MathHelper.h"

#include <cmath>

namespace Dark
{
namespace Water
{

using namespace Math;

namespace
{

Vector2f Rotate(const Vector2f& v, float radians)
{
    const float c = cosf(radians);
    const float s = sinf(radians);
    return Vector2f(v.x * c - v.y * s, v.x * s + v.y * c);
}

Vector2f NormalizedFlow(const WaterParams& params)
{
    Vector2f f = params.flowDir;
    const float mag = f.Magnitude();
    if (mag < 1.0e-5f)
        return Vector2f(1.0f, 0.0f);
    f *= (1.0f / mag);
    return f;
}

} // namespace

WaterParams defaultWaterParams(float waterLevel)
{
    WaterParams p;
    p.waterLevel   = waterLevel;
    p.flowDir      = Vector2f(1.0f, 0.25f);
    p.flowStrength = 0.85f;
    p.steepness    = 0.55f;

    // Long swell along the flow, then mid chop, then two smaller cross-ripples.
    p.waves[0] = WaterWave{ 0.10f, TwoPi / 28.0f, 0.42f, 1.15f };
    p.waves[1] = WaterWave{ -0.55f, TwoPi / 14.0f, 0.20f, 1.70f };
    p.waves[2] = WaterWave{ 0.95f, TwoPi / 7.0f, 0.09f, 2.35f };
    p.waves[3] = WaterWave{ -1.35f, TwoPi / 3.5f, 0.035f, 3.10f };
    return p;
}

Vector2f waveDirection(const WaterParams& params, int waveIndex)
{
    if (waveIndex < 0 || waveIndex >= kWaterWaveCount)
        return NormalizedFlow(params);

    const Vector2f flow = NormalizedFlow(params);
    const float    ang  = params.waves[waveIndex].angleFromFlow;
    const Vector2f rest = Rotate(flow, ang);
    // flowStrength 0 → keep the offset angle; 1 → pull toward flow.
    const float t = Clamp(params.flowStrength, 0.0f, 1.0f);
    Vector2f    d(Lerp(rest.x, flow.x, t * 0.65f), Lerp(rest.y, flow.y, t * 0.65f));
    const float mag = d.Magnitude();
    if (mag < 1.0e-5f)
        return flow;
    d *= (1.0f / mag);
    return d;
}

float maxWaveAmplitude(const WaterParams& params)
{
    float s = 0.0f;
    for (int i = 0; i < kWaterWaveCount; ++i)
        s += params.waves[i].amplitude;
    return s;
}

void evaluateWaves(
    const WaterParams& params,
    float worldX,
    float worldZ,
    float time,
    float& outY,
    Vector3f& outNormal)
{
    float y = params.waterLevel;
    // Accumulate Gerstner partials; start with a flat +Y normal basis.
    float nx = 0.0f;
    float ny = 1.0f;
    float nz = 0.0f;

    const float Q = Clamp(params.steepness, 0.0f, 1.0f);

    for (int i = 0; i < kWaterWaveCount; ++i)
    {
        const WaterWave& w = params.waves[i];
        if (w.amplitude <= 0.0f || w.frequency <= 0.0f)
            continue;

        const Vector2f D  = waveDirection(params, i);
        const float    k  = w.frequency;
        const float    A  = w.amplitude;
        const float    dp = (D.x * worldX + D.y * worldZ) * k + time * w.speed;
        const float    s  = sinf(dp);
        const float    c  = cosf(dp);
        const float    wa = k * A;

        y += A * s;

        nx += D.x * wa * c;
        nz += D.y * wa * c;
        ny -= Q * wa * s;
    }

    outY = y;
    outNormal = Vector3f(-nx, ny, -nz);
    outNormal.Normalize();
}

float waveHeight(const WaterParams& params, float worldX, float worldZ, float time)
{
    Vector3f n;
    float    y = 0.0f;
    evaluateWaves(params, worldX, worldZ, time, y, n);
    return y;
}

} // namespace Water
} // namespace Dark
