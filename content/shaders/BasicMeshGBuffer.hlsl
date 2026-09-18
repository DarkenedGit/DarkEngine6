// Opaque mesh G-buffer: albedo + octahedral world normal. No lighting.
#pragma pack_matrix(row_major)

#include "GBuffer.hlsli"

cbuffer FrameConstants : register(b0)
{
    float4x4 worldViewProj;
    float4x4 world;
    float4   color;
    float4x4 prevWorldViewProj;
    float    roughness;
    float    metallic;
    float    ao;
    float    normalScale;
    float    alphaCutoff;
    float    alphaModeMask;
};

Texture2D    gAlbedo   : register(t0);
Texture2D    gNormal   : register(t1);
Texture2D    gOrm      : register(t2);
Texture2D    gEmissive : register(t3);
SamplerState gSamp     : register(s0);

#include "NormalMap.hlsli"

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
    float4 currClip  : TEXCOORD1;
    float4 prevClip  : TEXCOORD2;
    float4 tangentWS : TANGENT;
};

PSInput VSMain(VSInput input)
{
    float4 wp = float4(input.position, 1.0f);
    PSInput o;
    o.currClip  = mul(wp, worldViewProj);
    o.prevClip  = mul(wp, prevWorldViewProj);
    o.position  = o.currClip;
    o.normalWS  = mul(float4(input.normal, 0.0f), world).xyz;
    o.tangentWS = float4(mul(float4(input.tangent.xyz, 0.0f), world).xyz, input.tangent.w);
    o.uv        = input.uv;
    return o;
}

GBufferOut PSMain(PSInput input)
{
    float4 albedoS = gAlbedo.Sample(gSamp, input.uv);
    if (alphaModeMask > 0.5f)
        clip(albedoS.a - alphaCutoff);

    float3 albedo = albedoS.rgb * color.rgb;
    float3 n      = ApplyNormalMap(input.normalWS, input.tangentWS, input.uv);

    float4 orm   = gOrm.Sample(gSamp, input.uv);
    float  rough = saturate(orm.g * roughness);
    float  metal = saturate(orm.b * metallic);
    float  aoOut = saturate(orm.r * ao);

    float emis = dot(gEmissive.Sample(gSamp, input.uv).rgb, float3(0.2126f, 0.7152f, 0.0722f)) * color.a;

    GBufferOut o;
    o.albedo   = float4(albedo, emis);
    o.attrib   = float4(EncodeOct(n), rough, metal);
    o.velocity = VelocityUv(input.currClip, input.prevClip);
    o.ao       = aoOut;
    return o;
}
