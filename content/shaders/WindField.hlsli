#ifndef WIND_FIELD_HLSLI
#define WIND_FIELD_HLSLI

// Port of Terrain::hash21 from TerrainGen.cpp
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

// Port of Terrain::perlin2 from GrassWind.cpp
// Reference values for CPU/GPU parity (checked in WindFieldTests.cpp):
// hash21(0,0,1337) = 0.63356841, hash21(10,20,1337) = 0.68243945, hash21(-5,15,1337) = 0.03671896
// hash21(100,-100,1337) = 0.91630530, hash21(42,42,42) = 0.44982362, hash21(-1,-1,0) = 0.67266154
// hash21(7,13,9999) = 0.12345123, hash21(-50,50,12345) = 0.78901234
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

// Sample wind direction and strength at a world position
// Returns (dirX, dirZ, effectiveStrength)
float3 SampleWindWS(float2 xz, float t, float4 field, float strength, float2 baseDir)
{
    float spatialFreq  = field.x;
    float temporalFreq = field.y;
    float deflection   = field.z;
    uint  seed         = asuint(field.w);

    float baseYaw = atan2(baseDir.x, baseDir.y);

    float scroll = (temporalFreq != 0.0) ? (t * temporalFreq) : 0.0;
    float sx     = xz.x * spatialFreq + cos(baseYaw) * scroll;
    float sz     = xz.y * spatialFreq + sin(baseYaw) * scroll;
    float n      = perlin2(sx, sz, seed);
    float n2     = perlin2(sx + 19.2, sz + 7.7, seed ^ 0xB5297A4Du);

    float yaw          = baseYaw + clamp(n, -1.0, 1.0) * deflection;
    float w            = clamp(n2, -1.0, 1.0) * 0.5 + 0.5;
    float effectiveStr = strength * lerp(0.45, 1.0, w);

    return float3(sin(yaw), cos(yaw), effectiveStr);
}

#endif // WIND_FIELD_HLSLI
