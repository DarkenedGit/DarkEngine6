// Grass visibility. CSCompact is one thread per resident blade. CSArgs writes four draw args.
// Blade and wind structs match Terrain::GrassBlade and GrassTileWind. Root UAVs are structured.
#pragma pack_matrix(row_major)

struct GrassBladeGpu
{
    float x;
    float y;
    float z;
    float yaw;
    float heightMul;
    float flexMul;
    float phase;
    uint tileSlot;
};

struct GrassTileWindGpu
{
    float windX;
    float windZ;
    float fade;
    float pad;
};

cbuffer GrassCullConstants : register(b0)
{
    float4 planes[6];
    uint4 resident;
    uint4 indexCount;
    uint4 indexBase;
    uint lodIndex;
    uint windCount;
    uint pad1;
    uint pad2;
};

StructuredBuffer<GrassBladeGpu>     gBlades   : register(t0);
StructuredBuffer<GrassTileWindGpu> gTiles    : register(t1);
// Structured, not RWBuffer. A root UAV cannot bind a typed buffer. Stride is still 4.
RWStructuredBuffer<uint>            gIndices  : register(u0);
RWStructuredBuffer<uint>            gCounters : register(u1);
RWStructuredBuffer<uint>            gArgs     : register(u2);

bool outsideRoot(float x, float y, float z)
{
    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        if (dot(planes[i].xyz, float3(x, y, z)) + planes[i].w < 0.0)
            return true;
    }
    return false;
}

[numthreads(64, 1, 1)]
void CSCompact(uint3 id : SV_DispatchThreadID)
{
    uint count = resident[lodIndex];
    if (id.x >= count)
        return;

    GrassBladeGpu blade = gBlades[id.x];
    if (blade.tileSlot >= windCount)
        return;
    // fade <= 0 is past 128 m. The 112-128 m band stays and the vertex shader still fades it.
    if (gTiles[blade.tileSlot].fade <= 0.0)
        return;
    if (outsideRoot(blade.x, blade.y, blade.z))
        return;

    uint slot;
    InterlockedAdd(gCounters[lodIndex], 1, slot);
    if (slot < count)
        gIndices[indexBase[lodIndex] + slot] = id.x;
}

[numthreads(64, 1, 1)]
void CSArgs(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 4)
        return;

    uint count = gCounters[id.x];
    uint cap   = resident[id.x];
    if (count > cap)
        count = cap;
    // pad2 is the last constant. Reading it keeps this cbuffer at 160 bytes so it matches CSCompact.
    uint tail = pad2 | lodIndex | windCount | indexBase[id.x];
    if (tail == 0xFFFFFFFFu)
        count = 0;

    // Five uints, one D3D12_DRAW_INDEXED_ARGUMENTS. StartInstanceLocation stays 0.
    uint base = id.x * 5;
    gArgs[base + 0] = indexCount[id.x];
    gArgs[base + 1] = count;
    gArgs[base + 2] = 0;
    gArgs[base + 3] = 0;
    gArgs[base + 4] = 0;
}
