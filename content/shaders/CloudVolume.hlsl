// Ray-marched participating-media cloud volumes.
// Camera-inside works: tEnter clamps to 0, beer-lambert + dual-lobe HG + self-shadow.
// Distance LOD: rays that stay inside lodDetailDist keep the fixed step count. Longer rays
// use a full-quality near shell, a fade, then a coarse tail (fewer octaves, fewer sun steps).
#pragma pack_matrix(row_major)

#include "GBuffer.hlsli"

cbuffer CloudVolumePassConstants : register(b0)
{
    float4x4 invViewProj;
    float3   cameraPos;
    float    time;
    float3   sunDir;
    float    volumeCount;
    float3   sunColor;
    float    nearZ;
    float3   ambientColor;
    float    lodEnabled;
    float    lodDetailDist;
    float    lodFadeDist;
    float    lodNearStep;
    float    lodFarStep;
    float    lodMaxNear;
    float    lodMaxFar;
    float    lodNearLight;
    float    lodFarLight;
};

Texture2D                 gDepth   : register(t0);
StructuredBuffer<float4>  gVolumes : register(t1);

static const int kCloudSteps      = 40;
static const int kCloudLightSteps = 5;
static const int kCloudLightMax   = 8;
static const float kCloudFar      = 2800.0f;

struct GpuCloudVolume
{
    float3 center;
    float  density;
    float3 halfExtents;
    float  coverage;
    float4 invRotation;
    float3 albedo;
    float  softness;
    float  absorption;
    float  scattering;
    float  anisotropy;
    float  noiseScale;
    float  detailScale;
    float  detailStrength;
    float  heightFalloff;
    float  silverLining;
    float  shape;
    float  windSpeed;
    float  _pad0;
    float  _pad1;
    float3 windDir;
    float  _pad2;
};

GpuCloudVolume LoadVolume(uint i)
{
    // 128-byte records as 8 float4s.
    uint base = i * 8u;
    GpuCloudVolume v;
    float4 a = gVolumes[base + 0];
    float4 b = gVolumes[base + 1];
    float4 c = gVolumes[base + 2];
    float4 d = gVolumes[base + 3];
    float4 e = gVolumes[base + 4];
    float4 f = gVolumes[base + 5];
    float4 g = gVolumes[base + 6];
    float4 h = gVolumes[base + 7];
    v.center         = a.xyz;
    v.density        = a.w;
    v.halfExtents    = b.xyz;
    v.coverage       = b.w;
    v.invRotation    = c;
    v.albedo         = d.xyz;
    v.softness       = d.w;
    v.absorption     = e.x;
    v.scattering     = e.y;
    v.anisotropy     = e.z;
    v.noiseScale     = e.w;
    v.detailScale    = f.x;
    v.detailStrength = f.y;
    v.heightFalloff  = f.z;
    v.silverLining   = f.w;
    v.shape          = g.x;
    v.windSpeed      = g.y;
    v.windDir        = h.xyz;
    return v;
}

float3 QuatRotate(float4 q, float3 v)
{
    return v + 2.0f * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

float3 Hash33(float3 p)
{
    p = frac(p * float3(0.1031f, 0.1030f, 0.0973f));
    p += dot(p, p.yxz + 33.33f);
    return frac((p.xxy + p.yxx) * p.zyx) * 2.0f - 1.0f;
}

float GradientNoise3(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    float3 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);

    float n000 = dot(Hash33(i + float3(0, 0, 0)), f - float3(0, 0, 0));
    float n100 = dot(Hash33(i + float3(1, 0, 0)), f - float3(1, 0, 0));
    float n010 = dot(Hash33(i + float3(0, 1, 0)), f - float3(0, 1, 0));
    float n110 = dot(Hash33(i + float3(1, 1, 0)), f - float3(1, 1, 0));
    float n001 = dot(Hash33(i + float3(0, 0, 1)), f - float3(0, 0, 1));
    float n101 = dot(Hash33(i + float3(1, 0, 1)), f - float3(1, 0, 1));
    float n011 = dot(Hash33(i + float3(0, 1, 1)), f - float3(0, 1, 1));
    float n111 = dot(Hash33(i + float3(1, 1, 1)), f - float3(1, 1, 1));

    float nx00 = lerp(n000, n100, u.x);
    float nx10 = lerp(n010, n110, u.x);
    float nx01 = lerp(n001, n101, u.x);
    float nx11 = lerp(n011, n111, u.x);
    return lerp(lerp(nx00, nx10, u.y), lerp(nx01, nx11, u.y), u.z);
}

float Fbm3(float3 p, float octaveWeight)
{
    // Rotate each octave so lattice axes do not stack into visible tiles.
    // octaveWeight is the continuous octave count: 4 matches the old integer loop,
    // and a fraction fades the next octave out instead of popping it off.
    const float3x3 rot = float3x3(
        0.00f,  0.80f,  0.60f,
       -0.80f,  0.36f, -0.48f,
       -0.60f, -0.48f,  0.64f);
    float v = 0.0f;
    float a = 0.5f;
    [loop]
    for (int i = 0; i < 4; ++i)
    {
        float w = saturate(octaveWeight - float(i));
        if (w <= 0.0f)
            break;
        v += a * w * GradientNoise3(p);
        p = mul(p, rot) * 2.02f;
        a *= 0.5f;
    }
    return v * 0.5f + 0.5f;
}

float CloudHenyeyGreenstein(float cosTheta, float g)
{
    float g2    = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cosTheta;
    return (1.0f - g2) / (12.5663706f * pow(max(denom, 1e-4f), 1.5f));
}

float IntersectAabbLocal(float3 o, float3 d, float3 halfExt, out float tEnter, out float tExit)
{
    tEnter = 0.0f;
    tExit  = 1e9f;
    [unroll]
    for (int i = 0; i < 3; ++i)
    {
        float oi = o[i];
        float di = d[i];
        float h  = halfExt[i];
        if (abs(di) < 1e-8f)
        {
            if (oi < -h || oi > h)
                return 0.0f;
        }
        else
        {
            float invD = 1.0f / di;
            float t0   = (-h - oi) * invD;
            float t1   = ( h - oi) * invD;
            if (t0 > t1)
            {
                float tmp = t0;
                t0 = t1;
                t1 = tmp;
            }
            tEnter = max(tEnter, t0);
            tExit  = min(tExit, t1);
            if (tEnter > tExit)
                return 0.0f;
        }
    }
    if (tExit < 0.0f)
        return 0.0f;
    tEnter = max(tEnter, 0.0f);
    return tExit > tEnter ? 1.0f : 0.0f;
}

float IntersectEllipsoidLocal(float3 o, float3 d, float3 halfExt, out float tEnter, out float tExit)
{
    tEnter = 0.0f;
    tExit  = 0.0f;
    float3 so = o / max(halfExt, 1e-3f);
    float3 sd = d / max(halfExt, 1e-3f);
    float  a  = dot(sd, sd);
    if (a < 1e-12f)
        return 0.0f;
    float b    = dot(so, sd);
    float c    = dot(so, so) - 1.0f;
    float disc = b * b - a * c;
    if (disc < 0.0f)
        return 0.0f;
    float s  = sqrt(disc);
    float t0 = (-b - s) / a;
    float t1 = (-b + s) / a;
    if (t0 > t1)
    {
        float tmp = t0;
        t0 = t1;
        t1 = tmp;
    }
    if (t1 < 0.0f)
        return 0.0f;
    tEnter = max(t0, 0.0f);
    tExit  = t1;
    return tExit > tEnter ? 1.0f : 0.0f;
}

float ShapeMask(float3 localP, GpuCloudVolume v)
{
    float3 h    = max(v.halfExtents, 1e-3f);
    float  soft = saturate(v.softness);
    if (v.shape < 0.5f)
    {
        float3 q       = abs(localP) - h;
        float  outside = length(max(q, 0.0f));
        float  inside  = min(max(q.x, max(q.y, q.z)), 0.0f);
        float  sdf     = outside + inside;
        float  r       = max(soft * min(h.x, min(h.y, h.z)), 0.01f);
        return saturate(1.0f - sdf / r);
    }
    float e = length(localP / h);
    return saturate((1.0f + soft * 0.15f - e) / max(soft, 0.02f));
}

float CloudDensity(float3 worldPos, GpuCloudVolume v, float octaveWeight, float detailMul)
{
    float3 localP = QuatRotate(v.invRotation, worldPos - v.center);
    float  mask   = ShapeMask(localP, v);
    if (mask <= 1e-4f)
        return 0.0f;

    float3 windUv = worldPos * v.noiseScale + v.windDir * (time * v.windSpeed);
    // Warp and the detail octave only exist on the full-quality path. detailMul fades
    // both to zero so the coarse tail does not pay for them and does not pop.
    if (detailMul > 1.0e-3f)
    {
        float3 warp = float3(
            GradientNoise3(windUv),
            GradientNoise3(windUv + 19.7f),
            GradientNoise3(windUv + 31.4f));
        windUv += warp * (0.35f * detailMul);
    }
    float n = Fbm3(windUv, octaveWeight);
    if (detailMul > 1.0e-3f)
        n -= v.detailStrength * detailMul * (Fbm3(windUv * v.detailScale + 11.3f, 2.0f) - 0.5f);
    float thresh = 1.0f - saturate(v.coverage);
    n = saturate((n - thresh) / max(0.08f, v.softness * 0.65f));

    float ny     = localP.y / max(v.halfExtents.y, 1e-3f);
    float height = saturate(1.0f - (ny * 0.5f + 0.5f) * v.heightFalloff);
    float bottom = saturate((ny + 1.0f) / 0.32f);
    return v.density * n * mask * height * bottom;
}

float CloudShadow(float3 pos, GpuCloudVolume v, int lightSteps)
{
    float T      = 1.0f;
    float baseDt = max(min(min(v.halfExtents.x, v.halfExtents.y), v.halfExtents.z) * 0.18f, 1.2f);
    int   steps  = clamp(lightSteps, 1, kCloudLightMax);
    // Fewer taps still walk the original 5-step sun distance. The tail is coarser, not shorter.
    float  dt   = baseDt * (float(kCloudLightSteps) / float(steps));
    float3 step = sunDir * dt;
    [loop]
    for (int i = 0; i < kCloudLightMax; ++i)
    {
        if (i >= steps)
            break;
        pos += step;
        float d = CloudDensity(pos, v, 2.0f, 0.0f);
        T *= exp(-d * max(v.absorption, 0.15f) * dt);
        if (T < 0.02f)
            break;
    }
    return T;
}

void AccumulateCloud(float3 p, GpuCloudVolume v, float octaveWeight, float detailMul, int lightSteps, float dt, float3 rayDir, inout float3 scatter, inout float T)
{
    float d = CloudDensity(p, v, octaveWeight, detailMul);
    if (d <= 1.0e-5f)
        return;
    float sh    = CloudShadow(p, v, lightSteps);
    float cosTh = dot(-rayDir, sunDir);
    float g     = v.anisotropy;
    float phase = lerp(CloudHenyeyGreenstein(cosTh, g), CloudHenyeyGreenstein(cosTh, -0.35f * g), 0.32f);
    phase += v.silverLining * pow(saturate(cosTh), 8.0f) * 2.2f;
    float  ms    = 1.0f - exp(-3.5f * d);
    float3 sunLi = saturate(sunColor) * sh * (0.22f + 0.85f * phase);
    float3 ambLi = saturate(ambientColor) * (0.28f + 0.95f * ms);
    float3 Li    = (sunLi + ambLi) * v.albedo;
    float  sigmaS = d * max(v.scattering, 0.0f);
    float  sigmaT = d * (max(v.absorption, 0.0f) + max(v.scattering, 0.0f));
    scatter += T * sigmaS * Li * dt;
    T *= exp(-sigmaT * dt);
}

int LodStepCount(float span, float step, float cap)
{
    if (span <= 1.0e-3f)
        return 0;
    int n = (int)ceil(span / max(step, 0.5f));
    int c = (int)cap;
    if (c < 1)
        c = 1;
    if (n < 1)
        n = 1;
    if (n > c)
        n = c;
    return n;
}

void MarchUniform(float segStart, float segEnd, int stepCount, float dt, float jitter, float octaveWeight, float detailMul, int lightSteps, GpuCloudVolume v, float3 rayDir, inout float3 scatter, inout float T)
{
    if (stepCount <= 0 || !(segEnd > segStart) || T < 0.012f)
        return;
    float t = segStart + jitter * dt;
    [loop]
    for (int s = 0; s < stepCount; ++s)
    {
        if (T < 0.012f)
            return;
        if (t > segEnd)
            break;
        AccumulateCloud(cameraPos + rayDir * t, v, octaveWeight, detailMul, lightSteps, dt, rayDir, scatter, T);
        t += dt;
    }
}

void MarchFade(float segStart, float segEnd, float jitter, int lightSteps, GpuCloudVolume v, float3 rayDir, inout float3 scatter, inout float T)
{
    float span = segEnd - segStart;
    if (span <= 1.0e-3f || T < 0.012f)
        return;
    float mid = 0.5f * (max(lodNearStep, 0.5f) + max(lodFarStep, lodNearStep));
    int   n   = LodStepCount(span, mid, lodMaxNear);
    if (n <= 0)
        return;

    float sum = 0.0f;
    [loop]
    for (int i = 0; i < n; ++i)
    {
        float u  = (n <= 1) ? 1.0f : float(i) / float(n - 1);
        float su = u * u * (3.0f - 2.0f * u);
        sum += lerp(lodNearStep, lodFarStep, su);
    }
    sum = max(sum, 1.0e-3f);

    float fade = max(lodFadeDist, 1.0e-3f);
    float t    = segStart;
    [loop]
    for (int s = 0; s < n; ++s)
    {
        if (T < 0.012f)
            return;
        float u        = (n <= 1) ? 1.0f : float(s) / float(n - 1);
        float su       = u * u * (3.0f - 2.0f * u);
        float dt       = span * lerp(lodNearStep, lodFarStep, su) / sum;
        float sampleT  = min(t + jitter * dt, segEnd);
        float along    = saturate((sampleT - lodDetailDist) / fade);
        float shape    = along * along * (3.0f - 2.0f * along);
        float octaves  = lerp(4.0f, 2.0f, shape);
        float detail   = 1.0f - along;
        AccumulateCloud(cameraPos + rayDir * sampleT, v, octaves, detail, lightSteps, dt, rayDir, scatter, T);
        t += dt;
    }
}

float Ign(float2 p)
{
    return frac(52.9829189f * frac(dot(p, float2(0.06711056f, 0.00583715f))));
}

struct PSInput
{
    float4 position : SV_POSITION;
};

PSInput VSMain(uint id : SV_VertexID)
{
    float2 pos = float2((id << 1) & 2, id & 2) * 2.0f - 1.0f;
    PSInput o;
    o.position = float4(pos, 0.0f, 1.0f);
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    uint count = (uint)volumeCount;
    if (count == 0)
        discard;

    int2  texel = int2(input.position.xy);
    float depth = gDepth.Load(int3(texel, 0)).r;

    uint w, h;
    gDepth.GetDimensions(w, h);
    float ndcX = (input.position.x / float(w)) * 2.0f - 1.0f;
    float ndcY = 1.0f - (input.position.y / float(h)) * 2.0f;

    float3 pNear  = ReconstructWorldPos(ndcX, ndcY, 1.0f, invViewProj);
    float3 rayDir = normalize(pNear - cameraPos);

    float tScene = kCloudFar;
    if (!IsSkyDepth(depth))
    {
        float3 worldPos = ReconstructWorldPos(ndcX, ndcY, depth, invViewProj);
        tScene = length(worldPos - cameraPos);
    }

    float3 scatter = 0.0.xxx;
    float  T       = 1.0f;
    float  jitter  = Ign(input.position.xy + time);

    [loop]
    for (uint vi = 0; vi < count; ++vi)
    {
        GpuCloudVolume v = LoadVolume(vi);
        float3 localO = QuatRotate(v.invRotation, cameraPos - v.center);
        float3 localD = QuatRotate(v.invRotation, rayDir);
        float t0, t1;
        float hit = (v.shape < 0.5f)
            ? IntersectAabbLocal(localO, localD, v.halfExtents, t0, t1)
            : IntersectEllipsoidLocal(localO, localD, v.halfExtents, t0, t1);
        if (hit < 0.5f)
            continue;
        t1 = min(t1, tScene);
        if (t1 <= t0)
            continue;

        // Short chords stay on the original fixed march so a default-sized volume is unchanged.
        if (lodEnabled < 0.5f || t1 <= lodDetailDist)
        {
            int   steps = kCloudSteps;
            if (t0 <= 1e-3f)
                steps = 56;
            float dt = (t1 - t0) / float(steps);
            float t  = t0 + jitter * dt;
            [loop]
            for (int s = 0; s < steps; ++s)
            {
                if (T < 0.012f)
                    break;
                AccumulateCloud(cameraPos + rayDir * t, v, 4.0f, 1.0f, kCloudLightSteps, dt, rayDir, scatter, T);
                t += dt;
                if (t > t1)
                    break;
            }
        }
        else
        {
            int   nearLights = clamp((int)lodNearLight, 1, kCloudLightMax);
            int   farLights  = clamp((int)lodFarLight, 1, kCloudLightMax);
            float nearStep   = max(lodNearStep, 0.5f);
            float farStep    = max(lodFarStep, nearStep);
            float shellEnd   = min(t1, lodDetailDist);
            float fadeEnd    = min(t1, lodDetailDist + max(lodFadeDist, 0.0f));
            if (shellEnd > t0)
            {
                float span = shellEnd - t0;
                int   n    = LodStepCount(span, nearStep, lodMaxNear);
                MarchUniform(t0, shellEnd, n, span / float(max(n, 1)), jitter, 4.0f, 1.0f, nearLights, v, rayDir, scatter, T);
            }
            if (T >= 0.012f && fadeEnd > max(t0, lodDetailDist))
                MarchFade(max(t0, lodDetailDist), fadeEnd, jitter, nearLights, v, rayDir, scatter, T);
            float farStart = max(t0, lodDetailDist + max(lodFadeDist, 0.0f));
            if (T >= 0.012f && t1 > farStart)
            {
                float span = t1 - farStart;
                int   n    = LodStepCount(span, farStep, lodMaxFar);
                MarchUniform(farStart, t1, n, span / float(max(n, 1)), jitter, 2.0f, 0.0f, farLights, v, rayDir, scatter, T);
            }
        }
        if (T < 0.012f)
            break;
    }

    if (T > 0.995f && dot(scatter, scatter) < 1e-8f)
        discard;

    return float4(min(scatter, 8.0.xxx), saturate(T));
}
