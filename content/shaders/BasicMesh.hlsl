// Basic lit textured mesh shader — row-major matrices (matches Dark::Math::Matrix4f).
#pragma pack_matrix(row_major)

#include "Color.hlsli"
#ifndef ENCODE_SRGB
#define ENCODE_SRGB 0
#endif

cbuffer FrameConstants : register(b0)
{
    float4x4 worldViewProj;
    float4x4 world;
    float4   color;
    float3   lightDirWS; // direction toward the light
    float    ambientScale;
    float3   lightColor;
    float    _pad1;
    float3   cameraPos;
    float    lighting; // 1 = lit, 0 = albedo only
    float    normalScale;
    float    ao;
    float    alphaCutoff;
    float    alphaModeMask;
    float    emissive;
};

Texture2D    gAlbedo   : register(t0);
Texture2D    gNormal   : register(t1);
Texture2D    gOrm      : register(t2);
Texture2D    gEmissive : register(t3);
SamplerState gSamp     : register(s0);

#define SHADOW_T t4
#include "Shadow.hlsli"

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 tangent  : TANGENT;
};

struct PSInput
{
    float4 position  : SV_POSITION;
    float3 normalWS  : NORMAL;
    float2 uv        : TEXCOORD0;
    float3 worldPos  : TEXCOORD1;
    float4 tangentWS : TANGENT;
};

PSInput VSMain(VSInput input)
{
    PSInput o;
    float4 wp = mul(float4(input.position, 1.0f), world);
    o.position  = mul(float4(input.position, 1.0f), worldViewProj);
    o.normalWS  = mul(float4(input.normal, 0.0f), world).xyz;
    o.tangentWS = float4(mul(float4(input.tangent.xyz, 0.0f), world).xyz, input.tangent.w);
    o.uv        = input.uv;
    o.worldPos  = wp.xyz;
    return o;
}

float3 ApplyNormalMap(float3 nW, float4 tangentWS, float2 uv)
{
    nW = normalize(nW);
    if (length(tangentWS.xyz) < 1e-6f)
        return nW;
    float3 tW = normalize(tangentWS.xyz);
    tW = normalize(tW - nW * dot(nW, tW));
    float3 bW = cross(nW, tW) * tangentWS.w;
    float3 nt = gNormal.Sample(gSamp, uv).xyz * 2.0f - 1.0f;
    nt.xy *= normalScale;
    nt = normalize(nt);
    return normalize(nt.x * tW + nt.y * bW + nt.z * nW);
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
    float3 diffuse = ndotl * lightColor * albedo.rgb * shadow;
    float  emis = dot(gEmissive.Sample(gSamp, input.uv).rgb, float3(0.2126f, 0.7152f, 0.0722f)) * emissive;
    return float4(encodeSceneRgb(ambient + diffuse + emis), albedo.a);
}
