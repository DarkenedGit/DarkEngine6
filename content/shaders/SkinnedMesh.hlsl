// Skinned forward mesh — row-major (matches Dark::Math::Matrix4f).
#pragma pack_matrix(row_major)

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
};

cbuffer BonePalette : register(b2)
{
    float4x4 boneCurr[64];
    float4x4 bonePrev[64];
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
    uint4  joints   : BLENDINDICES;
    float4 weights  : BLENDWEIGHT;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normalWS : NORMAL;
    float2 uv       : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
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
    float4 wp   = mul(posM, world);
    o.position  = mul(posM, worldViewProj);
    o.normalWS  = mul(float4(nM, 0.0f), world).xyz;
    o.uv        = input.uv;
    o.worldPos  = wp.xyz;
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 albedo = gAlbedo.Sample(gSamp, input.uv) * color;
    if (lighting < 0.5f)
        return albedo;
    float3 n = normalize(input.normalWS);
    float3 l = normalize(lightDirWS);
    float  ndotl = saturate(dot(n, l));
    float  shadow = ComputeShadow(input.worldPos, cameraPos);
    float3 ambient = ambientScale * albedo.rgb;
    float3 diffuse = ndotl * lightColor * albedo.rgb * shadow;
    return float4(ambient + diffuse, albedo.a);
}
