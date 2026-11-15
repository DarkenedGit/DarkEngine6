// Foliage visibility. CSCompact counts one thread per gathered instance. A prefix and a
// scatter then pack variants into the kind's existing 65536 span. CSArgs writes one
// draw per kind, variant, and opaque part. SM5 SV_InstanceID ignores StartInstanceLocation,
// so the value is the prefix and the vertex shader adds gPrefix itself.
#pragma pack_matrix(row_major)

#include "FoliageWorld.hlsli"

// Match FoliagePipeline::kCullKindCount, kCullPartsPerKind, kGrassKindIndex, kMaxFoliageVariants, and kMaxFoliageDraw.
static const uint kKindCount    = 4;
static const uint kVariants     = 8;
static const uint kPartsPerKind = 8;
static const uint kGrassKind    = 3;
static const uint kMaxInstances = 65536;

cbuffer FoliageCullConstants : register(b0)
{
    float4 planes[6];
    float4 sphere;
    float4 boundsMin[4];
    float4 boundsMax[4];
    uint4  kindResident;
    uint   instanceCount;
    uint   useSphere;
    uint   shadowView;
    uint   viewIndex;
    uint4  partCount;
    uint4  indexCount[8];
    uint4  tail;
};

StructuredBuffer<FoliageWorld> gWorlds  : register(t0);
StructuredBuffer<uint>         gKinds   : register(t1);
StructuredBuffer<uint>         gVariant : register(t2);
// Structured, not RWBuffer. A root UAV cannot bind a typed buffer. Stride is still 4.
RWStructuredBuffer<uint>       gIndices  : register(u0);
RWStructuredBuffer<uint>       gCounters : register(u1);
RWStructuredBuffer<uint>       gArgs     : register(u2);
RWStructuredBuffer<uint>       gPrefix   : register(u3);
RWStructuredBuffer<uint>       gCursor   : register(u4);

uint residentOf(uint kind)
{
    if (kind == 0)
        return kindResident.x;
    if (kind == 1)
        return kindResident.y;
    if (kind == 2)
        return kindResident.z;
    return kindResident.w;
}

uint partsOf(uint kind)
{
    if (kind == 0)
        return partCount.x;
    if (kind == 1)
        return partCount.y;
    if (kind == 2)
        return partCount.z;
    return partCount.w;
}

uint partIndexCount(uint kind, uint part)
{
    uint reg = kind * 2;
    uint comp = part;
    if (part >= 4)
    {
        reg += 1;
        comp = part - 4;
    }
    uint4 packed = indexCount[reg];
    if (comp == 0)
        return packed.x;
    if (comp == 1)
        return packed.y;
    if (comp == 2)
        return packed.z;
    return packed.w;
}

float3 aabbCorner(float3 bmin, float3 bmax, int bit)
{
    float x = (bit & 1) != 0 ? bmax.x : bmin.x;
    float y = (bit & 2) != 0 ? bmax.y : bmin.y;
    float z = (bit & 4) != 0 ? bmax.z : bmin.z;
    return float3(x, y, z);
}

bool boundsVisible(uint kind, FoliageWorld rows)
{
    float3 bmin  = boundsMin[kind].xyz;
    float3 bmax  = boundsMax[kind].xyz;
    float4x4 world = FoliageWorldMatrix(rows);
    float3 p0    = mul(float4(bmin, 1.0), world).xyz;
    float3 mn    = p0;
    float3 mx    = p0;
    [unroll]
    for (int corner = 1; corner < 8; ++corner)
    {
        float3 p = mul(float4(aabbCorner(bmin, bmax, corner), 1.0), world).xyz;
        mn = min(mn, p);
        mx = max(mx, p);
    }

    // w is the world-space wind margin. The bend is in metres, after instance scale.
    float sway = max(boundsMax[kind].w, 0.0);
    mn -= sway;
    mx += sway;

    if (useSphere != 0)
    {
        float3 center  = sphere.xyz;
        float3 closest = clamp(center, mn, mx);
        float3 delta   = closest - center;
        return dot(delta, delta) <= sphere.w * sphere.w;
    }

    [unroll]
    for (int plane = 0; plane < 6; ++plane)
    {
        float3 n  = planes[plane].xyz;
        float3 pv = float3(n.x >= 0.0 ? mx.x : mn.x, n.y >= 0.0 ? mx.y : mn.y, n.z >= 0.0 ? mx.z : mn.z);
        if (dot(n, pv) + planes[plane].w < 0.0)
            return false;
    }
    return true;
}

bool visibleInstance(uint id, out uint kind, out uint variant)
{
    uint packed = gKinds[id];
    kind = packed & 0xFFu;
    if (kind >= kKindCount)
        return false;
    variant = (packed >> 8u) & 0xFFu;
    if (variant >= kVariants)
        variant = 0u;
    // Shadow views still draw trees, flowers, and rocks. Grass tufts are not casters.
    if (shadowView != 0 && kind == kGrassKind)
        return false;
    if (boundsMin[kind].w == 0.0)
        return false;
    if (!boundsVisible(kind, gWorlds[id]))
        return false;
    return true;
}

[numthreads(64, 1, 1)]
void CSCompact(uint3 id : SV_DispatchThreadID)
{
    // tail is the last constant. Reading it keeps this cbuffer at 432 bytes so it matches CSArgs.
    uint keep = tail.x | tail.y | tail.z | tail.w | instanceCount | viewIndex;
    if (keep == 0xFFFFFFFFu)
        return;
    if (id.x >= instanceCount)
        return;

    uint kind;
    uint variant;
    if (!visibleInstance(id.x, kind, variant))
        return;
    uint slot;
    InterlockedAdd(gCounters[kind * kVariants + variant], 1, slot);
}

[numthreads(4, 1, 1)]
void CSPrefix(uint3 id : SV_DispatchThreadID)
{
    // Live read of every constant so FXC does not shrink this cbuffer below 432 bytes.
    uint keep = tail.x | tail.y | tail.z | tail.w | viewIndex | instanceCount | useSphere | shadowView
        | kindResident.x | partCount.x | indexCount[0].x
        | asuint(planes[0].x) | asuint(sphere.x) | asuint(boundsMin[0].w) | asuint(boundsMax[0].w);
    if (keep == 0xFFFFFFFFu || id.x >= kKindCount)
        return;
    uint sum = 0;
    [unroll]
    for (uint variant = 0; variant < kVariants; ++variant)
    {
        uint slot = id.x * kVariants + variant;
        gPrefix[slot] = sum;
        sum += gCounters[slot];
    }
}

[numthreads(64, 1, 1)]
void CSScatter(uint3 id : SV_DispatchThreadID)
{
    uint keep = tail.x | tail.y | tail.z | tail.w | instanceCount | viewIndex | useSphere | shadowView
        | kindResident.x | partCount.x | indexCount[0].x
        | asuint(planes[0].x) | asuint(sphere.x) | asuint(boundsMin[0].w) | asuint(boundsMax[0].w);
    if (keep == 0xFFFFFFFFu)
        return;
    if (id.x >= instanceCount)
        return;

    uint kind;
    uint variant;
    if (!visibleInstance(id.x, kind, variant))
        return;
    uint counter = kind * kVariants + variant;
    uint local;
    InterlockedAdd(gCursor[counter], 1, local);
    uint cap = residentOf(kind);
    if (local < cap)
    {
        uint kindBase = (viewIndex * kKindCount + kind) * kMaxInstances;
        gIndices[kindBase + gPrefix[counter] + local] = id.x;
    }
}

[numthreads(32, 1, 1)]
void CSArgs(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= kKindCount * kVariants)
        return;

    uint kind    = id.x >> 3;
    uint variant = id.x & 7u;
    uint count   = gCounters[id.x];
    uint cap     = residentOf(kind);
    if (count > cap)
        count = cap;
    if (shadowView != 0 && kind == kGrassKind)
        count = 0;
    // tail, partsOf, and partIndexCount stay live so the reflected cbuffer remains 432 bytes.
    uint legacy = partsOf(kind) | partIndexCount(kind, 0);
    uint infoBase = id.x * 9u;
    uint parts = gVariant[infoBase];
    uint keep = tail.x | tail.y | tail.z | tail.w | legacy | parts | viewIndex;
    if (keep == 0xFFFFFFFFu)
        count = 0;
    if (parts > kPartsPerKind)
        parts = kPartsPerKind;

    uint prefix = gPrefix[id.x];
    [unroll]
    for (uint part = 0; part < kPartsPerKind; ++part)
    {
        uint base = ((viewIndex * kKindCount * kVariants + kind * kVariants + variant) * kPartsPerKind + part) * 5u;
        uint indexCount = 0;
        uint instances = 0;
        if (part < parts)
        {
            indexCount = gVariant[infoBase + 1u + part];
            instances = count;
        }
        gArgs[base + 0] = indexCount;
        gArgs[base + 1] = instances;
        gArgs[base + 2] = 0;
        gArgs[base + 3] = 0;
        gArgs[base + 4] = prefix;
    }
}
