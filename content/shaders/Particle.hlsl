// Camera-facing particle quads — row-major matrices (matches Dark::Math::Matrix4f).
#pragma pack_matrix(row_major)

cbuffer FrameConstants : register(b0)
{
    float4x4 viewProj;
};

Texture2D    gSprite : register(t0);
SamplerState gSamp   : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

PSInput VSMain(VSInput input)
{
    PSInput o;
    o.position = mul(float4(input.position, 1.0f), viewProj);
    o.uv       = input.uv;
    o.color    = input.color;
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 tex = gSprite.Sample(gSamp, input.uv);
    return tex * input.color;
}
