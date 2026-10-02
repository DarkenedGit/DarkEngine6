// Geometric-mean exposure meter. Quarter-res pass stores (log2(luma) * w, w).
// Each downsample is a 2×2 average of that pair, so log/weight survives the pyramid.
#pragma pack_matrix(row_major)

#include "Depth.hlsli"

cbuffer AutoExposureConstants : register(b0)
{
    float invDstWidth;
    float invDstHeight;
    float srcWidth;
    float srcHeight;
};

Texture2D gSrc : register(t0);
#ifdef AE_REDUCE
Texture2D gDepth : register(t1);
#endif

struct PSInput
{
    float4 position : SV_POSITION;
};

PSInput VSMain(uint id : SV_VertexID)
{
    float2 pos = float2((id << 1) & 2, id & 2) * 2.0f - 1.0f;
    PSInput o;
    o.position = float4(pos, 0.0f, 1.0f);
    return o;
}

float luma709(float3 c)
{
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

float2 destUv(float2 pixel)
{
    return pixel * float2(invDstWidth, invDstHeight);
}

int2 srcSize()
{
    return int2(max(srcWidth, 1.0f), max(srcHeight, 1.0f));
}

#ifdef AE_REDUCE
// Point load so a sky texel does not bleed into a ground texel and zero its weight.
float2 PSReduce(PSInput input) : SV_TARGET
{
    float2 uv     = destUv(input.position.xy);
    int2   dim    = srcSize();
    int2   srcPx  = clamp(int2(uv * float2(dim)), int2(0, 0), dim - int2(1, 1));
    float3 hdr    = gSrc.Load(int3(srcPx, 0)).rgb;
    float  depth  = gDepth.Load(int3(srcPx, 0)).r;
    float  r      = length(uv * 2.0f - float2(1.0f, 1.0f));
    float  w      = IsSkyDepth(depth) ? 0.0f : lerp(0.15f, 1.0f, saturate(1.0f - r));
    float  y      = log2(max(luma709(hdr), 1.0e-4f)) * w;
    return float2(y, w);
}
#endif

// Manual 2×2 average. R32G32_FLOAT is not a linearly filterable format on every device.
float2 sample4Tap(float2 uv)
{
    int2   dim  = srcSize();
    float2 pix  = uv * float2(dim) - float2(0.5f, 0.5f);
    int2   base = int2(floor(pix));
    int2   p00  = clamp(base, int2(0, 0), dim - int2(1, 1));
    int2   p10  = clamp(base + int2(1, 0), int2(0, 0), dim - int2(1, 1));
    int2   p01  = clamp(base + int2(0, 1), int2(0, 0), dim - int2(1, 1));
    int2   p11  = clamp(base + int2(1, 1), int2(0, 0), dim - int2(1, 1));
    float2 c    = gSrc.Load(int3(p00, 0)).rg;
    c += gSrc.Load(int3(p10, 0)).rg;
    c += gSrc.Load(int3(p01, 0)).rg;
    c += gSrc.Load(int3(p11, 0)).rg;
    return c * 0.25f;
}

float2 PSDownsample(PSInput input) : SV_TARGET
{
    return sample4Tap(destUv(input.position.xy));
}
