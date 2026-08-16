// Depth-only directional caster. No pixel shader.
#pragma pack_matrix(row_major)

cbuffer CasterConstants : register(b0)
{
    float4x4 lightWVP;
};

float4 VSMain(float3 position : POSITION) : SV_POSITION
{
    return mul(float4(position, 1.0f), lightWVP);
}
