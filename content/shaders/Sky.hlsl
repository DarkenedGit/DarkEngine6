// Fullscreen atmosphere: Rayleigh/Mie-inspired sky + coverage clouds.
// Must stay visually consistent with Sky::Environment::evaluateSky (CPU).
#pragma pack_matrix(row_major)

cbuffer FrameConstants : register(b0)
{
    float3 cameraPos;
    float  coverage;
    float3 sunDir;
    float  turbidity;
    float3 sunColor;
    float  cloudTime;
    float3 moonDir;
    float  windSpeed;
    float3 moonColor;
    float  rain;
    float2 windDir;
    float  sunElevation;
    float  exposure;
    float3 cameraRight;
    float  tanHalfFovX;
    float3 cameraUp;
    float  tanHalfFovY;
    float3 cameraLook;
    float  _pad;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 clipXY   : TEXCOORD0;
};

PSInput VSMain(uint id : SV_VertexID)
{
    float2 pos = float2((id << 1) & 2, id & 2) * 2.0f - 1.0f;
    // Standard fullscreen triangle: (-1,-1), (-1,3), (3,-1) via vertex id 0,1,2
    // The bit trick above gives (-1,-1), (3,-1), (-1,3) — also covers the screen.
    PSInput o;
    o.position = float4(pos, 0.0f, 1.0f);
    o.clipXY   = pos;
    return o;
}

float Hash21(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float Noise2(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    float a = Hash21(i);
    float b = Hash21(i + float2(1, 0));
    float c = Hash21(i + float2(0, 1));
    float d = Hash21(i + float2(1, 1));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float Fbm(float2 p)
{
    float v = 0.0f;
    float a = 0.5f;
    [unroll]
    for (int i = 0; i < 5; ++i)
    {
        v += a * Noise2(p);
        p = p * 2.03f + 17.1f;
        a *= 0.5f;
    }
    return v;
}

float RayleighPhase(float cosTheta)
{
    return 0.75f * (1.0f + cosTheta * cosTheta);
}

float MiePhase(float cosTheta, float g)
{
    const float pi = 3.14159265f;
    float g2 = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cosTheta;
    return (1.0f - g2) / (4.0f * pi * max(pow(max(denom, 1e-4f), 1.5f), 1e-4f));
}

float3 EvaluateSky(float3 v)
{
    v = normalize(v);
    float dayF = saturate((sunElevation + 0.12f) / 0.28f);
    float dusk = saturate((0.32f - sunElevation) / 0.34f);

    float3 dayZen  = float3(0.10f, 0.32f, 0.68f);
    float3 dayHor  = float3(0.62f, 0.74f, 0.88f);
    float3 duskZen = float3(0.18f, 0.12f, 0.22f);
    float3 duskHor = float3(0.92f, 0.38f, 0.16f);
    float3 nightZen = float3(0.01f, 0.02f, 0.05f);
    float3 nightHor = float3(0.03f, 0.04f, 0.08f);

    float3 zen = lerp(dayZen, duskZen, dusk);
    float3 hor = lerp(dayHor, duskHor, dusk);
    zen = lerp(nightZen, zen, dayF);
    hor = lerp(nightHor, hor, dayF);

    if (v.y < 0.0f)
    {
        float t = saturate(-v.y);
        return lerp(hor, hor * 0.35f, t);
    }

    float h = pow(1.0f - saturate(v.y), 0.45f);
    float3 base = lerp(zen, hor, h);

    float cosS = dot(v, sunDir);
    float cosM = dot(v, moonDir);
    float sunVis = saturate((sunElevation + 0.02f) / 0.08f) * (1.0f - 0.88f * coverage);
    float moonVis = saturate((moonDir.y + 0.02f) / 0.10f) * (1.0f - dayF) * (1.0f - 0.7f * coverage);
    float horizonAmt = 1.0f - saturate(max(sunElevation, 0.0f) / 0.28f);
    horizonAmt = horizonAmt * horizonAmt * (3.0f - 2.0f * horizonAmt);

    // Tight corona (real sun is ~0.5°). High g + tiny scale = small glow, not a hemisphere.
    float mieS = MiePhase(cosS, 0.86f);
    float mieM = MiePhase(cosM, 0.80f);
    float ray  = RayleighPhase(cosS);

    base += sunColor * (0.12f * ray + 0.35f * mieS) * sunVis;
    base += moonColor * (1.2f * mieM * moonVis);

    // Limb-darkened disc. Radius grows near the horizon (keep in sync with Environment::sunAngularRadius).
    float radius = lerp(0.0046f, 0.0150f, horizonAmt);
    float ang    = acos(saturate(cosS));
    float r      = ang / max(radius, 1e-5f);
    float edge   = 1.0f - smoothstep(0.88f, 1.06f, r);
    float mu     = sqrt(saturate(1.0f - r * r)); // 1 at center, 0 at limb
    float limb   = 0.22f + 0.78f * mu;
    float3 discCol = lerp(float3(1.00f, 0.72f, 0.32f), float3(1.00f, 0.97f, 0.90f), mu);
    base += discCol * (2.8f * limb * edge * sunVis);

    // Horizon sunset glow band: gaussian in elevation, stronger toward the sun.
    float duskAmt = saturate((0.34f - sunElevation) / 0.38f) * (1.0f - 0.72f * coverage);
    if (duskAmt > 1e-3f)
    {
        float bandY = 0.045f + 0.02f * horizonAmt;
        float bandW = 0.055f + 0.04f * horizonAmt;
        float dy    = (v.y - bandY) / bandW;
        float band  = exp(-dy * dy);

        float2 vh = v.xz;
        float2 sh = sunDir.xz;
        float vhm = length(vh);
        float shm = length(sh);
        float az = 0.5f;
        if (vhm > 1e-4f && shm > 1e-4f)
            az = saturate(dot(vh / vhm, sh / shm) * 0.5f + 0.5f);
        float azW = pow(az, 1.35f);
        float3 glow = lerp(float3(1.00f, 0.38f, 0.08f), float3(1.00f, 0.62f, 0.22f), az);
        base += glow * (0.62f * band * azW * duskAmt * (0.65f + 0.35f * saturate(turbidity / 8.0f)));
    }

    float lum = dot(base, float3(0.2126f, 0.7152f, 0.0722f));
    float3 overcast = lum * float3(0.92f, 0.95f, 1.00f);
    overcast *= (0.45f + 0.55f * saturate(v.y));
    overcast = lerp(overcast, float3(0.18f, 0.20f, 0.22f), rain * 0.45f);
    base = lerp(base, overcast, coverage);

    // Soft cloud plane in view-space XZ / Y.
    float2 cloudUv = v.xz / max(v.y, 0.08f);
    cloudUv += windDir * cloudTime * windSpeed;
    float n = Fbm(cloudUv * 1.6f);
    float thresh = 1.0f - coverage * 0.85f;
    float cloud = saturate((n - thresh) * (2.4f + 3.0f * coverage));
    cloud *= saturate(v.y * 3.0f); // no clouds on the horizon line
    float3 cloudCol = lerp(float3(0.75f, 0.78f, 0.82f), sunColor, 0.25f * sunVis);
    cloudCol = lerp(cloudCol, float3(0.16f, 0.17f, 0.19f), rain);
    base = lerp(base, cloudCol, cloud * saturate(coverage * 1.2f));

    return max(base, 0.0f) * exposure;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    // Reconstruct the world ray from the camera basis (no matrix inverse).
    float3 dir = normalize(
        cameraLook
        + cameraRight * (input.clipXY.x * tanHalfFovX)
        + cameraUp * (input.clipXY.y * tanHalfFovY));
    return float4(EvaluateSky(dir), 1.0f);
}
