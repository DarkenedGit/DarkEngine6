// Lakes and streams share this shader. Vertex Y is the rest height (water level
// on a lake, bed plus clearance on a ribbon). VS and PS add the same four swells
// plus the same shore chop. Crest phase is warped so the sun does not sit on
// straight rows. The pixel shader then adds a gated detail normal and noise.
// Far banks: the 1.6 m / 0.9 m chop is under the LOD 2 grid, so a distant
// cliff line is long swell plus foam shading, not a tessellated silhouette.
// TEXCOORD0.x is the rectangle fade on a lake and centerline-to-bank on a ribbon.
// TEXCOORD0.y is baked terrain height. Stream width rides NORMAL.x (lakes write 0).
// TANGENT.w is the stream-to-lake blend. Lakes write (0,0,0,0).
#pragma pack_matrix(row_major)

#include "Color.hlsli"
#ifndef ENCODE_SRGB
#define ENCODE_SRGB 0
#endif
#include "PbrLighting.hlsli"
#define SHADOW_T t2
#include "Shadow.hlsli"
#define FOG_SAMPLE_CSM 1
#include "Fog.hlsli"
#include "SsrMarch.hlsli"

#define kWaterRoughness 0.15f
#define kTwoPi 6.28318530718f

cbuffer FrameConstants : register(b0)
{
    float4x4 worldViewProj;
    float3   cameraPos;
    float    time;
    float3   lightDir;
    float    waterLevel;
    float2   flowDir;
    float    flowStrength;
    float    specPower;
    float4   waves[4];     // xy dir, z freq, w amp
    float4   waveSpeed;
    float3   deepColor;
    float    opacity;
    float3   shallowColor;
    float    shoreDepth;
    float3   skyZenith;
    float    fresnelF0;
    float3   skyHorizon;
    float    steepness;
    uint     lightCount;
    // Consecutive scalars pack like C++ uint waterIndex[8]. A cbuffer array would be 16-byte strided.
    uint     waterIndex0;
    uint     waterIndex1;
    uint     waterIndex2;
    uint     waterIndex3;
    uint     waterIndex4;
    uint     waterIndex5;
    uint     waterIndex6;
    uint     waterIndex7;
    float3   fogColor;
    float    fogDensity;
    float    heightFogDensity;
    float    heightFogFalloff;
    float    heightFogHeight;
    float    volumetricFogDensity;
    float    volumetricHeight;
    float3   lightColor;
    float    heightOriginX;
    float3   ambientColor;
    float    heightOriginZ;
    float    heightCellSize;
    float    heightWorldSizeX;
    float    heightWorldSizeZ;
    float    _fogPad;
    float4x4 invViewProj;
    float4x4 viewProj;
    float    nearZ;
    float    ssrEnabled;
    float    thickness;
    float    stride;
    float    edgeFade;
    float    maxRoughness;
    float    _ssrPad0;
    float    _ssrPad1;
    float3   sunDir;    float coverage;
    float3   sunColor;  float turbidity;
    float3   moonDir;   float rain;
    float3   moonColor; float windSpeed;
    float2   windDir;   float sunElevation; float exposure;
    float    cloudTime; float3 _skyPad;
    float4   clLightColor;
    float4   clLightDir;
    float4   clSkyTop;
    float4   clSkyBottom;
    float4   clWind;
    float    foamAmount;
    float    foamWidthScale;
    float    flowSpeed;
    float    detailAmount;
};

#include "SkyEval.hlsli"

Texture2D    gHeightMap  : register(t1);
SamplerState gHeightSamp : register(s0);
Texture2D    gSsrColor   : register(t3);
Texture2D    gDepth      : register(t4);
SamplerState gSsrPoint   : register(s2);

struct GpuLocalLight
{
    float3 pos;
    float  range;
    float3 color;
    float  invRange2;
    float3 dir;
    float  type;
    float  innerCos;
    float  outerCos;
    float  sourceRadius;
    float  pad;
};

StructuredBuffer<GpuLocalLight> gLights : register(t0);

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 tangent  : TANGENT;
};

struct PSInput
{
    float4 position    : SV_POSITION;
    float2 restXZ      : TEXCOORD0;
    float  terrainY    : TEXCOORD1;
    float  bankUv      : TEXCOORD2;
    float  restY       : TEXCOORD3;
    float  streamWidth : TEXCOORD4;
    float4 tangent     : TEXCOORD5;
};

float Hash01(int x, int z)
{
    uint n = asuint(x) * 374761393u ^ asuint(z) * 668265263u;
    n = (n ^ 0x27d4eb2du) * 1274126177u;
    n = n ^ (n >> 16);
    return float(n & 0x00FFFFFFu) / 16777216.0f;
}

float ValueNoise(float2 xz)
{
    float2 i = floor(xz);
    float2 f = xz - i;
    float2 u = f * f * (3.0f - 2.0f * f);
    int ix = int(i.x);
    int iz = int(i.y);
    float h00 = Hash01(ix, iz);
    float h10 = Hash01(ix + 1, iz);
    float h01 = Hash01(ix, iz + 1);
    float h11 = Hash01(ix + 1, iz + 1);
    float a = lerp(h00, h10, u.x);
    float b = lerp(h01, h11, u.x);
    return lerp(a, b, u.y);
}

// Same UV as FogSampleTerrainY. 1e6 is that function's out-of-range sentinel.
// A sentinel tap makes the slope huge and would foam the map border, so any
// tap at or above 1e5 drops the shore weight to 0.
float SampleHeight(float2 xz)
{
    if (heightCellSize <= 1.0e-6f)
        return 1.0e6f;
    float2 uv = (xz - float2(heightOriginX, heightOriginZ)) / max(float2(heightWorldSizeX, heightWorldSizeZ), float2(1.0e-3f, 1.0e-3f));
    if (any(uv < 0.0f) || any(uv > 1.0f))
        return 1.0e6f;
    return gHeightMap.SampleLevel(gHeightSamp, uv, 0).r;
}

bool SampleBed(float2 xz, out float bedY, out float slope, out float2 downslope)
{
    bedY = 1.0e6f;
    slope = 0.0f;
    downslope = float2(1.0f, 0.0f);
    float h = heightCellSize;
    float hC = SampleHeight(xz);
    float hL = SampleHeight(xz - float2(h, 0.0f));
    float hR = SampleHeight(xz + float2(h, 0.0f));
    float hD = SampleHeight(xz - float2(0.0f, h));
    float hU = SampleHeight(xz + float2(0.0f, h));
    if (hC >= 1.0e5f || hL >= 1.0e5f || hR >= 1.0e5f || hD >= 1.0e5f || hU >= 1.0e5f)
        return false;
    bedY = hC;
    float2 g = float2(hR - hL, hU - hD) * (0.5f / max(h, 1.0e-4f));
    slope = length(g);
    if (slope > 1.0e-5f)
        downslope = -g / slope;
    return true;
}

float ShoreWeight(float surfaceY, float bedY, float slope, float maxAmp)
{
    float depth = max(surfaceY - bedY, 0.0f);
    float horiz = slope > 1.0e-3f ? depth / slope : (depth < 0.05f ? 0.0f : 1.0e4f);
    float scale = max(foamWidthScale, 0.0f);
    float slopeT = saturate(slope / 0.7f);
    float ampT = saturate(max(maxAmp, 0.0f) / 0.75f);
    float band = 8.0f * scale;
    band *= lerp(1.15f, 0.45f, slopeT);
    band *= lerp(0.85f, 1.25f, ampT);
    band = clamp(band, 2.5f, 14.0f);
    return saturate(1.0f - horiz / max(band, 1.0e-4f));
}

float2 FlowHeading(float2 lakeDir, float2 tangentXZ, float w)
{
    // w == 0 is the lake heading before any degenerate check.
    if (w <= 0.0f)
        return lakeDir;
    float tLen = length(tangentXZ);
    float2 t = (tLen > 1.0e-5f) ? (tangentXZ / tLen) : lakeDir;
    float2 raw = lerp(lakeDir, t, saturate(w));
    if (length(raw) < 1.0e-4f)
        return t;
    return normalize(raw);
}

void AccumGerstner(float2 D, float k, float A, float speed, float Q, float2 xz, float phase, inout float3 offset, inout float3 partial)
{
    if (A <= 0.0f || k <= 0.0f)
        return;
    float dp = dot(D, xz) * k + time * speed + phase;
    float s = sin(dp);
    float c = cos(dp);
    float wa = k * A;
    offset.y += A * s;
    offset.xz += D * (Q * A * c);
    partial.x += D.x * wa * c;
    partial.z += D.y * wa * c;
    partial.y -= Q * wa * s;
}

void AddSlope(inout float3 n, float2 dir, float k, float A, float phase)
{
    float wa = k * A;
    float c = cos(phase);
    n.x -= dir.x * wa * c;
    n.z -= dir.y * wa * c;
}

float2 RotateFlow(float2 flow, float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return float2(flow.x * c - flow.y * s, flow.x * s + flow.y * c);
}

// Value-noise slope. The domain is rotated by the caller so the lattice is not the world axes.
void AddNoiseSlope(inout float3 n, float2 xz, float freq, float gain)
{
    float e = 0.55f;
    float2 p = xz * freq;
    float n0 = ValueNoise(p);
    float nx = ValueNoise(p + float2(e, 0.0f));
    float nz = ValueNoise(p + float2(0.0f, e));
    n.x -= (nx - n0) * gain;
    n.z -= (nz - n0) * gain;
}

// Detail speeds are literals. They match nothing on the CPU.
// Straight sines become glint rows. Warp the sample, gate the sine, and add a
// rotated noise slope so a crest cannot run across the lake.
void AddDetail(inout float3 n, float2 xz, float2 flow)
{
    float amount = saturate(detailAmount);
    if (amount <= 0.0f)
        return;
    float scroll = flowSpeed * time;
    float2 drifted = xz - flow * scroll;

    float2 warp;
    warp.x = ValueNoise(drifted * 0.23f) - 0.5f;
    warp.y = ValueNoise(drifted * 0.23f + float2(4.2f, 9.7f)) - 0.5f;
    float2 sampleXz = drifted + warp * 5.0f;

    float gate = smoothstep(0.15f, 0.82f, ValueNoise(drifted * 0.19f + float2(2.2f, 6.8f)));
    float sine = amount * lerp(0.20f, 0.55f, gate);

    float2 d0 = RotateFlow(flow, 0.40f);
    float k0 = kTwoPi / 2.70f;
    AddSlope(n, d0, k0, 0.08f * sine, dot(d0, sampleXz) * k0 + time * 1.35f);

    float2 d1 = RotateFlow(flow, 1.70f);
    float k1 = kTwoPi / 1.45f;
    AddSlope(n, d1, k1, 0.05f * sine, dot(d1, sampleXz) * k1 + time * 1.85f);

    float2 d2 = RotateFlow(flow, 2.80f);
    float k2 = kTwoPi / 0.83f;
    AddSlope(n, d2, k2, 0.03f * sine, dot(d2, sampleXz) * k2 + time * 2.45f);

    float2 d3 = RotateFlow(flow, -0.90f);
    float k3 = kTwoPi / 0.47f;
    AddSlope(n, d3, k3, 0.02f * sine, dot(d3, sampleXz) * k3 + time * 3.05f);

    float2 spun = RotateFlow(drifted, 0.77f);
    AddNoiseSlope(n, spun, 0.55f, 0.70f * amount);
    AddNoiseSlope(n, spun + float2(20.0f, 7.0f), 1.70f, 0.38f * amount);
}

void AddStreamRipples(inout float3 n, float2 xz, float w, float2 tangentXZ)
{
    if (w <= 0.0f)
        return;
    float tLen = length(tangentXZ);
    if (tLen <= 1.0e-5f)
        return;
    float2 t = tangentXZ / tLen;
    float k0 = kTwoPi / 1.30f;
    float k1 = kTwoPi / 0.60f;
    AddSlope(n, t, k0, 0.05f * w, dot(t, xz) * k0 - time * flowSpeed * k0);
    AddSlope(n, t, k1, 0.025f * w, dot(t, xz) * k1 - time * flowSpeed * k1);
}

float RibbonBank(float bankUv, float width)
{
    float fromBank = saturate(bankUv) * width * 0.5f;
    float band = min(width * 0.5f * max(foamWidthScale, 0.0f), 0.45f * width);
    return saturate(1.0f - fromBank / max(band, 1.0e-3f));
}

// Both stages call this. Shore depth is the swell, before chop, so chop cannot feed itself.
void EvalSurface(
    float2 xz,
    float restY,
    float4 tangent,
    float streamWidth,
    float bankUv,
    float bakedBed,
    out float3 offset,
    out float3 dispNormal,
    out float3 shadedNormal,
    out float shore,
    out float foam,
    out float depth)
{
    float w = saturate(tangent.w);
    float Q = saturate(steepness);
    offset = 0.0f.xxx;
    float3 partial = float3(0.0f, 1.0f, 0.0f);

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float2 D = FlowHeading(waves[i].xy, tangent.xz, w);
        float amp = waves[i].w * lerp(1.0f, 0.35f, w);
        // Longer swells bend a little. The short ones scramble so their crests do not rule the lake.
        float seed = float(i) * 17.0f + 3.0f;
        float jitterAmp = lerp(2.2f, 5.5f, float(i) * (1.0f / 3.0f));
        float jitter = (ValueNoise(xz * 0.037f + seed) - 0.5f) * jitterAmp;
        AccumGerstner(D, waves[i].z, amp, waveSpeed[i], Q, xz, jitter, offset, partial);
    }

    float swellY = offset.y;
    float maxAmp = waves[0].w + waves[1].w + waves[2].w + waves[3].w;
    float bedY = bakedBed;
    float slope = 0.0f;
    float2 down = float2(1.0f, 0.0f);
    bool bedOk = SampleBed(xz, bedY, slope, down);
    float lakeShore = 0.0f;
    if (bedOk)
        lakeShore = ShoreWeight(restY + swellY, bedY, slope, maxAmp);
    else
        bedY = bakedBed;

    bool stream = streamWidth > 0.5f;
    float ribbon = stream ? RibbonBank(bankUv, streamWidth) : 0.0f;
    shore = max(lakeShore, ribbon);

    float chopQ = Q + 0.35f;
    float2 across = float2(-down.y, down.x);
    AccumGerstner(down, kTwoPi / 1.6f, 0.12f * shore, 2.4f, chopQ, xz, 0.0f, offset, partial);
    AccumGerstner(across, kTwoPi / 0.9f, 0.06f * shore, 3.1f, chopQ, xz, 0.0f, offset, partial);

    dispNormal = normalize(float3(-partial.x, partial.y, -partial.z));
    float3 nrm = dispNormal;
    float2 flow = flowDir;
    float flowLen = length(flow);
    flow = (flowLen > 1.0e-5f) ? (flow / flowLen) : float2(1.0f, 0.0f);
    AddDetail(nrm, xz, flow);
    AddStreamRipples(nrm, xz, w, tangent.xz);
    shadedNormal = normalize(nrm);

    depth = max(restY + swellY - bedY, 0.0f);

    float2 flowScroll = flow * flowSpeed * time;
    float n0 = ValueNoise(xz * 0.22f + flowScroll * 0.15f);
    float n1 = ValueNoise(xz * 0.57f - time * float2(0.07f, 0.04f));
    float broken = saturate(shore * 1.35f - 0.25f + 0.55f * n0 + 0.25f * n1);
    float crest = saturate(shore * saturate(0.5f - dispNormal.y));
    foam = smoothstep(0.35f, 0.72f, saturate(broken * 0.85f + crest * 0.5f)) * saturate(foamAmount);
}

float3 SkyColor(float3 dir)
{
    float t = saturate(dir.y * 0.5f + 0.5f);
    return lerp(skyHorizon, skyZenith, t);
}

PSInput VSMain(VSInput input)
{
    PSInput o;
    float3 offset;
    float3 dispN;
    float3 shaded;
    float shore;
    float foam;
    float depth;
    EvalSurface(input.position.xz, input.position.y, input.tangent, input.normal.x, input.uv.x, input.uv.y, offset, dispN, shaded, shore, foam, depth);
    float3 world = float3(input.position.x, input.position.y, input.position.z) + offset;
    o.restXZ      = input.position.xz;
    o.terrainY    = input.uv.y;
    o.bankUv      = input.uv.x;
    o.restY       = input.position.y;
    o.streamWidth = input.normal.x;
    o.tangent     = input.tangent;
    o.position    = mul(float4(world, 1.0f), worldViewProj);
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 offset;
    float3 dispNormal;
    float3 n;
    float shore;
    float foam;
    float depth;
    EvalSurface(input.restXZ, input.restY, input.tangent, input.streamWidth, input.bankUv, input.terrainY, offset, dispNormal, n, shore, foam, depth);

    bool stream = input.streamWidth > 0.5f;
    float shallow = saturate(1.0f - depth / max(shoreDepth, 1.0e-3f));
    float3 body = lerp(deepColor, shallowColor, shallow);
    body *= lerp(1.0f, 0.2f, foam);
    float3 foamColor = float3(0.85f, 0.88f, 0.86f);

    float edge = stream ? 1.0f : saturate(input.bankUv);
    float metersIn = input.bankUv * (input.streamWidth * 0.5f);
    float rim = stream ? smoothstep(0.0f, 0.10f, metersIn) : 1.0f;
    float alphaDepth = saturate(depth / max(shoreDepth * 0.35f, 1.0e-3f));
    float alpha = opacity * alphaDepth * edge * rim;

    float3 worldPos = float3(input.restXZ.x, input.restY, input.restXZ.y) + offset;

    if (specPower < 0.0f)
    {
        float3 unlit = lerp(body, foamColor, foam);
        alpha = max(alpha, foam * 0.85f);
        return float4(encodeSceneRgb(unlit), saturate(alpha));
    }

    float3 v = normalize(cameraPos - worldPos);
    float3 l = normalize(lightDir);
    float ndotv = saturate(dot(n, v));
    float fres = fresnelF0 + (1.0f - fresnelF0) * pow(1.0f - ndotv, 5.0f);
    float rough = lerp(kWaterRoughness, 0.55f, foam);

    float3 r = reflect(-v, n);
    float3 reflection = SkyColor(r);
    if (ssrEnabled >= 0.5f && dot(n, v) > 0.0f && kWaterRoughness <= maxRoughness)
    {
        reflection = EvaluateSky(r);
        uint w;
        uint hTex;
        gDepth.GetDimensions(w, hTex);
        SsrHit h = SsrMarch(worldPos, r, ndotv, kWaterRoughness, 0.0f, float2(w, hTex), gDepth, gSsrColor, gSsrPoint, viewProj, nearZ, thickness, stride,
                            edgeFade, maxRoughness, kSsrWaterMaxSteps);
        if (h.kind == SSR_HIT)
            reflection = lerp(reflection, h.radiance, saturate(h.conf));
    }
    reflection = lerp(reflection, body, foam);

    float ndotl = saturate(dot(n, l));
    float3 color = body * (0.18f + 0.55f * ndotl);
    color = lerp(color, reflection, fres);
    color += PbrDirectional(n, v, 0.0f.xxx, rough, 0.0f, l, 0.85f.xxx);

    if (lightCount > 0)
    {
        const uint idx[8] = { waterIndex0, waterIndex1, waterIndex2, waterIndex3, waterIndex4, waterIndex5, waterIndex6, waterIndex7 };
        [unroll]
        for (uint li = 0; li < 8; ++li)
        {
            if (li < lightCount)
            {
                GpuLocalLight light = gLights[idx[li]];
                float3 toLight = light.pos - worldPos;
                float d = length(toLight);
                float3 liDir = toLight / max(d, 1.0e-4f);
                float cosTheta = dot(-liDir, light.dir);
                float3 lit = PbrPunctual(n, v, body, rough, 0.0f, toLight, light.color, light.sourceRadius);
                lit *= windowedDistanceAttenuation(d * d, light.invRange2);
                if (light.type >= 0.5f)
                    lit *= spotAngleAttenuation(cosTheta, light.innerCos, light.outerCos);
                color += lit;
            }
        }
    }

    color = lerp(color, foamColor, foam);

    FogParams fp;
    fp.cameraPos            = cameraPos;
    fp.lightDir             = lightDir;
    fp.lightColor           = lightColor;
    fp.ambientColor         = ambientColor;
    fp.fogColor             = fogColor;
    fp.fogDensity           = fogDensity;
    fp.heightFogDensity     = heightFogDensity;
    fp.heightFogFalloff     = heightFogFalloff;
    fp.heightFogHeight      = heightFogHeight;
    fp.volumetricFogDensity = volumetricFogDensity;
    fp.waterLevel           = waterLevel;
    fp.volumetricHeight     = volumetricHeight;
    fp.heightOrigin         = float2(heightOriginX, heightOriginZ);
    fp.heightCellSize       = heightCellSize;
    fp.heightWorldSize      = float2(heightWorldSizeX, heightWorldSizeZ);
    FogResult fogResult = FogIntegrate(cameraPos, worldPos, fp, gHeightMap, gHeightSamp, 1.0f);
    color = ApplyLitFog(color, fogResult);

    // Fresnel boost is not multiplied by the rectangle edge. Foam stays opaque on a ribbon bank.
    alpha += fres * 0.15f * (1.0f - foam);
    alpha = max(alpha, foam * 0.85f);
    return float4(encodeSceneRgb(color), saturate(alpha));
}
