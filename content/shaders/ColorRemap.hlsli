#ifndef DE_COLOR_REMAP_HLSLI
#define DE_COLOR_REMAP_HLSLI

// Generic 3x4 colour matrix plus aux and strength (render-graph plan §3.4.2).
// out = lerp(c, M * float4(c, aux), strength). Strength 0 is a no-op: the
// function returns c with no matrix math, so an identity / unused remap has
// no per-pixel cost and is bit-identical to today's output.
struct ColorRemap
{
    float4 m0;
    float4 m1;
    float4 m2;
    float  aux;
    float  strength;
    float2 _pad;
};

float3 ApplyColorRemap(float3 c, ColorRemap r)
{
    if (r.strength <= 0.0f)
        return c;
    float4 v     = float4(c, r.aux);
    float3 mapped = float3(dot(r.m0, v), dot(r.m1, v), dot(r.m2, v));
    return lerp(c, mapped, saturate(r.strength));
}

#endif
