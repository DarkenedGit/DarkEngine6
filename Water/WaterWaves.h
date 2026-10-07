#pragma once

#include "Math/Vector2f.h"
#include "Math/Vector3f.h"

namespace Dark
{

    namespace Terrain
    {
    class HeightMap;
    }

    constexpr int   kWaterWaveCount   = 4;
    constexpr float kLakeFlowSpeed    = 0.4f;
    constexpr float kShoreChopMax     = 0.18f;

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
        float          waterLevel     = 0.0f;
        Math::Vector2f flowDir        = Math::Vector2f(1.0f, 0.0f);
        float          flowStrength   = 0.85f; // how hard waves align to flow
        float          steepness      = 0.55f; // Gerstner Q, 0 = sine, ~1 = sharp crests
        float          amplitudeScale = 1.0f;  // multiplies every wave amplitude; 1 keeps the authored height
        float          speedScale     = 1.0f;  // multiplies every wave speed; 1 keeps the authored travel speed
        float          flowSpeed      = kLakeFlowSpeed; // meters/second along flow. Scrolls detail and foam. Does not align headings.
        float          foam           = 1.0f;
        float          foamWidthScale = 1.0f;
        float          detailAmount   = 1.0f;
        // Session art rollback. Not saved. Swaps the lake pull to flowStrength * 0.65.
        bool           legacyPull     = false;
        WaterWave      waves[kWaterWaveCount];
    };

    WaterParams defaultWaterParams(float waterLevel);

    // saturate(flowStrength) * 0.25. The shader does not apply a second pull.
    float lakeAlignmentPull(float flowStrength);

    // Analytic surface. `time` in seconds. Position is rest XZ on the water plane.
    // Y only. The HLSL Gerstner also adds a horizontal Q term. Do not fold them together.
    // Shore chop is evaluateShoreChop, not an argument here, so gameplay height stays chop-free.
    void evaluateWaves(
        const WaterParams& params,
        float worldX,
        float worldZ,
        float time,
        float& outY,
        Math::Vector3f& outNormal);

    float waveHeight(const WaterParams& params, float worldX, float worldZ, float time);

    // Sum of scaled displacement amplitudes, plus kShoreChopMax once.
    // The 0.18 m pad is not multiplied by amplitudeScale. Detail normals are not included.
    float displacementAmplitude(const WaterParams& params);
    float maxWaveAmplitude(const WaterParams& params);

    float scaledWaveAmplitude(const WaterParams& params, int waveIndex);
    float scaledWaveSpeed(const WaterParams& params, int waveIndex);

    Math::Vector2f waveDirection(const WaterParams& params, int waveIndex);

    // True only for the old octave quartet (frequency and speed within 1e-3, amplitude and angle within 1e-4).
    bool isLegacyHarmonicPreset(const WaterParams& params);
    void copyLegacyHarmonicWaves(WaterWave outWaves[kWaterWaveCount]);

    struct ShoreSample
    {
        float slope       = 0.0f;
        float horizMeters = 0.0f;
        float bandMeters  = 0.0f;
        float weight      = 0.0f; // 0 open, 1 on the bank
    };

    // maxDisplacementAmp is the four swell amplitudes, not maxWaveAmplitude after the 0.18 m pad.
    ShoreSample evaluateShore(
        float waterSurfaceY,
        float terrainY,
        float slope,
        float foamWidthScale,
        float maxDisplacementAmp);

    // Extra meters of Y at the bank. 0 when weight is 0. Same phase as Water.hlsl.
    // Not folded into evaluateWaves or tryHeightAtWorld.
    float evaluateShoreChop(
        float worldX,
        float worldZ,
        float time,
        float shoreWeight,
        float downslopeX,
        float downslopeZ);

    // Integer value noise shared with Water.hlsl. Not a sin hash.
    float hash01(int x, int z);
    float valueNoise(float x, float z);

    // Central-difference bed. ok is false when the map cannot be sampled.
    struct BedSlope
    {
        float height = 0.0f;
        float slope  = 0.0f;
        float downX  = 1.0f;
        float downZ  = 0.0f;
        bool  ok     = false;
    };

    BedSlope sampleBedSlope(const Terrain::HeightMap& height, float worldX, float worldZ);

} // namespace Dark
