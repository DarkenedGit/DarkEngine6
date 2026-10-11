// Instanced opaque mesh G-buffer. World is already baked on the instance.
#pragma pack_matrix(row_major)

#include "GBuffer.hlsli"
#include "MeshInstance.hlsli"

cbuffer BatchConstants : register(b0)
{
    float4x4 viewProj;
    float4x4 prevViewProj;
    float    roughness;
    float    metallic;
    float    ao;
    float    normalScale;
    float    alphaCutoff;
    float    alphaModeMask;
    uint     baseIndex;
    uint     pad;
};

StructuredBuffer<MeshInstanceGpu> gInstances : register(t4);
StructuredBuffer<uint>            gIndices   : register(t5);

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
    uint   iid      : SV_InstanceID;
};

struct PSInput
{
    float4 position  : SV_POSITION;
    float3 normalWS  : NORMAL;
    float2 uv        : TEXCOORD0;
    float4 currClip  : TEXCOORD1;
    float4 prevClip  : TEXCOORD2;
    float4 tangentWS : TANGENT;
    nointerpolation float4 color : COLOR0;
};

PSInput VSMain(VSInput input)
{
    MeshInstanceGpu inst  = gInstances[gIndices[input.iid + baseIndex]];
    float4x4        world = meshInstanceRows(inst.world0, inst.world1, inst.world2, inst.world3);
    float4x4        prev  = meshInstanceRows(inst.prev0, inst.prev1, inst.prev2, inst.prev3);
    float4          wp    = float4(input.position, 1.0f);
    PSInput         o;
    o.currClip  = mul(mul(wp, world), viewProj);
    o.prevClip  = mul(mul(wp, prev), prevViewProj);
    o.position  = o.currClip;
    o.normalWS  = mul(float4(input.normal, 0.0f), world).xyz;
    o.tangentWS = float4(mul(float4(input.tangent.xyz, 0.0f), world).xyz, input.tangent.w);
    o.uv        = input.uv;
    o.color     = inst.color;
    return o;
}

GBufferOut PSMain(PSInput input)
{
    float4 albedoS = gAlbedo.Sample(gSamp, input.uv);
    if (alphaModeMask > 0.5f)
        clip(albedoS.a - alphaCutoff);

    float3 albedo = albedoS.rgb * input.color.rgb;
    float3 n      = ApplyNormalMap(input.normalWS, input.tangentWS, input.uv);

    float4 orm   = gOrm.Sample(gSamp, input.uv);
    float  rough = saturate(orm.g * roughness);
    float  metal = saturate(orm.b * metallic);
    float  aoOut = saturate(orm.r * ao);

    float emis = dot(gEmissive.Sample(gSamp, input.uv).rgb, float3(0.2126f, 0.7152f, 0.0722f)) * input.color.a;

    GBufferOut o;
    o.albedo   = float4(albedo, emis);
    o.attrib   = float4(EncodeOct(n), rough, metal);
    o.velocity = VelocityUv(input.currClip, input.prevClip);
    o.ao       = aoOut;
    return o;
}
