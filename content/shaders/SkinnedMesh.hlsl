// Skinned forward mesh — row-major (matches Dark::Math::Matrix4f).
#pragma pack_matrix(row_major)

#include "Color.hlsli"
#include "PbrLighting.hlsli"
#ifndef ENCODE_SRGB
#define ENCODE_SRGB 0
#endif

cbuffer FrameConstants : register(b0)
{
    float4x4 worldViewProj;
    float4x4 world;
    float4   color;
    float3   lightDirWS;
    float    ambientScale;
    float3   lightColor;
    float    _pad1;
    float3   cameraPos;
    float    lighting;
    float    normalScale;
    float    ao;
    float    alphaCutoff;
    float    alphaModeMask;
    float    emissive;
};

cbuffer BonePalette : register(b2)
{
    float4x4 boneCurr[64];
    float4x4 bonePrev[64];
};

Texture2D    gAlbedo   : register(t0);
Texture2D    gNormal   : register(t1);
Texture2D    gOrm      : register(t2);
Texture2D    gEmissive : register(t3);
SamplerState gSamp     : register(s0);

#include "NormalMap.hlsli"

#define SHADOW_T t4
#include "Shadow.hlsli"

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 tangent  : TANGENT;
    uint4  joints   : BLENDINDICES;
    float4 weights  : BLENDWEIGHT;
};

struct PSInput
{
    float4 position  : SV_POSITION;
    float3 normalWS  : NORMAL;
    float2 uv        : TEXCOORD0;
    float3 worldPos  : TEXCOORD1;
    float4 tangentWS : TANGENT;
};

float4 skinPos(float3 p, uint4 j, float4 w)
{
    float4 hp = float4(p, 1.0f);
    return w.x * mul(hp, boneCurr[j.x]) + w.y * mul(hp, boneCurr[j.y])
         + w.z * mul(hp, boneCurr[j.z]) + w.w * mul(hp, boneCurr[j.w]);
}

float3 skinNrm(float3 n, uint4 j, float4 w)
{
    float3 r = w.x * mul(n, (float3x3)boneCurr[j.x])
             + w.y * mul(n, (float3x3)boneCurr[j.y])
             + w.z * mul(n, (float3x3)boneCurr[j.z])
             + w.w * mul(n, (float3x3)boneCurr[j.w]);
    return r;
}

PSInput VSMain(VSInput input)
{
    PSInput o;
    float4 posM = skinPos(input.position, input.joints, input.weights);
    float3 nM   = skinNrm(input.normal, input.joints, input.weights);
    float3 tM   = skinNrm(input.tangent.xyz, input.joints, input.weights);
    float4 wp   = mul(posM, world);
    o.position  = mul(posM, worldViewProj);
    o.normalWS  = mul(float4(nM, 0.0f), world).xyz;
    o.tangentWS = float4(mul(float4(tM, 0.0f), world).xyz, input.tangent.w);
    o.uv        = input.uv;
    o.worldPos  = wp.xyz;
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 albedoS = gAlbedo.Sample(gSamp, input.uv);
    if (alphaModeMask > 0.5f)
        clip(albedoS.a - alphaCutoff);
    float4 albedo = float4(albedoS.rgb * color.rgb, albedoS.a * color.a);
    if (lighting < 0.5f)
        return float4(encodeSceneRgb(albedo.rgb), albedo.a);
    float3 n = ApplyNormalMap(input.normalWS, input.tangentWS, input.uv);
    float3 l = normalize(lightDirWS);
    float  ndotl = saturate(dot(n, l));
    float  shadow = ComputeShadow(input.worldPos, cameraPos);
    float  aoTerm = saturate(gOrm.Sample(gSamp, input.uv).r * ao);
    float3 ambient = ambientScale * albedo.rgb * aoTerm;
    float3 diffuse = ndotl * lightColor * albedo.rgb * shadow * (1.0f / DE_PBR_PI);
    float  emis = dot(gEmissive.Sample(gSamp, input.uv).rgb, float3(0.2126f, 0.7152f, 0.0722f)) * emissive;
    return float4(encodeSceneRgb(ambient + diffuse + emis), albedo.a);
}
