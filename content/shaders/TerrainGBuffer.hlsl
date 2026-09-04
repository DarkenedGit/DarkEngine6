// Opaque terrain G-buffer: splat albedo + octahedral world normal. No lighting/fog/shadow.
#pragma pack_matrix(row_major)

#include "GBuffer.hlsli"

cbuffer FrameConstants : register(b0)
{
    float4x4 worldViewProj;
    float4x4 world;
    float4   color;
    float4   layerTiling;
    float4x4 prevWorldViewProj;
};

Texture2D    gLayer0 : register(t0);
Texture2D    gLayer1 : register(t1);
Texture2D    gLayer2 : register(t2);
Texture2D    gLayer3 : register(t3);
Texture2D    gSplat  : register(t4);
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
    float4 currClip : TEXCOORD1;
    float4 prevClip : TEXCOORD2;
};

PSInput VSMain(VSInput input)
{
    float4 wp = float4(input.position, 1.0f);
    PSInput o;
    o.currClip = mul(wp, worldViewProj);
    o.prevClip = mul(wp, prevWorldViewProj);
    o.position = o.currClip;
    o.normalWS = mul(float4(input.normal, 0.0f), world).xyz;
    o.uv       = input.uv;
    return o;
}

GBufferOut PSMain(PSInput input)
{
    float4 splat = gSplat.Sample(gSamp, input.uv);
    float  wsum  = splat.r + splat.g + splat.b + splat.a + 1e-5f;
    splat /= wsum;

    float4 albedo =
          splat.r * gLayer0.Sample(gSamp, input.uv * layerTiling.x)
        + splat.g * gLayer1.Sample(gSamp, input.uv * layerTiling.y)
        + splat.b * gLayer2.Sample(gSamp, input.uv * layerTiling.z)
        + splat.a * gLayer3.Sample(gSamp, input.uv * layerTiling.w);
    albedo *= color;

    GBufferOut o;
    float3 n = normalize(input.normalWS);
    o.albedo   = float4(albedo.rgb, 1.0f);
    o.attrib   = float4(EncodeOct(n), 1.0f, 0.0f);
    o.velocity = VelocityUv(input.currClip, input.prevClip);
    return o;
}
