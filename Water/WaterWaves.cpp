#include "Water/WaterWaves.h"

#include "Core/Log.h"
#include "Math/MathDefines.h"
#include "Math/MathHelper.h"
#include "Terrain/HeightMap.h"

#include <cmath>
#include <cstdint>

namespace Dark
{
    using namespace Math;

    namespace
    {

    constexpr float kLegacyAngle[kWaterWaveCount]       = { 0.10f, -0.55f, 0.95f, -1.35f };
    constexpr float kLegacyWavelength[kWaterWaveCount]  = { 28.0f, 14.0f, 7.0f, 3.5f };
    constexpr float kLegacyAmplitude[kWaterWaveCount]   = { 0.42f, 0.20f, 0.09f, 0.035f };
    constexpr float kLegacySpeed[kWaterWaveCount]       = { 1.15f, 1.70f, 2.35f, 3.10f };

    constexpr float kChopAlongWavelength  = 1.6f;
    constexpr float kChopAlongAmplitude   = 0.12f;
    constexpr float kChopAlongSpeed       = 2.4f;
    constexpr float kChopAcrossWavelength = 0.9f;
    constexpr float kChopAcrossAmplitude  = 0.06f;
    constexpr float kChopAcrossSpeed      = 3.1f;

    Vector2f Rotate(const Vector2f& v, float radians)
    {
        const float c = cosf(radians);
        const float s = sinf(radians);
        return Vector2f(v.x * c - v.y * s, v.x * s + v.y * c);
    }

    Vector2f NormalizedFlow(const WaterParams& params)
    {
        Vector2f    f   = params.flowDir;
        const float mag = f.Magnitude();
        if (mag < 1.0e-5f)
            return Vector2f(1.0f, 0.0f);
        f *= (1.0f / mag);
        return f;
    }

    bool Near(float a, float b, float tol)
    {
        return fabsf(a - b) <= tol;
    }

    float ChopY(float worldX, float worldZ, float time, float shoreWeight, float dirX, float dirZ, float wavelength, float amplitude, float speed)
    {
        const float A = amplitude * shoreWeight;
        if (A <= 0.0f || wavelength <= 1.0e-4f)
            return 0.0f;
        const float k  = TwoPi / wavelength;
        const float dp = (dirX * worldX + dirZ * worldZ) * k + time * speed;
        return A * sinf(dp);
    }

    } // namespace

    WaterParams defaultWaterParams(float waterLevel)
    {
        WaterParams p;
        p.waterLevel   = waterLevel;
        p.flowDir      = Vector2f(1.0f, 0.25f);
        p.flowStrength = 0.85f;
        p.steepness    = 0.55f;
        p.flowSpeed    = kLakeFlowSpeed;
        p.foam         = 1.0f;
        p.foamWidthScale = 1.0f;
        p.detailAmount = 1.0f;

        // Incommensurate swell. Ratios are not integers, so crests do not phase-lock.
        p.waves[0] = WaterWave{ 0.15f, TwoPi / 37.0f, 0.36f, 1.05f };
        p.waves[1] = WaterWave{ 2.05f, TwoPi / 19.4f, 0.18f, 1.45f };
        p.waves[2] = WaterWave{ -1.55f, TwoPi / 11.3f, 0.11f, 1.85f };
        p.waves[3] = WaterWave{ 1.15f, TwoPi / 6.4f, 0.07f, 2.25f };
        return p;
    }

    float lakeAlignmentPull(float flowStrength)
    {
        return Clamp(flowStrength, 0.0f, 1.0f) * 0.25f;
    }

    Vector2f waveDirection(const WaterParams& params, int waveIndex)
    {
        if (waveIndex < 0 || waveIndex >= kWaterWaveCount)
            return NormalizedFlow(params);

        const Vector2f flow = NormalizedFlow(params);
        const float    ang  = params.waves[waveIndex].angleFromFlow;
        const Vector2f rest = Rotate(flow, ang);
        // Lakes pull once, here. legacyPull is the session checkbox (0.65) and is not saved.
        const float pull = params.legacyPull ? Clamp(params.flowStrength, 0.0f, 1.0f) * 0.65f : lakeAlignmentPull(params.flowStrength);
        Vector2f    d(Lerp(rest.x, flow.x, pull), Lerp(rest.y, flow.y, pull));
        const float mag = d.Magnitude();
        if (mag < 1.0e-5f)
            return flow;
        d *= (1.0f / mag);
        return d;
    }

    float scaledWaveAmplitude(const WaterParams& params, int waveIndex)
    {
        if (waveIndex < 0 || waveIndex >= kWaterWaveCount)
            return 0.0f;
        const float scale = params.amplitudeScale > 0.0f ? params.amplitudeScale : 0.0f;
        return params.waves[waveIndex].amplitude * scale;
    }

    float scaledWaveSpeed(const WaterParams& params, int waveIndex)
    {
        if (waveIndex < 0 || waveIndex >= kWaterWaveCount)
            return 0.0f;
        const float scale = params.speedScale > 0.0f ? params.speedScale : 0.0f;
        return params.waves[waveIndex].speed * scale;
    }

    float displacementAmplitude(const WaterParams& params)
    {
        float s = 0.0f;
        for (int i = 0; i < kWaterWaveCount; ++i)
            s += scaledWaveAmplitude(params, i);
        return s;
    }

    float maxWaveAmplitude(const WaterParams& params)
    {
        return displacementAmplitude(params) + kShoreChopMax;
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
            const float      A = scaledWaveAmplitude(params, i);
            if (A <= 0.0f || w.frequency <= 0.0f)
                continue;

            const Vector2f D  = waveDirection(params, i);
            const float    k  = w.frequency;
            const float    dp = (D.x * worldX + D.y * worldZ) * k + time * scaledWaveSpeed(params, i);
            const float    s  = sinf(dp);
            const float    c  = cosf(dp);
            const float    wa = k * A;

            y += A * s;

            nx += D.x * wa * c;
            nz += D.y * wa * c;
            ny -= Q * wa * s;
        }

        outY      = y;
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

    void copyLegacyHarmonicWaves(WaterWave outWaves[kWaterWaveCount])
    {
        for (int i = 0; i < kWaterWaveCount; ++i)
            outWaves[i] = WaterWave{ kLegacyAngle[i], TwoPi / kLegacyWavelength[i], kLegacyAmplitude[i], kLegacySpeed[i] };
    }

    bool isLegacyHarmonicPreset(const WaterParams& params)
    {
        for (int i = 0; i < kWaterWaveCount; ++i)
        {
            const WaterWave& w = params.waves[i];
            if (!Near(w.angleFromFlow, kLegacyAngle[i], 1.0e-4f))
                return false;
            if (!Near(w.frequency, TwoPi / kLegacyWavelength[i], 1.0e-3f))
                return false;
            if (!Near(w.amplitude, kLegacyAmplitude[i], 1.0e-4f))
                return false;
            if (!Near(w.speed, kLegacySpeed[i], 1.0e-3f))
                return false;
        }
        return true;
    }

    ShoreSample evaluateShore(
        float waterSurfaceY,
        float terrainY,
        float slope,
        float foamWidthScale,
        float maxDisplacementAmp)
    {
        ShoreSample s;
        if (slope < 0.0f)
            slope = 0.0f;
        const float depth = waterSurfaceY > terrainY ? (waterSurfaceY - terrainY) : 0.0f;
        s.slope           = slope;
        s.horizMeters     = slope > 1.0e-3f ? depth / slope : (depth < 0.05f ? 0.0f : 1.0e4f);

        float scale = foamWidthScale;
        if (scale < 0.0f)
            scale = 0.0f;
        const float slopeT = Clamp(slope / 0.7f, 0.0f, 1.0f);
        const float amp    = maxDisplacementAmp > 0.0f ? maxDisplacementAmp : 0.0f;
        const float ampT   = Clamp(amp / 0.75f, 0.0f, 1.0f);
        float       band   = 8.0f * scale;
        band *= Lerp(1.15f, 0.45f, slopeT);
        band *= Lerp(0.85f, 1.25f, ampT);
        s.bandMeters = Clamp(band, 2.5f, 14.0f);
        s.weight     = s.bandMeters > 1.0e-4f ? Clamp(1.0f - s.horizMeters / s.bandMeters, 0.0f, 1.0f) : 0.0f;
        return s;
    }

    float evaluateShoreChop(
        float worldX,
        float worldZ,
        float time,
        float shoreWeight,
        float downslopeX,
        float downslopeZ)
    {
        if (shoreWeight <= 0.0f)
            return 0.0f;
        if (shoreWeight > 1.0f)
            shoreWeight = 1.0f;

        float dx = downslopeX;
        float dz = downslopeZ;
        const float mag = sqrtf(dx * dx + dz * dz);
        if (mag <= 1.0e-5f)
        {
            dx = 1.0f;
            dz = 0.0f;
        }
        else
        {
            dx /= mag;
            dz /= mag;
        }
        const float ax = -dz;
        const float az = dx;
        const float along  = ChopY(worldX, worldZ, time, shoreWeight, dx, dz, kChopAlongWavelength, kChopAlongAmplitude, kChopAlongSpeed);
        const float across = ChopY(worldX, worldZ, time, shoreWeight, ax, az, kChopAcrossWavelength, kChopAcrossAmplitude, kChopAcrossSpeed);
        return along + across;
    }

    float hash01(int x, int z)
    {
        uint32_t n = static_cast<uint32_t>(x) * 374761393u ^ static_cast<uint32_t>(z) * 668265263u;
        n          = (n ^ 0x27d4eb2du) * 1274126177u;
        n          = n ^ (n >> 16);
        return static_cast<float>(n & 0x00FFFFFFu) / 16777216.0f;
    }

    float valueNoise(float x, float z)
    {
        const float x0 = floorf(x);
        const float z0 = floorf(z);
        const float fx = x - x0;
        const float fz = z - z0;
        const float ux = fx * fx * (3.0f - 2.0f * fx);
        const float uz = fz * fz * (3.0f - 2.0f * fz);
        const int   ix = static_cast<int>(x0);
        const int   iz = static_cast<int>(z0);
        const float h00 = hash01(ix, iz);
        const float h10 = hash01(ix + 1, iz);
        const float h01 = hash01(ix, iz + 1);
        const float h11 = hash01(ix + 1, iz + 1);
        const float a   = h00 + (h10 - h00) * ux;
        const float b   = h01 + (h11 - h01) * ux;
        return a + (b - a) * uz;
    }

    BedSlope sampleBedSlope(const Terrain::HeightMap& height, float worldX, float worldZ)
    {
        BedSlope s;
        if (!height.valid() || height.cellSize() <= 1.0e-6f)
            return s;
        const float h  = height.cellSize();
        const float hL = height.heightAtWorld(worldX - h, worldZ);
        const float hR = height.heightAtWorld(worldX + h, worldZ);
        const float hD = height.heightAtWorld(worldX, worldZ - h);
        const float hU = height.heightAtWorld(worldX, worldZ + h);
        const float gx = (hR - hL) * (0.5f / h);
        const float gz = (hU - hD) * (0.5f / h);
        s.height       = height.heightAtWorld(worldX, worldZ);
        s.slope        = sqrtf(gx * gx + gz * gz);
        if (s.slope > 1.0e-5f)
        {
            s.downX = -gx / s.slope;
            s.downZ = -gz / s.slope;
        }
        s.ok = true;
        return s;
    }

} // namespace Dark
