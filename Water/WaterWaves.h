#pragma once

#include "Math/Vector2f.h"
#include "Math/Vector3f.h"

namespace Dark
{
namespace Water
{

constexpr int kWaterWaveCount = 4;

// One Gerstner component. Direction is `flow` rotated by `angleFromFlow`.
struct WaterWave
{
    float angleFromFlow = 0.0f; // radians
    float frequency     = 1.0f; // 2π / wavelength
    float amplitude     = 0.1f;
    float speed         = 1.0f;
};

struct WaterParams
{
    float           waterLevel    = 0.0f;
    Math::Vector2f  flowDir       = Math::Vector2f(1.0f, 0.0f);
    float           flowStrength  = 0.85f; // how hard waves align to flow
    float           steepness     = 0.55f; // Gerstner Q, 0 = sine, ~1 = sharp crests
    WaterWave       waves[kWaterWaveCount];
};

WaterParams defaultWaterParams(float waterLevel);

// Analytic surface. `time` in seconds. Position is rest XZ on the water plane.
void evaluateWaves(
    const WaterParams& params,
    float worldX,
    float worldZ,
    float time,
    float& outY,
    Math::Vector3f& outNormal);

float waveHeight(const WaterParams& params, float worldX, float worldZ, float time);

// Sum of amplitudes — used to expand chunk AABBs and valley-cull margin.
float maxWaveAmplitude(const WaterParams& params);

Math::Vector2f waveDirection(const WaterParams& params, int waveIndex);

} // namespace Water
} // namespace Dark
