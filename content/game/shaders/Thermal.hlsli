// False-color heat. 0 is cold, about 0.5 is ambient, 1 blooms.
// The palette is linear HDR. Only the hot stop is above the bloom threshold.
// kThermal* must match the kVisionHeat* constants in Game/VisionModes.h.

static const float kThermalSky   = 0.15f;
static const float kThermalMetal = 0.28f;

float3 thermalFalseColor(float h)
{
    h = saturate(h);
    const float3 cold   = float3(0.015f, 0.03f, 0.10f);
    const float3 ground = float3(0.08f, 0.03f, 0.14f);
    const float3 body   = float3(1.15f, 0.42f, 0.06f);
    const float3 fire   = float3(5.5f, 2.2f, 0.35f);
    if (h < 0.41f)
        return lerp(cold, ground, saturate(h / 0.41f));
    if (h < 0.72f)
        return lerp(ground, body, saturate((h - 0.41f) / 0.31f));
    return lerp(body, fire, saturate((h - 0.72f) / 0.28f));
}

// classHeat is the surface's class (the SurfaceTag LUT). Emissive is at least as hot as its glow;
// metal reads toward kThermalMetal.
float thermalScalar(float classHeat, float emis, float metallic)
{
    float heat = max(classHeat, emis);
    heat = max(emis, lerp(heat, kThermalMetal, saturate(metallic)));
    return saturate(heat);
}
