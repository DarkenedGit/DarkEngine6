// Bee vision is a channel shift, not a second BRDF.
// Long (green) → red, mid (blue) → green, short (UV reflectance) → blue. Red is dropped.
// The engine applies it as a ColorRemap (beeShiftRemap in Game/VisionModes.h) before lighting;
// beeShift is the same math. kUv* must match the kVisionUv* constants there.

static const float kUvSky   = 0.75f;
static const float kUvWater = 0.05f;
static const float kUvFog   = 0.80f;

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
