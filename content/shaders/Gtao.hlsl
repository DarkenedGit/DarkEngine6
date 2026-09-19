// Jimenez 2016 GTAO (cosine-weighted horizon). Graphics-queue fullscreen PS.
// VS clipXY is NDC Y-up (bit-trick); UV is derived from clip, not SV_POSITION.
#pragma pack_matrix(row_major)

#include "GBuffer.hlsli"

#define GTAO_DIRECTIONS 4
#define GTAO_STEPS 4

cbuffer GtaoGpuParams : register(b0)
{
    float2   invSizeHalf;
    float2   invSizeFull;
    float4x4 invProj;
    float4x4 view;
    float4x4 reprojection;
    float    radius;
    float    power;
    float    intensity;
    float    thickness;
    float    nearZ;
    float    farZ;
    float    reset;
    float    frameOffset;
    float4   _pad64;
};

// PSGtao: t0 depth, t1 attrib
// PSUpsampleTemporal: t0 AoHalf, t1 depth, t2 attrib, t3 history, t4 velocity
// PSCompose: t0 authored MRT3, t1 AoFull
Texture2D    gTex0  : register(t0);
Texture2D    gTex1  : register(t1);
Texture2D    gTex2  : register(t2);
Texture2D    gTex3  : register(t3);
Texture2D    gTex4  : register(t4);
SamplerState gPoint : register(s0);
SamplerState gLin   : register(s1);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

struct DualOut
{
    float ao   : SV_TARGET0; // curved ssao (AoFull)
    float hist : SV_TARGET1; // raw vis (AoHistory)
};

PSInput VSMain(uint id : SV_VertexID)
{
    float2 pos = float2((id << 1) & 2, id & 2) * 2.0f - 1.0f;
    PSInput o;
    o.position = float4(pos, 0.0f, 1.0f);
    o.uv       = pos * float2(0.5f, -0.5f) + 0.5f;
    return o;
}

float interleavedGradientNoise(float2 pos)
{
    return frac(52.9829189f * frac(dot(pos, float2(0.06711056f, 0.00583715f))));
}

float3 ReconstructViewPos(float2 uv, float depth)
{
    float2 ndc  = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 clip = float4(ndc, ClampDepthForReconstruct(depth), 1.0f);
    float4 vs   = mul(clip, invProj);
    return vs.xyz / max(vs.w, 1e-6f);
}

float3 ViewNormalFromAttrib(float2 oct)
{
    float3 nWS = DecodeOct(oct);
    return normalize(mul(nWS, (float3x3)view));
}

float IntegrateArcCosWeight(float2 h, float n)
{
    float2 arc = -cos(2.0f * h - n) + cos(n) + 2.0f * h * sin(n);
    return 0.25f * (arc.x + arc.y);
}

float PSGtao(PSInput input) : SV_TARGET
{
    float2 uv    = input.uv;
    float  depth = gTex0.SampleLevel(gPoint, uv, 0).r;
    if (IsSkyDepth(depth))
        return 1.0f;

    float3 vPos = ReconstructViewPos(uv, depth);
    if (vPos.z <= nearZ || vPos.z >= farZ * 0.999f)
        return 1.0f;

    float3 viewN  = ViewNormalFromAttrib(gTex1.SampleLevel(gPoint, uv, 0).rg);
    float3 viewDir = normalize(-vPos);

    float pixelRadius = radius * (0.5f / max(abs(invProj._22), 1e-6f)) * (1.0f / invSizeHalf.y) / max(abs(vPos.z), 1e-3f);
    pixelRadius = clamp(pixelRadius, (float)GTAO_STEPS, 512.0f);
    float stepRadius = pixelRadius / (float)(GTAO_STEPS + 1);

    float2 pixel = uv / invSizeHalf;
    float  noiseOffset    = 0.25f * (float)(((int)pixel.y - (int)pixel.x) & 3);
    float  noiseDirection = interleavedGradientNoise(pixel + frameOffset);
    float  rayStart       = frac(noiseOffset + frameOffset * 0.6180339887f);

    const float twoOverR2 = 2.0f / max(radius * radius, 1e-6f);
    float vis = 0.0f;

    [unroll]
    for (int dir = 0; dir < GTAO_DIRECTIONS; ++dir)
    {
        float  angle    = ((float)dir + noiseDirection) * (3.14159265f / (float)GTAO_DIRECTIONS);
        float3 sliceDir = float3(cos(angle), sin(angle), 0.0f);
        float2 uvDir    = float2(sliceDir.x, -sliceDir.y); // view Y up, UV Y down
        float2 h        = float2(-1.0f, -1.0f);

        [unroll]
        for (int step = 0; step < GTAO_STEPS; ++step)
        {
            float2 uvOff = uvDir * invSizeHalf * max(stepRadius * ((float)step + rayStart), 1.0f + (float)step);
            float2 uv0   = uv + uvOff;
            float2 uv1   = uv - uvOff;

            float d0 = gTex0.SampleLevel(gPoint, uv0, 0).r;
            float d1 = gTex0.SampleLevel(gPoint, uv1, 0).r;
            if (!IsSkyDepth(d0))
            {
                float3 ds      = ReconstructViewPos(uv0, d0) - vPos;
                float  dsdt    = dot(ds, ds);
                float  invLen  = rsqrt(max(dsdt, 1e-8f));
                float  falloff = saturate(dsdt * twoOverR2);
                float  H       = dot(ds, viewDir) * invLen;
                h.x = (H > h.x) ? lerp(H, h.x, falloff) : lerp(H, h.x, thickness);
            }
            if (!IsSkyDepth(d1))
            {
                float3 dt      = ReconstructViewPos(uv1, d1) - vPos;
                float  dsdt    = dot(dt, dt);
                float  invLen  = rsqrt(max(dsdt, 1e-8f));
                float  falloff = saturate(dsdt * twoOverR2);
                float  H       = dot(dt, viewDir) * invLen;
                h.y = (H > h.y) ? lerp(H, h.y, falloff) : lerp(H, h.y, thickness);
            }
        }

        float3 planeN   = normalize(cross(sliceDir, viewDir));
        float3 tangent  = cross(viewDir, planeN);
        float3 projN    = viewN - planeN * dot(viewN, planeN);
        float  projLen  = length(projN);
        float  cosN     = clamp(dot(projN / max(projLen, 1e-6f), viewDir), -1.0f, 1.0f);
        float  n        = -sign(dot(projN, tangent)) * acos(cosN);

        h = acos(clamp(h, -1.0f, 1.0f));
        h.x = n + max(-h.x - n, -1.57079632f);
        h.y = n + min(h.y - n, 1.57079632f);

        vis += projLen * IntegrateArcCosWeight(h, n);
    }

    return saturate(vis / (float)GTAO_DIRECTIONS);
}

DualOut PSUpsampleTemporal(PSInput input)
{
    float2 uv    = input.uv;
    int2   texel = int2(input.position.xy);
    float  depthC = gTex1.Load(int3(texel, 0)).r;

    DualOut o;
    if (IsSkyDepth(depthC))
    {
        o.ao   = 1.0f;
        o.hist = 1.0f;
        return o;
    }

    float3 nC   = DecodeOct(gTex2.Load(int3(texel, 0)).rg);
    float  zC   = ReconstructViewPos(uv, depthC).z;
    float  vis  = 0.0f;
    float  wsum = 0.0f;
    float  visMin = 1.0f;
    float  visMax = 0.0f;

    [unroll]
    for (int y = 0; y <= 1; ++y)
    {
        [unroll]
        for (int x = 0; x <= 1; ++x)
        {
            float2 tapUv = uv + (float2((float)x, (float)y) - 0.5f) * invSizeHalf;
            float  ao    = gTex0.SampleLevel(gLin, tapUv, 0).r;
            float  d     = gTex1.SampleLevel(gPoint, tapUv, 0).r;
            if (IsSkyDepth(d))
                continue;
            float3 n     = DecodeOct(gTex2.SampleLevel(gPoint, tapUv, 0).rg);
            float  z     = ReconstructViewPos(tapUv, d).z;
            float  wZ    = exp(-abs(z - zC) / max(radius, 0.05f));
            float  wN    = pow(saturate(dot(n, nC)), 8.0f);
            float  w     = wZ * wN + 1e-4f;
            vis += ao * w;
            wsum += w;
            visMin = min(visMin, ao);
            visMax = max(visMax, ao);
        }
    }
    vis /= max(wsum, 1e-6f);

    float filtered = vis;
    if (reset <= 0.5f)
    {
        float2 vel    = gTex4.Load(int3(texel, 0)).rg;
        float2 histUv = uv - vel;
        float  hist   = vis;
        if (histUv.x > 0.0f && histUv.x < 1.0f && histUv.y > 0.0f && histUv.y < 1.0f)
            hist = gTex3.SampleLevel(gLin, histUv, 0).r;
        hist     = clamp(hist, visMin, visMax);
        filtered = lerp(hist, vis, 0.1f);
    }

    // History is raw vis; curve only AoFull so intensity/power do not double-apply.
    o.hist = saturate(filtered);
    o.ao   = lerp(1.0f, pow(saturate(filtered), power), intensity);
    return o;
}

float PSCompose(PSInput input) : SV_TARGET
{
    int2 texel = int2(input.position.xy);
    float authoredAo = gTex0.Load(int3(texel, 0)).r;
    float ssao       = gTex1.Load(int3(texel, 0)).r;
    return authoredAo * ssao;
}
