#pragma pack_matrix(row_major)

cbuffer PartConstants : register(b0)
{
    float4x4 localToRoot;
    float4x4 lightWVP;
};

#include "FoliageWorld.hlsli"

StructuredBuffer<FoliageWorld> gWorlds  : register(t4);
StructuredBuffer<uint>         gIndices : register(t5);

float4 VSMain(float3 position : POSITION, uint iid : SV_InstanceID) : SV_POSITION
{
    float4x4 world      = FoliageWorldMatrix(gWorlds[gIndices[iid]]);
    float4x4 localWorld = mul(localToRoot, world);
    return mul(mul(float4(position, 1.0f), localWorld), lightWVP);
}
