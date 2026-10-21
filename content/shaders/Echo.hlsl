// Echolocation. Compile PSSnapshot with ECHO_SNAPSHOT. Compile PSComposite without it.
// The snapshot stores range and echo strength. A grass or plant clump stores echo + 2
// so the composite can light the whole clump. The composite reprojects that frozen
// chirp and lerps HDR toward it. Live normals are not read.
#pragma pack_matrix(row_major)

#include "GBuffer.hlsli"

#if defined(ECHO_SNAPSHOT)

cbuffer EchoSnapConstants : register(b0)
{
    float4x4 invViewProj;
    float3   cameraPos;
    float    fullWidth;
    float    fullHeight;
    float    snapWidth;
    float    snapHeight;
    float    _snapPad;
};

Texture2D gAttrib : register(t0);
Texture2D gDepth  : register(t1);

#else

cbuffer EchoViewConstants : register(b0)
{
    float4x4 currInvViewProj;
    float4x4 chirpViewProj;
    float3   chirpCamera;
    float    radius;
    float    strength;
    float    fullWidth;
    float    fullHeight;
    float    rangeTol;
    float    edgeMeters;
    float    maxRange;
    float    _viewPad0;
    float    _viewPad1;
};

Texture2D    gSnap   : register(t0);
Texture2D    gDepth  : register(t1);
Texture2D    gBeauty : register(t2);
SamplerState gPoint  : register(s0);

#endif

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

float2 NdcFromTexel(int2 texel, float width, float height)
{
    float2 uv = (float2(texel) + 0.5f) / float2(max(width, 1.0f), max(height, 1.0f));
    return float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
}

// Smooth facing surfaces ping. Rough foliage stays soft, not silent.
float EchoStrength(float facing, float roughness)
{
    float lobe = saturate(facing);
    lobe *= lobe;
    float hard = saturate(1.0f - roughness);
    return lobe * lerp(0.18f, 1.0f, hard);
}

#if defined(ECHO_SNAPSHOT)

// x = range (0 is a miss), y = echo, z = roughness.
float3 EchoAt(int2 texel)
{
    int2 limit = int2(max(fullWidth, 1.0f), max(fullHeight, 1.0f)) - 1;
    texel = clamp(texel, int2(0, 0), max(limit, int2(0, 0)));

    float depth = gDepth.Load(int3(texel, 0)).r;
    if (IsSkyDepth(depth))
        return float3(0.0f, 0.0f, 1.0f);

    float2 ndc    = NdcFromTexel(texel, fullWidth, fullHeight);
    float3 world  = ReconstructWorldPos(ndc.x, ndc.y, depth, invViewProj);
    float3 toCam  = cameraPos - world;
    float  range  = length(toCam);
    float3 view   = toCam / max(range, 1e-4f);
    float4 attrib = gAttrib.Load(int3(texel, 0));
    float  rough  = saturate(attrib.b);
    float  echo   = EchoStrength(dot(DecodeOct(attrib.rg), view), rough);
    return float3(max(range, 1e-3f), echo, rough);
}

float2 PSSnapshot(PSInput input) : SV_TARGET
{
    float2 scale = float2(fullWidth, fullHeight) / float2(max(snapWidth, 1.0f), max(snapHeight, 1.0f));
    // SV_POSITION is the pixel center, so this lands on the middle texel of the block.
    int2 full = int2(input.position.xy * scale);
    int2 limit = int2(max(fullWidth, 1.0f), max(fullHeight, 1.0f)) - 1;
    full = clamp(full, int2(0, 0), max(limit, int2(0, 0)));

    float3 center = EchoAt(full);
    if (center.x <= 0.0f)
        return float2(0.0f, 0.0f);
    // A smooth surface, and anything that is not rough, keeps one hard sample.
    if (center.z < 0.50f)
        return float2(center.x, center.y);

    // Meters per pixel at this depth. The old kernel stepped one block, which is a
    // single blade up close, so the field stayed sharp.
    float depth = gDepth.Load(int3(full, 0)).r;
    float2 ndc = NdcFromTexel(full, fullWidth, fullHeight);
    float3 world = ReconstructWorldPos(ndc.x, ndc.y, depth, invViewProj);
    int2 side = clamp(full + int2(1, 0), int2(0, 0), max(limit, int2(0, 0)));
    float2 ndcSide = NdcFromTexel(side, fullWidth, fullHeight);
    float3 worldSide = ReconstructWorldPos(ndcSide.x, ndcSide.y, depth, invViewProj);
    float metersPerPixel = max(length(worldSide - world), 1e-4f);
    float stepPx = clamp(0.50f / metersPerPixel, max(scale.x, 1.0f), 240.0f);
    int step = max(int(stepPx + 0.5f), 1);

    // Two metres across. Blades disagree. A hill or a body agrees with itself.
    float band = max(4.0f, center.x * 0.50f);
    float sumE = 0.0f;
    int count = 0;
    int disagree = 0;
    [unroll]
    for (int y = -2; y <= 2; ++y)
    {
        [unroll]
        for (int x = -2; x <= 2; ++x)
        {
            if (x == 0 && y == 0)
                continue;
            float3 tap = EchoAt(full + int2(x, y) * step);
            if (tap.x <= 0.0f || tap.z < 0.50f || abs(tap.x - center.x) > band)
                continue;
            sumE += tap.y;
            count += 1;
            if (abs(tap.y - center.y) > 0.07f)
                disagree += 1;
        }
    }

    if (count >= 6 && disagree * 2 >= count)
    {
        float mean = (sumE + center.y) / float(count + 1);
        float flat = lerp(mean, 0.30f, 0.88f);
        return float2(center.x, flat + 2.0f);
    }
    return float2(center.x, center.y);
}

#else

float3 EchoColor(float range, float echo, float shellRadius, float edgeWidth, float farMeters)
{
    if (range <= 0.0f || echo <= 1e-4f || range > shellRadius)
        return float3(0.0f, 0.0f, 0.0f);

    float edge = 1.0f - saturate((shellRadius - range) / max(edgeWidth, 1e-3f));
    float dist = saturate(1.0f - range / max(farMeters, 1e-3f));
    // Held body stays under the bloom threshold. A hard near edge may sit just above 1.
    float body = echo * lerp(0.12f, 0.85f, dist);
    float lead = echo * lerp(0.25f, 1.35f, dist);
    float luma = lerp(body, lead, edge);

    float3 hue   = float3(0.55f, 0.78f, 1.0f);
    float  paper = saturate(luma);
    return lerp(hue, float3(1.0f, 1.0f, 1.0f), paper) * luma;
}

float4 PSComposite(PSInput input) : SV_TARGET
{
    int2   texel   = int2(input.position.xy);
    float3 beauty  = gBeauty.Load(int3(texel, 0)).rgb;
    float  fade    = saturate(strength);
    float  depth   = gDepth.Load(int3(texel, 0)).r;
    float3 echoRgb = float3(0.0f, 0.0f, 0.0f);

    if (!IsSkyDepth(depth))
    {
        float2 ndc   = NdcFromTexel(texel, fullWidth, fullHeight);
        float3 world = ReconstructWorldPos(ndc.x, ndc.y, depth, currInvViewProj);
        float4 clip  = mul(float4(world, 1.0f), chirpViewProj);
        if (clip.w > 1e-4f)
        {
            float2 chirpNdc = clip.xy / clip.w;
            float2 uv       = chirpNdc * float2(0.5f, -0.5f) + 0.5f;
            if (uv.x >= 0.0f && uv.y >= 0.0f && uv.x <= 1.0f && uv.y <= 1.0f)
            {
                float2 snap  = gSnap.SampleLevel(gPoint, uv, 0).rg;
                float  range = snap.r;
                float  echo  = snap.g;
                float  live  = distance(world, chirpCamera);
                float  tol   = rangeTol + range * 0.04f;
                // Carpet flag: grass and plants light the whole clump, not one blade.
                if (echo >= 1.5f)
                {
                    echo -= 2.0f;
                    tol = max(2.5f, range * 0.50f);
                }
                if (abs(live - range) <= tol)
                    echoRgb = EchoColor(range, echo, radius, edgeMeters, maxRange);
            }
        }
    }

    return float4(lerp(beauty, echoRgb, fade), 1.0f);
}

#endif
