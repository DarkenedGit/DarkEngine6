// Opaque mesh G-buffer: albedo + octahedral world normal. No lighting.
#pragma pack_matrix(row_major)

#include "GBuffer.hlsli"

cbuffer FrameConstants : register(b0)
{
    float4x4 worldViewProj;
    float4x4 world;
    float4   color;
};

Texture2D    gAlbedo : register(t0);
SamplerState gSamp   : register(s0);

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
};

PSInput VSMain(VSInput input)
{
    PSInput o;
    o.position = mul(float4(input.position, 1.0f), worldViewProj);
    o.normalWS = mul(float4(input.normal, 0.0f), world).xyz;
    o.uv       = input.uv;
    return o;
}

GBufferOut PSMain(PSInput input)
{
    GBufferOut o;
    float4 albedo = gAlbedo.Sample(gSamp, input.uv) * color;
    float3 n      = normalize(input.normalWS);
    o.albedo      = float4(albedo.rgb, 1.0f);
    o.attrib      = float4(EncodeOct(n), 1.0f, 0.0f);
    return o;
}
