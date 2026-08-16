// Basic lit textured mesh shader — row-major matrices (matches Dark::Math::Matrix4f).
#pragma pack_matrix(row_major)

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
    float    _pad2;
};

Texture2D    gAlbedo : register(t0);
SamplerState gSamp   : register(s0);

#define SHADOW_T t1
#include "Shadow.hlsli"

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normalWS : NORMAL;
    float2 uv       : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
};

PSInput VSMain(VSInput input)
{
    PSInput o;
    float4 wp = mul(float4(input.position, 1.0f), world);
    o.position = mul(float4(input.position, 1.0f), worldViewProj);
    o.normalWS = mul(float4(input.normal, 0.0f), world).xyz;
    o.uv       = input.uv;
    o.worldPos = wp.xyz;
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 albedo = gAlbedo.Sample(gSamp, input.uv) * color;
    float3 n = normalize(input.normalWS);
    float3 l = normalize(lightDirWS);
    float  ndotl = saturate(dot(n, l));
    float  shadow = ComputeShadow(input.worldPos, cameraPos);
    float3 ambient = ambientScale * albedo.rgb;
    float3 diffuse = ndotl * lightColor * albedo.rgb * shadow;
    return float4(ambient + diffuse, albedo.a);
}
