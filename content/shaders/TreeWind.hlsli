#ifndef TREE_WIND_HLSLI
#define TREE_WIND_HLSLI

#include "WindField.hlsli"

// b1 layout is FoliageViewConstants. kindA/B/C are that kind's row.
// wp is the instance position. The pivot stays put. The crown bends in world metres.

float4 SmoothCurve(float4 x)
{
    return x * x * (3.0 - 2.0 * x);
}

float4 TriWave(float4 x)
{
    return abs(frac(x + 0.5) * 2.0 - 1.0);
}

float WindFade(float metres, float start, float end)
{
    return saturate((end - metres) / max(end - start, 1e-3));
}

float3 TreeWindOffset(float3 wp, float3 origin, float scaleY, float3 nWS, float4 wind, float4 field, float4 kA, float4 kB, float4 kC, float3 eye)
{
    float eyeDistance = distance(origin, eye);
    float bendW       = WindFade(eyeDistance, kB.w, kC.x);
    if (bendW <= 0.0 || kC.z <= 0.0)
        return float3(0.0, 0.0, 0.0);

    float  detailW = WindFade(eyeDistance, kB.y, kB.z);
    float3 sampled = SampleWindWS(origin.xz, wind.w, field, wind.z, wind.xy);
    float2 dir     = sampled.xy * sampled.z;

    float3 rel = wp - origin;
    float  h   = saturate(rel.y / max(kC.y * scaleY, 1e-3));
    float  bf  = h * h;

    float3 off = float3(0.0, 0.0, 0.0);
    off.xz     = dir * (kA.y / max(kA.x, 1e-3)) * bf * bendW;

    // Keep the branch length so the crown dips instead of stretching. A vertex on the pivot has no direction.
    float  len     = length(rel);
    float3 bent    = rel + off;
    float  bentLen = dot(bent, bent);
    if (len > 1e-4 && bentLen > 1e-8)
        off = normalize(bent) * len - rel;

    float  radial = saturate(length(rel.xz) / max(kC.y * scaleY * 0.35, 1e-3));
    float  objPh  = origin.x + origin.y + origin.z;
    float  vtxPh  = rel.x + rel.y + rel.z;
    float4 wave   = SmoothCurve(TriWave((wind.w * kB.x + float2(vtxPh + objPh, objPh)).xxyy * float4(1.975, 0.793, 0.375, 0.193)));
    float2 waveSum = wave.xz + wave.yw;
    float  amp     = sampled.z * detailW * bendW;

    off.xz += waveSum.x * kA.w * radial * h * nWS.xz * amp;
    off.y += waveSum.y * kA.z * radial * h * amp;
    return off;
}

#endif // TREE_WIND_HLSLI
