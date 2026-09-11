// Skinned opaque G-buffer. Row-major matrices.
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
};

cbuffer BonePalette : register(b2)
{
    float4x4 boneCurr[64];
    float4x4 bonePrev[64];
};

Texture2D    gAlbedo : register(t0);
SamplerState gSamp   : register(s0);

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
    float4 currClip : TEXCOORD1;
    float4 prevClip : TEXCOORD2;
};

float4 skinPosCurr(float3 p, uint4 j, float4 w)
{
    float4 hp = float4(p, 1.0f);
    return w.x * mul(hp, boneCurr[j.x]) + w.y * mul(hp, boneCurr[j.y])
         + w.z * mul(hp, boneCurr[j.z]) + w.w * mul(hp, boneCurr[j.w]);
}

float4 skinPosPrev(float3 p, uint4 j, float4 w)
{
    float4 hp = float4(p, 1.0f);
    return w.x * mul(hp, bonePrev[j.x]) + w.y * mul(hp, bonePrev[j.y])
         + w.z * mul(hp, bonePrev[j.z]) + w.w * mul(hp, bonePrev[j.w]);
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
    float4 posM  = skinPosCurr(input.position, input.joints, input.weights);
    float4 prevM = skinPosPrev(input.position, input.joints, input.weights);
    float3 nM    = skinNrm(input.normal, input.joints, input.weights);
    PSInput o;
    o.currClip = mul(posM, worldViewProj);
    o.prevClip = mul(prevM, prevWorldViewProj);
    o.position = o.currClip;
    o.normalWS = mul(float4(nM, 0.0f), world).xyz;
    o.uv       = input.uv;
    return o;
}

GBufferOut PSMain(PSInput input)
{
    GBufferOut o;
    float4 albedo = gAlbedo.Sample(gSamp, input.uv) * color;
    float3 n      = normalize(input.normalWS);
    o.albedo      = float4(albedo.rgb, color.a);
    float r = roughness;
    float m = metallic;
    if (r <= 0.0f && m <= 0.0f)
        r = 1.0f;
    o.attrib   = float4(EncodeOct(n), r, m);
    o.velocity = VelocityUv(input.currClip, input.prevClip);
    return o;
}
