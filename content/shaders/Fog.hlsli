#ifndef DE_FOG_HLSLI
#define DE_FOG_HLSLI

// Lit exponential distance + height fog, plus valley volumetric density around water.
// In-scatter is atmospheric (horizon color, sun/moon, local lights) and kept dim
// enough that it fades with distance instead of saturating into a glowing shell.

struct FogParams
{
    float3 cameraPos;
    float3 lightDir;
    float3 lightColor;
    float3 ambientColor;
    float3 fogColor;
    float  fogDensity;
    float  heightFogDensity;
    float  heightFogFalloff;
    float  heightFogHeight;
    float  volumetricFogDensity;
    float  waterLevel;
    float  volumetricHeight;
    float2 heightOrigin;
    float  heightCellSize;
    float2 heightWorldSize;
};

struct FogResult
{
    float  transmittance;
    float3 inScatter;
};

float FogHenyeyGreenstein(float cosTheta, float g)
{
    float g2    = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cosTheta;
    return (1.0f - g2) / (12.5663706f * pow(max(denom, 1e-4f), 1.5f));
}

// Horizon/sky color, desaturated so a packing miss (e.g. lightDir.y) cannot go lime.
float3 FogAlbedo(FogParams p)
{
    float3 c   = saturate(p.fogColor);
    float  lum = dot(c, float3(0.2126f, 0.7152f, 0.0722f));
    float3 mist = lum * float3(0.90f, 0.94f, 1.00f);
    return saturate(lerp(mist, c, 0.50f));
}

float FogHeightDensity(float y, FogParams p)
{
    float falloff = max(p.heightFogFalloff, 1e-4f);
    float d       = p.heightFogDensity * exp(-falloff * (y - p.heightFogHeight));
    return min(d, p.heightFogDensity * 3.0f);
}

float FogHeightOpticalDepth(float camY, float dist, float dirY, FogParams p)
{
    if (p.heightFogDensity <= 1e-8f || dist <= 0.0f)
        return 0.0f;
    float b    = max(p.heightFogFalloff, 1e-4f);
    float base = min(p.heightFogDensity * exp(-b * (camY - p.heightFogHeight)), p.heightFogDensity * 3.0f);
    float a    = b * dirY;
    if (abs(a) > 1e-4f)
        return base * (1.0f - exp(-a * dist)) / a;
    return base * dist;
}

float FogSampleTerrainY(Texture2D heightMap, SamplerState heightSamp, float2 xz, FogParams p)
{
    if (p.heightCellSize <= 1e-6f)
        return p.waterLevel - p.volumetricHeight;
    float2 uv = (xz - p.heightOrigin) / max(p.heightWorldSize, float2(1e-3f, 1e-3f));
    if (any(uv < 0.0f) || any(uv > 1.0f))
        return 1e6f;
    return heightMap.SampleLevel(heightSamp, uv, 0).r;
}

float FogValleyDensity(float3 p, float terrainY, FogParams fp)
{
    if (fp.volumetricFogDensity <= 1e-8f)
        return 0.0f;
    float h     = max(fp.volumetricHeight, 0.1f);
    float dy    = (p.y - fp.waterLevel) / h;
    float yBand = exp(-dy * dy);
    float wet   = saturate((fp.waterLevel + h - terrainY) / h);
    return fp.volumetricFogDensity * yBand * wet;
}

// Ambient mist + a little sun/moon. Do NOT multiply fogColor*sun (that double-lights
// and ACES-clips a dominant G channel to lime shells).
float3 FogLightTerm(float3 viewDir, float shadow, FogParams p)
{
    float3 albedo   = FogAlbedo(p);
    float3 l        = normalize(p.lightDir);
    float  cosTheta = dot(-viewDir, l);
    float  phase    = FogHenyeyGreenstein(cosTheta, 0.32f);
    float3 amb      = albedo * (0.70f + 1.7f * saturate(p.ambientColor));
    float3 sun      = saturate(p.lightColor) * albedo * (0.18f + 0.50f * phase) * lerp(0.40f, 1.0f, saturate(shadow));
    return amb + sun;
}

FogResult FogFinish(float T, float3 scatter)
{
    FogResult r;
    r.transmittance = saturate(T);
    r.inScatter     = min(scatter, 1.6.xxx); // visible mist without neon shells
    return r;
}

FogResult FogIntegrate(float3 cam, float3 worldPos, FogParams p, Texture2D heightMap, SamplerState heightSamp, float shadow)
{
    FogResult r;
    r.transmittance = 1.0f;
    r.inScatter     = 0.0.xxx;

    float3 ray  = worldPos - cam;
    float  dist = length(ray);
    if (dist < 1e-3f)
        return r;

    float3 dir   = ray / dist;
    float  tauH  = FogHeightOpticalDepth(cam.y, dist, dir.y, p);
    float  tauD  = p.fogDensity * dist;
    float  T     = exp(-(tauD + tauH));
    float3 light = FogLightTerm(dir, shadow, p);
    float3 scatter = light * (1.0f - T);

    if (p.volumetricFogDensity > 1e-6f)
    {
        const int kSteps = 12;
        float     dt     = dist / float(kSteps);
        float     Tv     = 1.0f;
        float3    vs     = 0.0.xxx;
        [unroll]
        for (int i = 0; i < kSteps; ++i)
        {
            float3 samplePos = cam + dir * ((float(i) + 0.5f) * dt);
            float  terrainY  = FogSampleTerrainY(heightMap, heightSamp, samplePos.xz, p);
            float  d         = FogValleyDensity(samplePos, terrainY, p);
            // sigma_s * Li * dt, not (1-exp(-d dt))*Li — the latter paints opaque balls.
            vs += Tv * (d * dt) * light;
            Tv *= exp(-d * dt);
        }
        scatter = scatter * Tv + vs;
        T *= Tv;
    }

    return FogFinish(T, scatter);
}

FogResult FogIntegrateNoHeight(float3 cam, float3 worldPos, FogParams p, float shadow)
{
    FogResult r;
    r.transmittance = 1.0f;
    r.inScatter     = 0.0.xxx;

    float3 ray  = worldPos - cam;
    float  dist = length(ray);
    if (dist < 1e-3f)
        return r;

    float3 dir     = ray / dist;
    float  tauH    = FogHeightOpticalDepth(cam.y, dist, dir.y, p);
    float  tauD    = p.fogDensity * dist;
    float  T       = exp(-(tauD + tauH));
    float3 light   = FogLightTerm(dir, shadow, p);
    float3 scatter = light * (1.0f - T);

    if (p.volumetricFogDensity > 1e-6f)
    {
        const int kSteps = 10;
        float     dt     = dist / float(kSteps);
        float     Tv     = 1.0f;
        float3    vs     = 0.0.xxx;
        [unroll]
        for (int i = 0; i < kSteps; ++i)
        {
            float3 samplePos = cam + dir * ((float(i) + 0.5f) * dt);
            float  d         = FogValleyDensity(samplePos, p.waterLevel - p.volumetricHeight, p);
            vs += Tv * (d * dt) * light;
            Tv *= exp(-d * dt);
        }
        scatter = scatter * Tv + vs;
        T *= Tv;
    }

    return FogFinish(T, scatter);
}

float3 ApplyLitFog(float3 lit, FogResult fog)
{
    return lit * fog.transmittance + fog.inScatter;
}

float FogTransmittance(FogResult fog)
{
    return fog.transmittance;
}

#endif
