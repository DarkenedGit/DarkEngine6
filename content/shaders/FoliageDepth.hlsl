#pragma pack_matrix(row_major)

cbuffer PartConstants : register(b0)
{
    float4x4 localToRoot;
    float4x4 lightWVP;
    float    alphaCutoff;
    float    alphaModeMask;
    float2   depthPad;
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
    PSInput  o;
    o.position = mul(mul(float4(input.position, 1.0f), localWorld), lightWVP);
    o.uv       = input.uv;
    return o;
}

void PSMain(PSInput input)
{
    if (alphaModeMask > 0.5f)
        clip(gAlbedo.Sample(gSamp, input.uv).a - alphaCutoff);
}
