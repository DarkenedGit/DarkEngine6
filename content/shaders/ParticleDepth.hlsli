// GPU port of Particles/ParticleDepthCollision.h. Do not grow a second formula.
// particleReconstructWorld is ReconstructWorldPos (one ClampDepthForReconstruct inside).
#ifndef DE_PARTICLE_DEPTH_HLSLI
#define DE_PARTICLE_DEPTH_HLSLI

#pragma pack_matrix(row_major)

#include "GBuffer.hlsli"

// particleProjectUv. v = -ndcY * 0.5 + 0.5 matches VelocityUv. Outside [0, 1) misses.
struct ParticleUv
{
    bool  inside;
    float ndcX;
    float ndcY;
    float u;
    float v;
    float clipW;
};

ParticleUv particleProjectUv(float3 world, float4x4 viewProj)
{
    ParticleUv r;
    r.inside = false;
    r.ndcX = 0;
    r.ndcY = 0;
    r.u = 0;
    r.v = 0;
    float4 clip = mul(float4(world, 1), viewProj);
    r.clipW = clip.w;
    if (!(clip.w > 0))
        return r;
    r.ndcX = clip.x / clip.w;
    r.ndcY = clip.y / clip.w;
    r.u = r.ndcX * 0.5 + 0.5;
    r.v = -r.ndcY * 0.5 + 0.5;
    r.inside = r.u >= 0 && r.u < 1 && r.v >= 0 && r.v < 1;
    return r;
}

// Texel center. Inverse of particleProjectUv's Y flip.
float2 particleNdcFromPixel(int2 pix, float width, float height)
{
    float u = (float(pix.x) + 0.5) / width;
    float v = (float(pix.y) + 0.5) / height;
    return float2(u * 2.0 - 1.0, 1.0 - v * 2.0);
}

// particleDepthHit. Shell is min(0.75, max(thickness, speed * h)). viewZ <= near misses.
bool particleDepthHit(float particleViewZ, float surfaceViewZ, float nearZ, float bias, float thickness, float speed, float substepDt)
{
    float pad = speed * substepDt;
    if (pad < thickness)
        pad = thickness;
    if (pad > 0.75)
        pad = 0.75;
    if (!(particleViewZ > nearZ))
        return false;
    float delta = particleViewZ - surfaceViewZ;
    return delta >= -bias && delta <= pad;
}

// particleResolveViewZ
float particleResolveViewZ(float surfaceViewZ, float bias)
{
    return surfaceViewZ - bias;
}

// particleReflectVelocity. Normal is flipped so it opposes the incoming velocity.
float3 particleReflectVelocity(float3 velocity, float3 normal, float restitution, float friction)
{
    float lenSq = dot(normal, normal);
    if (!(lenSq > 1.0e-10))
        return velocity;
    float3 n = normal * rsqrt(lenSq);
    float vn = dot(velocity, n);
    if (vn > 0)
    {
        n = -n;
        vn = -vn;
    }
    float3 tangent = velocity - n * vn;
    return tangent * (1.0 - saturate(friction)) - n * vn * saturate(restitution);
}

bool particleDepthSampleOk(Texture2D<float> depthTex, int2 pix, int width, int height, out float depth)
{
    depth = 0;
    if (pix.x < 0 || pix.y < 0 || pix.x >= width || pix.y >= height)
        return false;
    depth = depthTex.Load(int3(pix, 0));
    return !IsSkyDepth(depth);
}

// particleDepthNormal from ReconstructWorldPos samples. Plus neighbor, else minus. Both sides invalid fails.
bool particleFiniteNormal(Texture2D<float> depthTex, int2 pix, int width, int height, float centerDepth, float4x4 invViewProj, float3 cameraPos, out float3 normal)
{
    normal = float3(0, 0, 0);
    int2 px = pix;
    int2 py = pix;
    float dx = 0;
    float dy = 0;
    if (!particleDepthSampleOk(depthTex, pix + int2(1, 0), width, height, dx))
    {
        if (!particleDepthSampleOk(depthTex, pix - int2(1, 0), width, height, dx))
            return false;
        px = pix - int2(1, 0);
    }
    else
        px = pix + int2(1, 0);
    if (!particleDepthSampleOk(depthTex, pix + int2(0, 1), width, height, dy))
    {
        if (!particleDepthSampleOk(depthTex, pix - int2(0, 1), width, height, dy))
            return false;
        py = pix - int2(0, 1);
    }
    else
        py = pix + int2(0, 1);

    float2 ndcC = particleNdcFromPixel(pix, (float)width, (float)height);
    float2 ndcX = particleNdcFromPixel(px, (float)width, (float)height);
    float2 ndcY = particleNdcFromPixel(py, (float)width, (float)height);
    float3 c = ReconstructWorldPos(ndcC.x, ndcC.y, centerDepth, invViewProj);
    float3 x = ReconstructWorldPos(ndcX.x, ndcX.y, dx, invViewProj);
    float3 y = ReconstructWorldPos(ndcY.x, ndcY.y, dy, invViewProj);
    float3 n = cross(x - c, y - c);
    float lenSq = dot(n, n);
    if (!(lenSq > 1.0e-10))
        return false;
    n *= rsqrt(lenSq);
    if (dot(n, cameraPos - c) < 0)
        n = -n;
    normal = n;
    return true;
}

uint particlePackOct(float3 n)
{
    float lenSq = dot(n, n);
    if (!(lenSq > 1.0e-10))
        n = float3(0, 0, 1);
    else
        n *= rsqrt(lenSq);
    float2 e = saturate(EncodeOct(n));
    uint x = (uint)round(e.x * 255.0);
    uint y = (uint)round(e.y * 255.0);
    return x | (y << 8);
}

#endif
