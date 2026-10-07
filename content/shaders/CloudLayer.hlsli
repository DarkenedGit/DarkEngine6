#ifndef DE_CLOUD_LAYER_HLSLI
#define DE_CLOUD_LAYER_HLSLI

// Distant deck. Must match Sky::cloudShellHit. No cbuffer: the caller already
// declared cameraPos, coverage, and clLightColor / clLightDir / clSkyTop / clSkyBottom / clWind.

static const float kCloudR      = 6.36e6f;
static const float kCloudFourPi = 12.5663706f;

float CloudHg(float cosT, float g)
{
    float g2    = g * g;
    float denom = max(1.0f + g2 - 2.0f * g * cosT, 1e-4f);
    return (1.0f - g2) / (kCloudFourPi * pow(denom, 1.5f));
}

float CloudNoise(float2 p)
{
    const float2x2 m = float2x2(1.6f, 1.2f, -1.2f, 1.6f);
    float v = 0.0f;
    float a = 0.5f;
    [unroll]
    for (int octave = 0; octave < 3; ++octave)
    {
        v += a * Noise2(p);
        p = mul(m, p);
        a *= 0.5f;
    }
    return v / 0.875f;
}

float CloudDensityBase(float2 xz)
{
    float n = CloudNoise((xz + clWind.xy) * (1.0f / 4000.0f));
    return saturate((n - (1.0f - coverage)) / max(coverage, 0.05f));
}

float CloudDensity(float2 xz)
{
    float d      = CloudDensityBase(xz);
    float detail = CloudNoise((xz + clWind.zw) * (1.0f / 900.0f));
    return saturate(d - (1.0f - d) * 0.35f * detail);
}

bool CloudShellHit(float cameraY, float viewY, float altitude, out float t, out float muHit)
{
    t     = 0.0f;
    muHit = 1.0f;
    if (viewY <= 0.0f || cameraY >= altitude - 1.0f)
        return false;

    float y0 = cameraY;
    float k  = (altitude - y0) * (2.0f * kCloudR + altitude + y0);
    float b  = (kCloudR + y0) * viewY;
    float s  = sqrt(b * b + k);
    t        = (b >= 0.0f) ? k / (s + b) : (s - b);
    muHit    = (b + t) / (kCloudR + altitude);
    return true;
}

float4 EvaluateCloudLayer(float3 v, float3 skyNoDisc)
{
    if (coverage < 0.02f)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);

    float t;
    float muHit;
    if (!CloudShellHit(cameraPos.y, v.y, clLightDir.w, t, muHit))
        return float4(0.0f, 0.0f, 0.0f, 1.0f);

    float fade = saturate((35000.0f - t) / (0.25f * 35000.0f));
    if (fade <= 0.0f)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);

    float2 xz = cameraPos.xz + v.xz * t;
    float d   = CloudDensity(xz);
    if (d <= 1e-3f)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);

    float amount = clLightColor.a;
    float tau    = 12.0f * amount * d / max(muHit, 0.05f);
    float viewT  = exp(-tau);

    float3 L     = clLightDir.xyz;
    float  sy    = max(L.y, 0.08f);
    float2 s2    = L.xz / max(length(L.xz), 1e-4f);
    float  reach = min(0.5f * 800.0f / sy, 3000.0f);
    // Half a step at the hit plus three base taps. 3.5 keeps kappa on the mean density.
    float acc = 0.5f * d;
    [unroll]
    for (int tap = 1; tap <= 3; ++tap)
        acc += CloudDensityBase(xz + s2 * (reach * (tap / 3.0f)));
    float tauS = (12.0f * amount) * 0.5f * acc / (3.5f * sy);

    float cosT = dot(v, L);
    float sun  = 0.0f;
    float an   = 1.0f;
    float bn   = 1.0f;
    float cn   = 1.0f;
    [unroll]
    for (int n = 0; n < 3; ++n)
    {
        float ph = lerp(CloudHg(cosT, 0.45f * cn), CloudHg(cosT, -0.16f * cn), 0.32f) * kCloudFourPi;
        sun += an * exp(-bn * tauS) * ph;
        an *= 0.5f;
        bn *= 0.5f;
        cn *= 0.5f;
    }
    // Extinct the silver spike. An unextincted 0.75 blows out through a thick deck.
    sun += 0.75f * pow(saturate(cosT), 8.0f) * exp(-tauS);

    float powder = 1.0f;
    if (cosT < 0.0f)
        powder = lerp(1.0f, 1.0f - exp(-2.0f * tauS), 0.6f);

    float3 amb = lerp(clSkyTop.rgb, clSkyBottom.rgb, d);
    float3 S   = 0.9f * (clLightColor.rgb * (sun * powder) + amb);
    S          = lerp(skyNoDisc, S, exp(-t / 25000.0f));

    float alpha = fade * (1.0f - viewT);
    return float4(S * alpha, 1.0f - alpha);
}

#endif
