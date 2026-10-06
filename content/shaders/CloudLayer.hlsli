#ifndef DE_CLOUD_LAYER_HLSLI
#define DE_CLOUD_LAYER_HLSLI

#if SKY_CLOUDS_MODE == 2

cbuffer CloudLayerConstants : register(b2)
{
    float4 clLightColor;
    float4 clLightDir;
    float4 clSkyTop;
    float4 clSkyBottom;
    float4 clGeom;
    float4 clWind;
    float4 clScale;
    float4 clPhase;
    float4 clMs;
    float4 clMisc;
};

static const float kFourPi = 12.5663706f;

float ClHG(float cosT, float g)
{
    float g2 = g * g;
    return (1.0f - g2) / (kFourPi * pow(max(1.0f + g2 - 2.0f * g * cosT, 1e-4f), 1.5f));
}

float ClNoise(float2 p)
{
    const float2x2 m = float2x2(1.6f, 1.2f, -1.2f, 1.6f);
    float v = 0.0f, a = 0.5f;
    [unroll] for (int i = 0; i < 3; ++i)
    {
        v += a * Noise2(p);
        p = mul(m, p);
        a *= 0.5f;
    }
    return v / 0.875f;
}

float ClDensity(float2 xz, bool detail)
{
    float c = clSkyBottom.a;
    float n = ClNoise((xz + clWind.xy) * clScale.x);
    float d = saturate((n - (1.0f - c)) / max(c, 0.05f));
    if (detail && d > 0.0f)
    {
        float e = ClNoise((xz + clWind.zw) * clScale.y);
        d = saturate(d - (1.0f - d) * clScale.z * e);
    }
    return d;
}

float ClShell(float mu, float y0, out float muHit)
{
    float h = clGeom.x, R = clGeom.z;
    y0 = min(y0, h - 1.0f);
    float r0 = R + y0, r1 = R + h;
    float k  = (h - y0) * (2.0f * R + h + y0);
    float b  = r0 * mu;
    float s  = sqrt(b * b + k);
    float t  = (b >= 0.0f) ? k / (s + b) : (s - b);
    muHit    = (b + t) / r1;
    return t;
}

float4 EvaluateCloudLayer(float3 v, float3 skyNoDisc)
{
    if (clLightColor.a < 0.5f || v.y <= 0.0f)
        return float4(0, 0, 0, 1);

    float muHit;
    float t = ClShell(v.y, cameraPos.y, muHit);
    float fade = saturate((clGeom.w - t) / (0.25f * clGeom.w));
    if (fade <= 0.0f)
        return float4(0, 0, 0, 1);

    float2 xz = cameraPos.xz + v.xz * t;
    float  d  = ClDensity(xz, true);
    if (d <= 1e-3f)
        return float4(0, 0, 0, 1);

    float tauMax = clLightDir.w;
    float Tv     = exp(-tauMax * d / max(muHit, 0.05f));

    float3 L    = clLightDir.xyz;
    float  sy   = max(L.y, 0.08f);
    float2 s2   = L.xz / max(length(L.xz), 1e-4f);
    float  r    = min(0.5f * clGeom.y / sy, clMisc.y);
    float  acc  = 0.5f * d;
    [unroll] for (int k = 1; k <= 3; ++k)
        acc += ClDensity(xz + s2 * (r * (k / 3.0f)), false);
    float tauS = tauMax * clMisc.x * acc / (3.5f * sy);

    float cosT = dot(v, L);
    float sun = 0.0f, an = 1.0f, bn = 1.0f, cn = 1.0f;
    [unroll] for (int n = 0; n < 3; ++n)
    {
        float ph = lerp(ClHG(cosT, clPhase.x * cn), ClHG(cosT, clPhase.y * cn), clPhase.z) * kFourPi;
        sun += an * exp(-bn * tauS) * ph;
        an *= clMs.x; bn *= clMs.y; cn *= clMs.z;
    }
    sun += clPhase.w * pow(saturate(cosT), 8.0f) * exp(-0.25f * tauS);
    float powder = lerp(1.0f, 1.0f - exp(-2.0f * tauS), clMs.w * saturate(0.5f - 0.5f * cosT));

    float3 amb = clSkyTop.a * lerp(clSkyTop.rgb, clSkyBottom.rgb, d);
    float3 S   = clMisc.z * (clLightColor.rgb * (sun * powder) + amb);

    float  Tap = exp(-t / clScale.w);
    S = lerp(skyNoDisc, S, Tap);

    float alpha = fade * (1.0f - Tv);
    return float4(S * alpha, 1.0f - alpha);
}

#endif
#endif
