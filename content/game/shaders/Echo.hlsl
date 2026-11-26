// Echolocation. Compile PSSnapshot with ECHO_SNAPSHOT. Compile PSComposite without it.
// The snapshot stores range and echo strength. A porous rough cluster (a crown or a
// grass layer) stores echo + 2 so the composite can hold the whole clump. Echo already
// scales with density and size: a filled crown returns more than grass, and a small
// prop returns less than a wall. The composite reprojects that frozen chirp and lerps
// HDR toward it. Strength falls to nothing as range reaches the chirp limit.
// Live normals are not read.
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

// Smooth facing surfaces ping. Rough solids still return; a grazing face stays quiet.
float EchoStrength(float facing, float roughness)
{
    float lobe = saturate(facing);
    lobe *= lobe;
    float hard = saturate(1.0f - roughness);
    return lobe * lerp(0.50f, 1.0f, hard);
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

// 0.2 m props stay dim. Something that spans the sample window reads at full size.
float SizeScale(float extentMeters, bool fillsWindow)
{
    float extent = fillsWindow ? max(extentMeters, 1.70f) : extentMeters;
    float t = saturate((extent - 0.12f) / 1.55f);
    t = t * t * (3.0f - 2.0f * t);
    return lerp(0.22f, 1.0f, t);
}

float2 PSSnapshot(PSInput input) : SV_TARGET
{
    float2 scale = float2(fullWidth, fullHeight) / float2(max(snapWidth, 1.0f), max(snapHeight, 1.0f));
    int2 limit = int2(max(fullWidth, 1.0f), max(fullHeight, 1.0f)) - 1;
    int2 block = max(int2(scale + 0.5f), int2(1, 1));
    int2 origin = clamp(int2(input.position.xy) * block, int2(0, 0), limit);

    // The 8x8 block's closest hit. A leaf in the block wins over a hole at the corner,
    // which is what used to drop whole crowns.
    int2 anchor = origin;
    float anchorRange = 1e9f;
    bool found = false;
    [unroll]
    for (int by = 0; by < 4; ++by)
    {
        [unroll]
        for (int bx = 0; bx < 4; ++bx)
        {
            int2 texel = origin + int2(((bx * 2 + 1) * block.x) / 8, ((by * 2 + 1) * block.y) / 8);
            float3 tap = EchoAt(texel);
            if (tap.x <= 0.0f)
                continue;
            if (!found || tap.x < anchorRange)
            {
                found = true;
                anchorRange = tap.x;
                anchor = texel;
            }
        }
    }
    if (!found)
        return float2(0.0f, 0.0f);

    float depth = gDepth.Load(int3(anchor, 0)).r;
    float2 ndc = NdcFromTexel(anchor, fullWidth, fullHeight);
    float3 world = ReconstructWorldPos(ndc.x, ndc.y, depth, invViewProj);
    int2 side = clamp(anchor + int2(1, 0), int2(0, 0), limit);
    float2 ndcSide = NdcFromTexel(side, fullWidth, fullHeight);
    float3 worldSide = ReconstructWorldPos(ndcSide.x, ndcSide.y, depth, invViewProj);
    float metersPerPixel = max(length(worldSide - world), 1e-4f);
    int stepPx = (int)clamp(0.32f / metersPerPixel + 0.5f, 1.0f, 220.0f);

    // Inner 3x3 is density. The outer ring is size. 0.30 m keeps grass tips off the ground;
    // 0.50 m still joins a crown's front shell.
    const float kStepM = 0.32f;
    const float kFillBand = 0.30f;
    const float kExtentBand = 0.50f;

    int innerHits = 0;
    int innerNear = 0;
    float sumE = 0.0f;
    float sumE2 = 0.0f;
    float sumR = 0.0f;
    float extent = 0.0f;
    float sumSpan = 0.0f;
    int spanCount = 0;
    int posX = 0;
    int negX = 0;
    int posY = 0;
    int negY = 0;

    [loop]
    for (int dy = -2; dy <= 2; ++dy)
    {
        [loop]
        for (int dx = -2; dx <= 2; ++dx)
        {
            int2 texel = clamp(anchor + int2(dx, dy) * stepPx, int2(0, 0), limit);
            float3 tap = EchoAt(texel);
            if (tap.x <= 0.0f)
                continue;

            float lateral = length(float2((float)dx, (float)dy)) * kStepM;
            bool inner = abs(dx) <= 1 && abs(dy) <= 1;
            bool near = abs(tap.x - anchorRange) <= kFillBand;
            // Depth of this clump, ignoring hills far behind a hole. Grass sits on ground
            // half a metre back. A crown still has leaves metres deeper than its front.
            if (tap.x <= anchorRange + 5.0f)
            {
                sumSpan += tap.x - anchorRange;
                spanCount += 1;
            }
            if (inner)
            {
                innerHits += 1;
                if (near)
                {
                    innerNear += 1;
                    sumE += tap.y;
                    sumE2 += tap.y * tap.y;
                    sumR += tap.z;
                }
            }

            if (abs(tap.x - anchorRange) > kExtentBand)
                continue;
            extent = max(extent, lateral);
            if (abs(dx) != 2 && abs(dy) != 2)
                continue;
            if (dx == 2)
                posX = 1;
            if (dx == -2)
                negX = 1;
            if (dy == 2)
                posY = 1;
            if (dy == -2)
                negY = 1;
        }
    }

    if (innerNear < 1)
        return float2(anchorRange, 0.0f);

    float meanE = sumE / (float)innerNear;
    float meanR = sumR / (float)innerNear;
    float varE = 0.0f;
    if (innerNear > 1)
        varE = max(sumE2 / (float)innerNear - meanE * meanE, 0.0f);

    // fill is the front surface's share of the rays that hit anything. Sky does not dilute a crown.
    // Grass stays low because most rays hit the ground behind the blades. A card crown stays high.
    float fill = (float)innerNear / (float)max(innerHits, 1);
    bool gappy = innerHits >= innerNear + 2;
    // A flat rough surface (terrain, bark) keeps the facing response. Grass is gappy: rays fall
    // through to the ground. A crown of crossed cards scatters even when the shell itself is filled.
    bool volume = meanR >= 0.50f && innerNear >= 3 && (gappy || varE >= 0.025f);
    bool spans = (posX + negX) == 2 || (posY + negY) == 2;
    float sizeScale = SizeScale(extent, spans);

    if (volume)
    {
        // Coverage squared keeps sparse grass light. Depth keeps a thin sheet under a crown:
        // grass averages a fraction of a metre, a birch crown averages much more.
        float cover = saturate(fill);
        float meanSpan = spanCount > 0 ? sumSpan / (float)spanCount : 0.0f;
        float bulk = saturate(meanSpan / 1.25f);
        float depthScale = lerp(0.30f, 1.0f, bulk);
        float echo = cover * cover * depthScale * sizeScale;
        return float2(anchorRange, min(echo, 1.2f) + 2.0f);
    }

    return float2(anchorRange, saturate(meanE * sizeScale));
}

#else

float3 EchoColor(float range, float echo, float shellRadius, float edgeWidth, float farMeters)
{
    if (range <= 0.0f || echo <= 1e-4f || range > shellRadius)
        return float3(0.0f, 0.0f, 0.0f);

    float edge = 1.0f - saturate((shellRadius - range) / max(edgeWidth, 1e-3f));
    float dist = saturate(1.0f - range / max(farMeters, 1e-3f));
    // Held body stays under the bloom threshold. A hard near edge may sit just above 1.
    // dist is 0 at the chirp limit. The return fades across the outer part of the range
    // and is gone when the shell reaches that limit.
    float falloff = smoothstep(0.0f, 0.42f, dist);
    float body = echo * lerp(0.12f, 0.85f, dist);
    float lead = echo * lerp(0.25f, 1.35f, dist);
    float luma = lerp(body, lead, edge) * falloff;

    float3 hue   = float3(0.55f, 0.78f, 1.0f);
    float  paper = saturate(luma);
    return lerp(hue, float3(1.0f, 1.0f, 1.0f), paper) * luma;
}

float MatchTol(float range, float carpet)
{
    if (carpet > 0.5f)
        return 2.25f;
    return rangeTol + range * 0.04f;
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

                        // Porous cluster (crown, grass layer) is stored as echo + 2.
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
                    float tol = MatchTol(range, carpet > 0.1f ? 1.0f : 0.0f);

                    // Soft threshold for range test using smoothstep.
                    float rangeDiff = abs(live - range);
                    float rangeMatch = 1.0f - smoothstep(tol * 0.75f, tol * 1.25f, rangeDiff);
                    if (rangeMatch > 1e-3f)
                        echoRgb = EchoColor(range, echo, radius, edgeMeters, maxRange) * rangeMatch;
                }

                // A mixed block stores the front surface (the crown). Ground pixels in that
                // block look one snap neighborhood over for their own range. Skip pixels the
                // shell has not reached; those stay black without a wider search.
                if (dot(echoRgb, echoRgb) <= 1e-8f && live <= radius)
                {
                    int2 snapCenter = clamp(int2(uv * snapSize), int2(0, 0), int2(snapSize) - 1);
                    float bestDiff = 1e9f;
                    float bestEcho = 0.0f;
                    float bestRange = 0.0f;
                    float bestCarpet = 0.0f;
                    [loop]
                    for (int sy = -2; sy <= 2; ++sy)
                    {
                        [loop]
                        for (int sx = -2; sx <= 2; ++sx)
                        {
                            int2 tapCoord = clamp(snapCenter + int2(sx, sy), int2(0, 0), int2(snapSize) - 1);
                            float2 tapSnap = gSnap.Load(int3(tapCoord, 0)).rg;
                            float tapRange = tapSnap.r;
                            float tapEcho = tapSnap.g;
                            if (tapRange <= 0.0f)
                                continue;
                            float tapCarpet = 0.0f;
                            if (tapEcho >= 1.5f)
                            {
                                tapEcho -= 2.0f;
                                tapCarpet = 1.0f;
                            }
                            float diff = abs(live - tapRange);
                            float tapTol = MatchTol(tapRange, tapCarpet);
                            if (diff < tapTol && diff < bestDiff)
                            {
                                bestDiff = diff;
                                bestEcho = tapEcho;
                                bestRange = tapRange;
                                bestCarpet = tapCarpet;
                            }
                        }
                    }
                    if (bestDiff < 1e8f)
                    {
                        float tapTol = MatchTol(bestRange, bestCarpet);
                        float rangeMatch = 1.0f - smoothstep(tapTol * 0.75f, tapTol * 1.25f, bestDiff);
                        if (rangeMatch > 1e-3f)
                            echoRgb = EchoColor(bestRange, bestEcho, radius, edgeMeters, maxRange) * rangeMatch;
                    }
                }
            }
        }
    }

    return float4(lerp(beauty, echoRgb, fade), 1.0f);
}

#endif
