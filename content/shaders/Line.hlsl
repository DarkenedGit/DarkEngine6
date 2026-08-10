// Colored line list — row-major matrices (matches Dark::Math::Matrix4f).
#pragma pack_matrix(row_major)

cbuffer FrameConstants : register(b0)
{
    float4x4 worldViewProj;
    float4   color;
};

struct VSInput
{
    float3 position : POSITION;
};

struct PSInput
{
    float4 position : SV_POSITION;
};

PSInput VSMain(VSInput input)
{
    PSInput o;
    o.position = mul(float4(input.position, 1.0f), worldViewProj);
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return color;
}
