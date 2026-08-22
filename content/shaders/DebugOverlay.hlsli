#pragma pack_matrix(row_major)

cbuffer OverlayConstants : register(b0)
{
    float slice;
    float contrast;
    float invert;
    float _pad;
};

SamplerState gSamp : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

PSInput VSMain(uint id : SV_VertexID)
{
    PSInput o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.position = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    o.uv       = uv;
    return o;
}

float visDepth(float d)
{
    float v = invert > 0.5f ? (1.0f - d) : d;
    v = saturate(v);
    float p = max(contrast, 0.01f);
    return saturate(pow(v, p));
}
