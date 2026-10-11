// Foliage visibility. CSCompact is one thread per gathered instance. CSArgs writes one
// draw argument per opaque part. Root UAVs are structured. Kinds keep separate spans.
// gIndices[SV_InstanceID] still starts at zero for that kind. StartInstanceLocation stays 0.
#pragma pack_matrix(row_major)

#include "FoliageWorld.hlsli"

// Match FoliagePipeline::kCullKindCount, kCullPartsPerKind, kGrassKindIndex, and kMaxFoliageDraw.
static const uint kKindCount    = 4;
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
// Structured, not RWBuffer. A root UAV cannot bind a typed buffer. Stride is still 4.
RWStructuredBuffer<uint>       gIndices  : register(u0);
RWStructuredBuffer<uint>       gCounters : register(u1);
RWStructuredBuffer<uint>       gArgs     : register(u2);

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

[numthreads(64, 1, 1)]
void CSCompact(uint3 id : SV_DispatchThreadID)
{
    // tail is the last constant. Reading it keeps this cbuffer at 432 bytes so it matches CSArgs.
    uint keep = tail.x | tail.y | tail.z | tail.w | instanceCount | viewIndex;
    if (keep == 0xFFFFFFFFu)
        return;
    if (id.x >= instanceCount)
        return;

    uint kind = gKinds[id.x];
    if (kind >= kKindCount)
        return;
    // Shadow views still draw trees, flowers, and rocks. Grass tufts are not casters.
    if (shadowView != 0 && kind == kGrassKind)
        return;
    if (boundsMin[kind].w == 0.0)
        return;
    if (!boundsVisible(kind, gWorlds[id.x]))
        return;

    uint slot;
    InterlockedAdd(gCounters[kind], 1, slot);
    uint cap = residentOf(kind);
    if (slot < cap)
        gIndices[(viewIndex * kKindCount + kind) * kMaxInstances + slot] = id.x;
}

[numthreads(64, 1, 1)]
void CSArgs(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= kKindCount)
        return;

    uint count = gCounters[id.x];
    uint cap   = residentOf(id.x);
    if (count > cap)
        count = cap;
    if (shadowView != 0 && id.x == kGrassKind)
        count = 0;
    // tail is the last constant. Reading it keeps this cbuffer at 432 bytes so it matches CSCompact.
    uint keep = tail.x | tail.y | tail.z | tail.w | partsOf(id.x) | viewIndex;
    if (keep == 0xFFFFFFFFu)
        count = 0;

    uint parts = partsOf(id.x);
    if (parts > kPartsPerKind)
        parts = kPartsPerKind;
    for (uint part = 0; part < parts; ++part)
    {
        uint base = (viewIndex * kKindCount * kPartsPerKind + id.x * kPartsPerKind + part) * 5;
        gArgs[base + 0] = partIndexCount(id.x, part);
        gArgs[base + 1] = count;
        gArgs[base + 2] = 0;
        gArgs[base + 3] = 0;
        gArgs[base + 4] = 0;
    }
}
