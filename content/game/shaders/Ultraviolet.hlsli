// Bee vision is a channel shift, not a second BRDF.
// Long (green) → red, mid (blue) → green, short (UV reflectance) → blue. Red is dropped.
// DE_UV_CLASS is the reflectance used when a draw has no authored mask.

#ifndef DE_UV_CLASS
#define DE_UV_CLASS 0.0
#endif

static const float kUvSky   = 0.75f;
static const float kUvWater = 0.05f;
static const float kUvFog   = 0.80f;
static const float kUvMetal = 0.45f;

float3 beeAlbedo(float3 albedo, float uvReflect)
{
    return float3(albedo.g, albedo.b, saturate(uvReflect));
}

float3 beeShift(float3 albedo, float uvReflect, float strength)
{
    if (strength <= 1.0e-4f)
        return albedo;
    return lerp(albedo, beeAlbedo(albedo, uvReflect), saturate(strength));
}
