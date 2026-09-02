#ifndef DE_GBUFFER_HLSLI
#define DE_GBUFFER_HLSLI

float2 OctWrap(float2 v)
{
    return (1.0f - abs(v.yx)) * float2(v.x >= 0.0f ? 1.0f : -1.0f, v.y >= 0.0f ? 1.0f : -1.0f);
}

float2 EncodeOct(float3 n)
{
    n /= max(abs(n.x) + abs(n.y) + abs(n.z), 1e-6f);
    if (n.z < 0.0f)
        n.xy = OctWrap(n.xy);
    return n.xy * 0.5f + 0.5f;
}

float3 DecodeOct(float2 f)
{
    f = f * 2.0f - 1.0f;
    float3 n = float3(f.x, f.y, 1.0f - abs(f.x) - abs(f.y));
    if (n.z < 0.0f)
        n.xy = OctWrap(n.xy);
    return normalize(n);
}

struct GBufferOut
{
    float4 albedo : SV_TARGET0;
    float4 attrib : SV_TARGET1;
};

#endif
