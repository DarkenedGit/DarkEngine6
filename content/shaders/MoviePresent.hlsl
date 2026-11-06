// Opaque fullscreen movie. Window UV origin is top-left so letterbox bars stay black.
// The SRV is B8G8R8A8_UNORM. An sRGB view would darken an UNORM back buffer.

cbuffer MovieConstants : register(b0)
{
    float4 rect; // x0, y0, x1, y1
    float  opacity;
    float3 pad;
};

Texture2D    gFrame : register(t0);
SamplerState gSamp  : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 clipXy   : TEXCOORD0;
};

PSInput VSMain(uint id : SV_VertexID)
{
    // (-1,-1), (3,-1), (-1,3). Covers the viewport. No vertex buffer.
    float2 pos = float2((id << 1) & 2, id & 2) * 2.0f - 1.0f;
    PSInput o;
    o.position = float4(pos, 0.0f, 1.0f);
    o.clipXy   = pos;
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    // Clip y is up. Window UV y = 0 is the top.
    float2 uv      = float2(input.clipXy.x * 0.5f + 0.5f, 0.5f - input.clipXy.y * 0.5f);
    float  inside  = step(rect.x, uv.x) * step(uv.x, rect.z) * step(rect.y, uv.y) * step(uv.y, rect.w);
    float2 videoUv = (uv - rect.xy) / max(rect.zw - rect.xy, float2(1e-6f, 1e-6f));
    float3 rgb     = gFrame.Sample(gSamp, videoUv).rgb;
    return float4(rgb * inside * opacity, 1.0f);
}
