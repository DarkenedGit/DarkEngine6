#pragma pack_matrix(row_major)

#include "TreeWind.hlsli"

cbuffer PartConstants : register(b0)
{
    float4x4 localToRoot;
    float4x4 lightWVP;
    float    alphaCutoff;
    float    alphaModeMask;
    float    kindIndex;
    float    depthPad;
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

Texture2D    gAlbedo : register(t0);
SamplerState gSamp   : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float2 uv       : TEXCOORD0;
    uint   iid      : SV_InstanceID;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
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
        float3 nWS    = float3(0, 0, 0);

        float3 cur  = wp.xyz + TreeWindOffset(wp.xyz, origin, scaleY, nWS, windCur, field, kindA[k], kindB[k], kindC[k], eye.xyz);
        o.position  = mul(float4(cur, 1.0f), lightWVP);
    }
    else
    {
        o.position = mul(wp, lightWVP);
    }

    o.uv = input.uv;
    return o;
}

void PSMain(PSInput input)
{
    if (alphaModeMask > 0.5f)
        clip(gAlbedo.Sample(gSamp, input.uv).a - alphaCutoff);
}
