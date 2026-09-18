// Graphics-queue IBL bake: equirect→cube, cosine irradiance, Karis GGX prefilter.
#pragma pack_matrix(row_major)

#include "IblSampling.hlsli"

cbuffer BakeConstants : register(b0)
{
    uint  faceIndex;
    float roughness;
    uint  sampleCount;
    uint  _pad;
};

SamplerState gLinear : register(s0);

#if defined(IBL_SRC_EQUIRECT)
Texture2D gSrc : register(t0);
#else
TextureCube gSrcCube : register(t0);
#endif

struct PSInput
{
    float4 position : SV_POSITION;
    float2 clipXY   : TEXCOORD0; // Y-up NDC; do not rebuild from SV_POSITION
};

PSInput VSMain(uint id : SV_VertexID)
{
    float2 pos = float2((id << 1) & 2, id & 2) * 2.0f - 1.0f;
    PSInput o;
    o.position = float4(pos, 0.0f, 1.0f);
    o.clipXY   = pos;
    return o;
}

#if defined(IBL_SRC_EQUIRECT)
float4 PSEquirect(PSInput input) : SV_TARGET
{
    float3 dir = CubeFaceDir(input.clipXY, faceIndex);
    float2 uv  = DirToEquirectUv(dir);
    float3 c   = gSrc.SampleLevel(gLinear, uv, 0).rgb;
    return float4(c, 1.0f);
}
#else
float4 PSIrradiance(PSInput input) : SV_TARGET
{
    float3 n = CubeFaceDir(input.clipXY, faceIndex);
    float3 e = 0.0.xxx;
    uint   nSamples = max(sampleCount, 1u);
    for (uint i = 0u; i < nSamples; ++i)
    {
        float2 xi = Hammersley(i, nSamples);
        float3 l  = CosineSampleHemisphere(xi, n);
        // pdf = NdotL/π cancels the cosine; accumulate Li and scale π once.
        e += gSrcCube.SampleLevel(gLinear, l, 0).rgb;
    }
    e *= DE_PBR_PI / float(nSamples);
    return float4(e, 1.0f);
}

float4 PSPrefilter(PSInput input) : SV_TARGET
{
    float3 n = CubeFaceDir(input.clipXY, faceIndex);
    float3 v = n;
    float3 pre = 0.0.xxx;
    float  weight = 0.0f;
    uint   nSamples = max(sampleCount, 1u);
    for (uint i = 0u; i < nSamples; ++i)
    {
        float2 xi = Hammersley(i, nSamples);
        float3 h  = ImportanceSampleGGX(xi, n, roughness);
        float3 l  = normalize(2.0f * dot(v, h) * h - v);
        float  ndotL = saturate(dot(n, l));
        if (ndotL > 0.0f)
        {
            pre    += gSrcCube.SampleLevel(gLinear, l, 0).rgb * ndotL;
            weight += ndotL;
        }
    }
    pre = weight > 0.0f ? pre / weight : 0.0.xxx;
    return float4(pre, 1.0f);
}
#endif
