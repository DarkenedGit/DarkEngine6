// False-color heat. 0 is cold, about 0.5 is ambient, 1 blooms.
// G-buffer writers compile twice. DE_THERMAL stores heat in RT0.a.
// The other compile keeps emissive there, so the off mode stays the current picture.
// The palette is linear HDR. Only the hot stop is above the bloom threshold.

#ifndef DE_THERMAL_HEAT
#define DE_THERMAL_HEAT 0.41
#endif

static const float kThermalSky   = 0.15f;
static const float kThermalWater = 0.30f;
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

float thermalScalar(float emis, float metallic)
{
#ifdef DE_THERMAL
    float heat = max(DE_THERMAL_HEAT, emis);
    heat = max(emis, lerp(heat, kThermalMetal, saturate(metallic)));
    return saturate(heat);
#else
    // metallic * 0 keeps the off compile using both arguments. Finite emis is unchanged.
    return emis + metallic * 0.0f;
#endif
}

float3 thermalMix(float3 beauty, float heat, float strength)
{
    if (strength <= 1.0e-4f)
        return beauty;
    return lerp(beauty, thermalFalseColor(heat), saturate(strength));
}
