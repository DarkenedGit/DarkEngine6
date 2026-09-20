#ifndef DE_SSR_MARCH_HLSLI
#define DE_SSR_MARCH_HLSLI

#include "Depth.hlsli"

#define kSsrRayLength 256.0f
#define kSsrMaxSteps 32
#define kSsrWaterMaxSteps 16

#define SSR_HIT 0
#define SSR_MISS_SKY 1
#define SSR_MISS_OFFSCREEN 2
#define SSR_MISS_MAX 3

struct SsrHit
{
    float3 radiance;
    float  conf;
    uint   kind;
};

SsrHit SsrMakeMiss(uint kind)
{
    SsrHit h;
    h.radiance = 0.0.xxx;
    h.conf     = 0.0f;
    h.kind     = kind;
    return h;
}

float SsrEdgeFade(float2 uv, float edgeFade)
{
    float e = max(edgeFade, 1.0e-5f);
    float2 fade = saturate(min(uv, 1.0f - uv) / e);
    return fade.x * fade.y;
}

float SsrFresnelF0(float metallic, float NdotV)
{
    float F0   = lerp(0.04f, 1.0f, metallic);
    float oneL = saturate(1.0f - NdotV);
    return F0 + (1.0f - F0) * oneL * oneL * oneL * oneL * oneL;
}

SsrHit SsrMarch(
    float3 worldPos,
    float3 R,
    float NdotV,
    float roughness,
    float metallic,
    float2 resolution,
    Texture2D depthTex,
    Texture2D colorTex,
    SamplerState samp,
    float4x4 viewProj,
    float nearZ,
    float thickness,
    float stridePx,
    float edgeFade,
    float maxRoughness,
    int maxSteps)
{
    float3 P0 = worldPos;
    float3 P1 = worldPos + R * kSsrRayLength;
    float4 clip0 = mul(float4(P0, 1.0f), viewProj);
    float4 clip1 = mul(float4(P1, 1.0f), viewProj);
    float wMin = max(nearZ, 1.0e-3f);

    if (clip0.w <= wMin && clip1.w <= wMin)
        return SsrMakeMiss(SSR_MISS_SKY);

    if (clip1.w <= wMin)
    {
        float denom = clip0.w - clip1.w;
        if (abs(denom) < 1.0e-8f)
            return SsrMakeMiss(SSR_MISS_SKY);
        float tClip = (clip0.w - wMin) / denom;
        P1    = lerp(P0, P1, saturate(tClip));
        clip1 = mul(float4(P1, 1.0f), viewProj);
    }
    if (clip0.w <= wMin)
    {
        float denom = clip1.w - clip0.w;
        if (abs(denom) < 1.0e-8f)
            return SsrMakeMiss(SSR_MISS_SKY);
        float tClip = (wMin - clip0.w) / denom;
        P0    = lerp(P0, P1, saturate(tClip));
        clip0 = mul(float4(P0, 1.0f), viewProj);
    }
    if (clip0.w <= wMin || clip1.w <= wMin)
        return SsrMakeMiss(SSR_MISS_SKY);

    float2 uv0 = clip0.xy / clip0.w * float2(0.5f, -0.5f) + 0.5f;
    float2 uv1 = clip1.xy / clip1.w * float2(0.5f, -0.5f) + 0.5f;
    float  lenPx = length((uv1 - uv0) * resolution);
    if (lenPx < 1.0f)
        return SsrMakeMiss(SSR_MISS_SKY);

    float step = max(stridePx, 1.0f);
    float2 stepUv = (uv1 - uv0) / lenPx * step;
    int steps = max(maxSteps, 1);

    [loop]
    for (int i = 1; i <= steps; ++i)
    {
        float t = step * (float)i / lenPx;
        if (t >= 1.0f)
            return SsrMakeMiss(SSR_MISS_MAX);

        float2 uv = uv0 + stepUv * (float)i;
        if (uv.x < edgeFade || uv.y < edgeFade || uv.x > (1.0f - edgeFade) || uv.y > (1.0f - edgeFade))
            return SsrMakeMiss(SSR_MISS_OFFSCREEN);

        float sceneD = depthTex.SampleLevel(samp, uv, 0).r;
        if (IsSkyDepth(sceneD))
            return SsrMakeMiss(SSR_MISS_SKY);

        float rayViewZ   = 1.0f / lerp(1.0f / clip0.w, 1.0f / clip1.w, saturate(t));
        float sceneViewZ = LinearizeViewZ(sceneD, nearZ);
        if (rayViewZ >= sceneViewZ && (rayViewZ - sceneViewZ) <= thickness)
        {
            float tMiss = t - step / lenPx;
            float tHit  = t;
            [unroll]
            for (int r = 0; r < 4; ++r)
            {
                float  tMid  = 0.5f * (tMiss + tHit);
                float2 uvMid = uv0 + (uv1 - uv0) * tMid;
                float  midD  = depthTex.SampleLevel(samp, uvMid, 0).r;
                if (IsSkyDepth(midD))
                {
                    tMiss = tMid;
                    continue;
                }
                float midRayZ   = 1.0f / lerp(1.0f / clip0.w, 1.0f / clip1.w, saturate(tMid));
                float midSceneZ = LinearizeViewZ(midD, nearZ);
                if (midRayZ >= midSceneZ && (midRayZ - midSceneZ) <= thickness)
                    tHit = tMid;
                else
                    tMiss = tMid;
            }

            float2 uvHit = uv0 + (uv1 - uv0) * tHit;
            SsrHit h;
            h.radiance = colorTex.SampleLevel(samp, uvHit, 0).rgb;
            h.conf     = SsrEdgeFade(uvHit, edgeFade) * SsrFresnelF0(metallic, NdotV)
                * saturate(1.0f - roughness / max(maxRoughness, 1.0e-5f))
                * (1.0f - (float)i / (float)steps);
            h.kind = SSR_HIT;
            return h;
        }
    }

    return SsrMakeMiss(SSR_MISS_MAX);
}

#endif
