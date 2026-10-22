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
    float    snapWidth;
    float    snapHeight;
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
    int2 base = int2(input.position.xy * scale);
    int2 limit = int2(max(fullWidth, 1.0f), max(fullHeight, 1.0f)) - 1;
    base = clamp(base, int2(0, 0), max(limit, int2(0, 0)));

    float3 center = EchoAt(base);
    if (center.x <= 0.0f)
        return float2(0.0f, 0.0f);

    // Prefilter: aggregate a 4x4 grid at stride 2 within the 8x8 block.
    // Weight by depth agreement. Store mean echo and representative range.
    int blockSize = int(max(scale.x, 1.0f));
    int stride = max(blockSize / 4, 1);
    if (stride < 2)
        stride = 2;
    int halfStride = max(stride * 2, 1);

    float refRange = center.x;
    float depthBand = max(4.0f, refRange * 0.50f);
    float sumEcho = 0.0f;
    float sumWeight = 0.0f;
    float nearestRange = refRange;
    float minRangeDist = 1e6f;

    [unroll]
    for (int dy = -1; dy <= 1; dy += 1)
    {
        [unroll]
        for (int dx = -1; dx <= 1; dx += 1)
        {
            int2 offset = int2(dx, dy) * halfStride;
            int2 texel = clamp(base + offset, int2(0, 0), max(limit, int2(0, 0)));
            float3 tap = EchoAt(texel);
            if (tap.x <= 0.0f)
                continue;

            float rangeDiff = abs(tap.x - refRange);
            if (rangeDiff > depthBand)
                continue;

            float weight = 1.0f - saturate(rangeDiff / max(depthBand, 1e-3f));
            weight = weight * weight;
            sumEcho += tap.y * weight;
            sumWeight += weight;

            if (rangeDiff < minRangeDist)
            {
                minRangeDist = rangeDiff;
                nearestRange = tap.x;
            }
        }
    }

    if (sumWeight < 1e-4f)
        return float2(refRange, center.y);

    float meanEcho = sumEcho / sumWeight;
    float rough = center.z;

    // Grass carpet vote: if rough and echo varies, mark as carpet.
    if (rough >= 0.50f)
    {
        float depth = gDepth.Load(int3(base, 0)).r;
        float2 ndc = NdcFromTexel(base, fullWidth, fullHeight);
        float3 world = ReconstructWorldPos(ndc.x, ndc.y, depth, invViewProj);
        int2 side = clamp(base + int2(1, 0), int2(0, 0), max(limit, int2(0, 0)));
        float2 ndcSide = NdcFromTexel(side, fullWidth, fullHeight);
        float3 worldSide = ReconstructWorldPos(ndcSide.x, ndcSide.y, depth, invViewProj);
        float metersPerPixel = max(length(worldSide - world), 1e-4f);
        float stepPx = clamp(0.50f / metersPerPixel, max(scale.x, 1.0f), 240.0f);
        int step = max(int(stepPx + 0.5f), 1);

        float sumE2 = 0.0f;
        int count2 = 0;
        int disagree = 0;
        [unroll]
        for (int y = -2; y <= 2; ++y)
        {
            [unroll]
            for (int x = -2; x <= 2; ++x)
            {
                if (x == 0 && y == 0)
                    continue;
                float3 tap2 = EchoAt(base + int2(x, y) * step);
                if (tap2.x <= 0.0f || tap2.z < 0.50f || abs(tap2.x - refRange) > depthBand)
                    continue;
                sumE2 += tap2.y;
                count2 += 1;
                if (abs(tap2.y - meanEcho) > 0.07f)
                    disagree += 1;
            }
        }

        if (count2 >= 6 && disagree * 2 >= count2)
        {
            float mean2 = (sumE2 + meanEcho) / float(count2 + 1);
            float flat = lerp(mean2, 0.30f, 0.88f);
            return float2(nearestRange, flat + 2.0f);
        }
    }

    return float2(nearestRange, meanEcho);
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
                float live = distance(world, chirpCamera);
                float2 snapSize = float2(max(snapWidth, 1.0f), max(snapHeight, 1.0f));
                float2 snapUv = uv * snapSize - 0.5f;
                int2 snapBase = int2(floor(snapUv));
                float2 frac = snapUv - float2(snapBase);

                // Depth-aware 2x2 bilateral upsample instead of point sampling.
                float sumEcho = 0.0f;
                float sumRange = 0.0f;
                float sumWeight = 0.0f;
                float carpet = 0.0f;
                [unroll]
                for (int dy = 0; dy <= 1; ++dy)
                {
                    [unroll]
                    for (int dx = 0; dx <= 1; ++dx)
                    {
                        int2 tapCoord = clamp(snapBase + int2(dx, dy), int2(0, 0), int2(snapSize) - 1);
                        float2 tapSnap = gSnap.Load(int3(tapCoord, 0)).rg;
                        float tapRange = tapSnap.r;
                        float tapEcho = tapSnap.g;
                        if (tapRange <= 0.0f)
                            continue;

                        // Handle carpet flag per tap.
                        float tapCarpet = 0.0f;
                        if (tapEcho >= 1.5f)
                        {
                            tapEcho -= 2.0f;
                            tapCarpet = 1.0f;
                        }

                        // Bilinear weight x range-agreement weight.
                        float2 bilinear = float2(dx == 0 ? 1.0f - frac.x : frac.x, dy == 0 ? 1.0f - frac.y : frac.y);
                        float spatialWeight = bilinear.x * bilinear.y;

                        float rangeDiff = abs(live - tapRange);
                        float rangeWeight = 1.0f - saturate(rangeDiff / max(tapRange * 0.50f + 4.0f, 1e-3f));
                        rangeWeight = rangeWeight * rangeWeight;

                        float weight = spatialWeight * rangeWeight;
                        sumEcho += tapEcho * weight;
                        sumRange += tapRange * weight;
                        sumWeight += weight;
                        carpet = max(carpet, tapCarpet * weight);
                    }
                }

                if (sumWeight > 1e-4f)
                {
                    float range = sumRange / sumWeight;
                    float echo = sumEcho / sumWeight;
                    float tol = rangeTol + range * 0.04f;
                    if (carpet > 0.1f)
                        tol = max(2.5f, range * 0.50f);

                    // Soft threshold for range test using smoothstep.
                    float rangeDiff = abs(live - range);
                    float rangeMatch = 1.0f - smoothstep(tol * 0.75f, tol * 1.25f, rangeDiff);
                    if (rangeMatch > 1e-3f)
                        echoRgb = EchoColor(range, echo, radius, edgeMeters, maxRange) * rangeMatch;
                }
            }
        }
    }

    return float4(lerp(beauty, echoRgb, fade), 1.0f);
}

#endif
