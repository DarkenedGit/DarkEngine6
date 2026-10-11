// Instanced depth-only caster. No pixel shader. World is already baked.
#pragma pack_matrix(row_major)

#include "MeshInstance.hlsli"

cbuffer DepthBatchConstants : register(b0)
{
    float4x4 lightViewProj;
    uint     baseIndex;
    uint     pad0;
    uint     pad1;
    uint     pad2;
};

StructuredBuffer<MeshInstanceGpu> gInstances : register(t4);
StructuredBuffer<uint>            gIndices   : register(t5);

float4 VSMain(float3 position : POSITION, uint iid : SV_InstanceID) : SV_POSITION
{
    MeshInstanceGpu inst  = gInstances[gIndices[iid + baseIndex]];
    float4x4        world = meshInstanceRows(inst.world0, inst.world1, inst.world2, inst.world3);
    return mul(mul(float4(position, 1.0f), world), lightViewProj);
}
