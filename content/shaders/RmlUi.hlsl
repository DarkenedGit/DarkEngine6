// Player UI. RmlUi positions are window pixels, origin top-left, Y down.
// Vertex color and generated textures are premultiplied sRGB stored in an UNORM target.
// gFlipV stays 0. RmlUi 6.3 font atlases and file textures are both top-left.

cbuffer RmlCb : register(b0)
{
    float4x4 gTransform;
    float    gFlipV;
};

Texture2D    gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VSIn
{
    float2 position : POSITION;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD;
};

struct VSOut
{
    float4 position : SV_Position;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD;
};

VSOut VSMain(VSIn v)
{
    VSOut o;
    o.position = mul(gTransform, float4(v.position, 0.0, 1.0));
    o.color    = v.color;
    o.uv       = v.uv;
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    float2 uv = float2(i.uv.x, lerp(i.uv.y, 1.0 - i.uv.y, gFlipV));
    return gTexture.Sample(gSampler, uv) * i.color;
}
