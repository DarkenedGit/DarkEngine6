#pragma pack_matrix(row_major)

#include "GBuffer.hlsli"
#include "Thermal.hlsli"
#include "Ultraviolet.hlsli"
#include "TreeWind.hlsli"

cbuffer PartConstants : register(b0)
{
    float4x4 localToRoot;
    float4   color;
    float    roughness;
    float    metallic;
    float    ao;
    float    normalScale;
    float    alphaCutoff;
    float    alphaModeMask;
    float    kindIndex;
    float3   kindPad;
};

cbuffer FoliageView : register(b1)
{
    float4x4 viewProj;
    float4x4 prevViewProj;
    float4   windCur;
    float4   windPrev;
    float4   field;
    float4   kindA[4];
    float4   kindB[4];
    float4   kindC[4];
    float4   eye;
};

#include "FoliageWorld.hlsli"

StructuredBuffer<FoliageWorld> gWorlds  : register(t4);
StructuredBuffer<uint>         gIndices : register(t5);

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
};

PSInput VSMain(VSInput input)
{
    float4x4 world      = FoliageWorldMatrix(gWorlds[gIndices[input.iid]]);
    float4x4 localWorld = mul(localToRoot, world);
    float4   wp         = mul(float4(input.position, 1.0f), localWorld);

    PSInput o;

    if (eye.w > 0.5)
    {
        uint   k      = uint(kindIndex);
        float3 origin = world[3].xyz;
        float  scaleY = length(world[1].xyz);
        float3 nWS    = normalize(mul(float4(input.normal, 0.0f), localWorld).xyz);

        float3 cur  = wp.xyz + TreeWindOffset(wp.xyz, origin, scaleY, nWS, windCur, field, kindA[k], kindB[k], kindC[k], eye.xyz);
        float3 prv  = wp.xyz + TreeWindOffset(wp.xyz, origin, scaleY, nWS, windPrev, field, kindA[k], kindB[k], kindC[k], eye.xyz);

        o.currClip  = mul(float4(cur, 1.0f), viewProj);
        o.prevClip  = mul(float4(prv, 1.0f), prevViewProj);
    }
    else
    {
        o.currClip  = mul(wp, viewProj);
        o.prevClip  = mul(wp, prevViewProj);
    }

    o.position  = o.currClip;
    o.normalWS  = mul(float4(input.normal, 0.0f), localWorld).xyz;
    o.tangentWS = float4(mul(float4(input.tangent.xyz, 0.0f), localWorld).xyz, input.tangent.w);
    o.uv        = input.uv;
    return o;
}

GBufferOut PSMain(PSInput input, bool isFront : SV_IsFrontFace)
{
    float4 albedoS = gAlbedo.Sample(gSamp, input.uv);
    if (alphaModeMask > 0.5f)
        clip(albedoS.a - alphaCutoff);

    float3 albedo = albedoS.rgb * color.rgb;
    float3 n      = ApplyNormalMap(input.normalWS, input.tangentWS, input.uv);
    if (!isFront)
        n = -n;

    float4 orm   = gOrm.Sample(gSamp, input.uv);
    float  rough = saturate(orm.g * roughness);
    float  metal = saturate(orm.b * metallic);
    float  aoOut = saturate(orm.r * ao);

    float emis = dot(gEmissive.Sample(gSamp, input.uv).rgb, float3(0.2126f, 0.7152f, 0.0722f)) * color.a;

    GBufferOut o;
#ifdef DE_ULTRAVIOLET
    o.albedo   = float4(albedo, saturate(DE_UV_CLASS));
#else
    o.albedo   = float4(albedo, thermalScalar(emis, metal));
#endif
    o.attrib   = float4(EncodeOct(n), rough, metal);
    o.velocity = VelocityUv(input.currClip, input.prevClip);
    o.ao       = aoOut;
    return o;
}
