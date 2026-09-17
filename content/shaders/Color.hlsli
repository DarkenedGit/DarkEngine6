#ifndef DE_COLOR_HLSLI
#define DE_COLOR_HLSLI

float srgbToLinear(float c)
{
    c = saturate(c);
    return (c <= 0.04045f) ? (c / 12.92f) : pow((c + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float c)
{
    c = saturate(c);
    return (c <= 0.0031308f) ? (12.92f * c) : (1.055f * pow(c, 1.0f / 2.4f) - 0.055f);
}

float3 srgbToLinear(float3 c)
{
    return float3(srgbToLinear(c.r), srgbToLinear(c.g), srgbToLinear(c.b));
}

float3 linearToSrgb(float3 c)
{
    return float3(linearToSrgb(c.r), linearToSrgb(c.g), linearToSrgb(c.b));
}

#ifndef ENCODE_SRGB
#define ENCODE_SRGB 0
#endif

float3 encodeSceneRgb(float3 linearRgb)
{
#if ENCODE_SRGB
    return linearToSrgb(linearRgb);
#else
    return linearRgb;
#endif
}

#endif
