// Unlit 2D sprite — row-major matrices (matches Dark::Math::Matrix4f).
#pragma pack_matrix(row_major)

cbuffer SpriteConstants : register(b0)
{
    float4x4 worldViewProj;
    float4   color;
    float2   uvScale;
    float2   uvOffset;
};

Texture2D    gAlbedo : register(t0);
SamplerState gSamp   : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

PSInput VSMain(VSInput input)
{
    PSInput o;
    o.position = mul(float4(input.position, 1.0f), worldViewProj);
    o.uv       = input.uv * uvScale + uvOffset;
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return gAlbedo.Sample(gSamp, input.uv) * color;
}
