#ifndef TREE_WIND_HLSLI
#define TREE_WIND_HLSLI

#include "WindField.hlsli"

float4 SmoothCurve(float4 x)
{
    return x * x * (3.0 - 2.0 * x);
}

float4 TriWave(float4 x)
{
    return abs(frac(x + 0.5) * 2.0 - 1.0);
}

float WindFade(float d, float s, float e)
{
    return saturate((e - d) / max(e - s, 1e-3));
}

// Tree wind offset in world space
// wp: world position after instance transform
// origin: instance world position (row 3 of instance matrix)
// scaleY: instance Y-axis scale (length of row 1)
// nWS: world-space normal
// wind: windCur or windPrev (x=sin(heading), y=cos(heading), z=strength*gust, w=time)
// field: spatialFreq, temporalFreq, deflection, asfloat(seed)
// kA: stiffness, mainBendMetres*influence, branchAmp*influence, edgeAmp*influence
// kB: flutterFreq, detailFadeStart, detailFadeEnd, bendFadeStart
// kC: bendFadeEnd, kindHeight, influence, pad
// eye: camera position xyz
float3 TreeWindOffset(float3 wp, float3 origin, float scaleY, float3 nWS, float4 wind, float4 field,
                      float4 kA, float4 kB, float4 kC, float3 eye)
{
    float d       = distance(origin, eye);
    float bendW   = WindFade(d, kB.w, kC.x);
    if (bendW <= 0.0 || kC.z <= 0.0)
        return 0;

    float  detailW = WindFade(d, kB.y, kB.z);
    float3 w       = SampleWindWS(origin.xz, wind.w, field, wind.z, wind.xy);
    float2 dir     = w.xy * w.z;

    float3 rel = wp - origin;
    float  h   = saturate(rel.y / max(kC.y * scaleY, 1e-3));
    float  bf  = h * h;

    float3 off = 0;
    off.xz     = dir * (kA.y / kA.x) * bf * bendW;

    // Length-preserve so crown dips instead of stretching
    float  len  = length(rel);
    float3 bent = rel + off;
    off         = normalize(bent) * len - rel;

    // Detail (procedural weights, no vertex colour in v1)
    float radial = saturate(length(rel.xz) / max(kC.y * scaleY * 0.35, 1e-3));
    float objPh  = dot(origin, 1.0);
    float vtxPh  = dot(rel, 1.0);
    float4 wv    = SmoothCurve(TriWave((wind.w * kB.x + float2(vtxPh + objPh, objPh)).xxyy * float4(1.975, 0.793, 0.375, 0.193)));
    float2 s     = wv.xz + wv.yw;
    float  amp   = w.z * detailW * bendW;

    off.xz += s.x * kA.w * radial * h * nWS.xz * amp;
    off.y += s.y * kA.z * radial * h * amp;

    return off;
}

#endif // TREE_WIND_HLSLI
