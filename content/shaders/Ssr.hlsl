// Graphics-queue SSR: half-res DDA, full-res upsample + temporal, HDR downsample.
// VS clipXY is NDC Y-up (bit-trick); UV is derived from clip, not SV_POSITION.
#pragma pack_matrix(row_major)

#include "GBuffer.hlsli"
#include "SsrMarch.hlsli"

cbuffer SsrGpuParams : register(b0)
{
    float2   invSizeHalf;
    float2   invSizeFull;
    float4x4 invViewProj;
    float4x4 viewProj;
    float4x4 reprojection;
    float    nearZ;
    float    thickness;
    float    stride;
    float    maxRoughness;
    float    edgeFade;
    float    reset;
    float    frameOffset;
    float    _pad;
    float3   cameraPos;
    float    _padCam;
};

// PSTrace: t0 depth, t1 attrib, t2 sceneColor
// PSUpsampleTemporal: t0 SsrHalf, t1 depth, t2 attrib, t3 history, t4 velocity
// PSDownsample: t0 full-res HDR
// PSDebugConf: t0 SsrFull
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
    float4 full : SV_TARGET0;
    float4 hist : SV_TARGET1;
};

PSInput VSMain(uint id : SV_VertexID)
{
    float2 pos = float2((id << 1) & 2, id & 2) * 2.0f - 1.0f;
    PSInput o;
    o.position = float4(pos, 0.0f, 1.0f);
    o.uv       = pos * float2(0.5f, -0.5f) + 0.5f;
    return o;
}

float4 PSDownsample(PSInput input) : SV_TARGET
{
    float2 uv = input.uv;
    float2 t  = invSizeFull;
    float4 c  = gTex0.SampleLevel(gLin, uv + t * float2(-0.5f, -0.5f), 0);
    c += gTex0.SampleLevel(gLin, uv + t * float2(0.5f, -0.5f), 0);
    c += gTex0.SampleLevel(gLin, uv + t * float2(-0.5f, 0.5f), 0);
    c += gTex0.SampleLevel(gLin, uv + t * float2(0.5f, 0.5f), 0);
    return c * 0.25f;
}

float4 PSTrace(PSInput input) : SV_TARGET
{
    float2 uv    = input.uv;
    float  depth = gTex0.SampleLevel(gPoint, uv, 0).r;
    if (IsSkyDepth(depth))
        return float4(0.0f, 0.0f, 0.0f, 0.0f);

    float4 attrib    = gTex1.SampleLevel(gPoint, uv, 0);
    float  roughness = attrib.b;
    float  metallic  = attrib.a;
    if (roughness > maxRoughness)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);

    float2 ndc      = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float3 worldPos = ReconstructWorldPos(ndc.x, ndc.y, depth, invViewProj);
    float3 N        = DecodeOct(attrib.rg);
    float3 V        = normalize(cameraPos - worldPos);
    float3 R        = reflect(-V, N);
    float  NdotV    = saturate(dot(N, V));
    float2 resolution = float2(1.0f / max(invSizeHalf.x, 1.0e-8f), 1.0f / max(invSizeHalf.y, 1.0e-8f));

    SsrHit h = SsrMarch(worldPos, R, NdotV, roughness, metallic, resolution, gTex0, gTex2, gPoint, viewProj, nearZ, thickness,
                        stride, edgeFade, maxRoughness, kSsrMaxSteps);
    return float4(h.radiance, h.conf);
}

DualOut PSUpsampleTemporal(PSInput input)
{
    float2 uv     = input.uv;
    int2   texel  = int2(input.position.xy);
    float  depthC = gTex1.Load(int3(texel, 0)).r;

    DualOut o;
    if (IsSkyDepth(depthC))
    {
        o.full = float4(0.0f, 0.0f, 0.0f, 0.0f);
        o.hist = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return o;
    }

    float3 nC = DecodeOct(gTex2.Load(int3(texel, 0)).rg);
    float  zC = LinearizeViewZ(depthC, nearZ);

    float3 rad    = 0.0.xxx;
    float  conf   = 0.0f;
    float  wsum   = 0.0f;
    float3 radMin = float3(1.0e20f, 1.0e20f, 1.0e20f);
    float3 radMax = float3(-1.0e20f, -1.0e20f, -1.0e20f);

    [unroll]
    for (int y = 0; y <= 1; ++y)
    {
        [unroll]
        for (int x = 0; x <= 1; ++x)
        {
            float2 tapUv = uv + (float2((float)x, (float)y) - 0.5f) * invSizeHalf;
            float4 s     = gTex0.SampleLevel(gLin, tapUv, 0);
            float  d     = gTex1.SampleLevel(gPoint, tapUv, 0).r;
            if (IsSkyDepth(d))
                continue;
            float3 n  = DecodeOct(gTex2.SampleLevel(gPoint, tapUv, 0).rg);
            float  z  = LinearizeViewZ(d, nearZ);
            float  wZ = exp(-abs(z - zC) / max(thickness, 0.05f));
            float  wN = pow(saturate(dot(n, nC)), 8.0f);
            float  w  = wZ * wN + 1.0e-4f;
            rad += s.rgb * w;
            conf += s.a * w;
            wsum += w;
            radMin = min(radMin, s.rgb);
            radMax = max(radMax, s.rgb);
        }
    }
    if (wsum < 1.0e-5f)
    {
        float4 s = gTex0.SampleLevel(gLin, uv, 0);
        rad      = s.rgb;
        conf     = s.a;
        radMin   = s.rgb;
        radMax   = s.rgb;
    }
    else
    {
        rad /= wsum;
        conf /= wsum;
    }

    float4 current = float4(rad, saturate(conf));
    if (reset <= 0.5f)
    {
        float2 vel    = gTex4.Load(int3(texel, 0)).rg;
        float2 histUv = uv - vel;
        if (abs(vel.x) + abs(vel.y) < 1.0e-8f)
        {
            float2 ndc      = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
            float3 worldPos = ReconstructWorldPos(ndc.x, ndc.y, depthC, invViewProj);
            float4 currClip = mul(float4(worldPos, 1.0f), viewProj);
            float4 prevClip = mul(currClip, reprojection);
            float  pw       = max(abs(prevClip.w), 1.0e-5f);
            histUv          = prevClip.xy / pw * float2(0.5f, -0.5f) + 0.5f;
        }
        float4 hist = current;
        if (histUv.x > 0.0f && histUv.x < 1.0f && histUv.y > 0.0f && histUv.y < 1.0f)
            hist = gTex3.SampleLevel(gLin, histUv, 0);
        hist.rgb   = clamp(hist.rgb, radMin, radMax);
        current.rgb = lerp(hist.rgb, current.rgb, 0.1f);
        current.a   = min(current.a, hist.a);
    }

    o.full = current;
    o.hist = current;
    return o;
}

float PSDebugConf(PSInput input) : SV_TARGET
{
    int2 texel = int2(input.position.xy);
    return gTex0.Load(int3(texel, 0)).a;
}
