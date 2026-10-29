#ifndef WIND_FIELD_HLSLI
#define WIND_FIELD_HLSLI

// Terrain::hash21 and Terrain::perlin2. WindFieldTests locks hash21's literals.

float hash21(int x, int z, uint seed)
{
    uint h = uint(x) * 374761393u + uint(z) * 668265263u + seed * 2246822519u;
    h      = (h ^ (h >> 13)) * 1274126177u;
    return float(h & 0x00FFFFFFu) * (1.0 / 16777216.0);
}

float fade(float t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float cornerDot(int ix, int iz, float dx, float dz, uint seed)
{
    const float kInvSqrt2 = 0.7071067811865476;
    const float kGradX[8] = { kInvSqrt2, -kInvSqrt2, kInvSqrt2, -kInvSqrt2, 1.0, -1.0, 0.0, 0.0 };
    const float kGradZ[8] = { kInvSqrt2, kInvSqrt2, -kInvSqrt2, -kInvSqrt2, 0.0, 0.0, 1.0, -1.0 };

    uint index = uint(hash21(ix, iz, seed) * 16777216.0) & 7u;
    return kGradX[index] * dx + kGradZ[index] * dz;
}

float perlin2(float x, float z, uint seed)
{
    int   ix = int(floor(x));
    int   iz = int(floor(z));
    float fx = x - float(ix);
    float fz = z - float(iz);
    float u  = fade(fx);
    float v  = fade(fz);

    float n00 = cornerDot(ix, iz, fx, fz, seed);
    float n10 = cornerDot(ix + 1, iz, fx - 1.0, fz, seed);
    float n01 = cornerDot(ix, iz + 1, fx, fz - 1.0, seed);
    float n11 = cornerDot(ix + 1, iz + 1, fx - 1.0, fz - 1.0, seed);

    float nx0 = n00 + (n10 - n00) * u;
    float nx1 = n01 + (n11 - n01) * u;
    float n   = nx0 + (nx1 - nx0) * v;
    return clamp(n, -1.0, 1.0);
}

// dirX, dirZ, strength in the same field as sampleGrassWind.
float3 SampleWindWS(float2 xz, float t, float4 field, float strength, float2 baseDir)
{
    float spatialFreq  = field.x;
    float temporalFreq = field.y;
    float deflection   = field.z;
    uint  seed         = asuint(field.w);

    float baseYaw = atan2(baseDir.x, baseDir.y);
    float scroll  = (temporalFreq != 0.0) ? (t * temporalFreq) : 0.0;
    float sx      = xz.x * spatialFreq + cos(baseYaw) * scroll;
    float sz      = xz.y * spatialFreq + sin(baseYaw) * scroll;
    float n       = perlin2(sx, sz, seed);
    float n2      = perlin2(sx + 19.2, sz + 7.7, seed ^ 0xB5297A4Du);

    float yaw = baseYaw + clamp(n, -1.0, 1.0) * deflection;
    float w   = clamp(n2, -1.0, 1.0) * 0.5 + 0.5;
    return float3(sin(yaw), cos(yaw), strength * lerp(0.45, 1.0, w));
}

#endif // WIND_FIELD_HLSLI
