// Instanced skinned depth. No pixel shader. Part world is already baked.
#pragma pack_matrix(row_major)

#include "SkinnedInstance.hlsli"

cbuffer MeshDepthBatchConstants : register(b0)
{
    float4x4 lightViewProj;
    uint     baseIndex;
    uint     pad0;
    uint     pad1;
    uint     pad2;
};

struct VSInput
{
    float3 position : POSITION;
    uint4  joints   : BLENDINDICES;
    float4 weights  : BLENDWEIGHT;
    uint   iid      : SV_InstanceID;
};

float4 VSMain(VSInput input) : SV_POSITION
{
    SkinnedInstanceGpu inst  = gInstances[gIndices[input.iid + baseIndex]];
    float4x4           world = skinnedRows(inst.world0, inst.world1, inst.world2, inst.world3);
    float4             posM  = skinPosCurr(inst.paletteIndex, input.position, input.joints, input.weights);
    float4             prevM = skinPosPrev(inst.paletteIndex, input.position, input.joints, input.weights);
    float              keepF = inst.prev0.x + inst.prev1.y + inst.prev2.z + inst.prev3.w + inst.color.a + prevM.x;
    uint               keep  = inst.pad0 | inst.pad1 | inst.pad2 | pad0 | pad1 | pad2;
    if (keep == 0xFFFFFFFFu && keepF == 0.0f)
        posM = float4(0.0, 0.0, 0.0, 0.0);
    return mul(mul(posM, world), lightViewProj);
}
