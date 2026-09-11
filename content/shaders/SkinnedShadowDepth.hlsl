// Depth-only skinned caster. No pixel shader.
#pragma pack_matrix(row_major)

cbuffer CasterConstants : register(b0)
{
    float4x4 lightWVP;
};

cbuffer BonePalette : register(b2)
{
    float4x4 boneCurr[64];
    float4x4 bonePrev[64];
};

struct VSInput
{
    float3 position : POSITION;
    uint4  joints   : BLENDINDICES;
    float4 weights  : BLENDWEIGHT;
};

float4 skinPos(float3 p, uint4 j, float4 w)
{
    float4 hp = float4(p, 1.0f);
    return w.x * mul(hp, boneCurr[j.x]) + w.y * mul(hp, boneCurr[j.y])
         + w.z * mul(hp, boneCurr[j.z]) + w.w * mul(hp, boneCurr[j.w]);
}

float4 VSMain(VSInput input) : SV_POSITION
{
    float4 posM = skinPos(input.position, input.joints, input.weights);
    return mul(posM, lightWVP);
}
