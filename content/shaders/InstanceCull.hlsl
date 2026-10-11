// Static DrawList cull. CSCompact is one thread per sorted item. CSArgs is one thread per batch.
// MeshCullAabb is scalar fields so the 48-byte layout matches the C++ struct.
#pragma pack_matrix(row_major)

struct MeshCullAabb
{
    float centerX;
    float centerY;
    float centerZ;
    uint  flags;
    float extentX;
    float extentY;
    float extentZ;
    uint  gbufferBatch;
    uint  depthBatch;
    uint  pad0;
    uint  pad1;
    uint  pad2;
};

struct GpuBatchRecord
{
    uint indexCount;
    uint itemBegin;
    uint itemCount;
    uint pad;
};

cbuffer CullConstants : register(b0)
{
    float4 planes[6];
    uint   itemCount;
    uint   batchCount;
    uint   indexBase;
    uint   argBase;
    uint   counterBase;
    uint   useDepthBatch;
    uint   pad0;
    uint   pad1;
};

StructuredBuffer<MeshCullAabb>   gAabbs    : register(t0);
StructuredBuffer<GpuBatchRecord> gBatches  : register(t1);
// Structured, not RWBuffer. A root UAV cannot bind a typed buffer. Stride is still 4.
RWStructuredBuffer<uint>         gIndices  : register(u0);
RWStructuredBuffer<uint>         gCounters : register(u1);
RWStructuredBuffer<uint>         gArgs     : register(u2);

static const uint kAlwaysVisible = 1;
static const uint kCastShadow    = 2;

bool outsideFrustum(float cx, float cy, float cz, float ex, float ey, float ez)
{
    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        float px = planes[i].x >= 0.0 ? cx + ex : cx - ex;
        float py = planes[i].y >= 0.0 ? cy + ey : cy - ey;
        float pz = planes[i].z >= 0.0 ? cz + ez : cz - ez;
        if (dot(planes[i].xyz, float3(px, py, pz)) + planes[i].w < 0.0)
            return true;
    }
    return false;
}

[numthreads(64, 1, 1)]
void CSCompact(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= itemCount)
        return;

    MeshCullAabb box = gAabbs[id.x];
    if (useDepthBatch != 0 && (box.flags & kCastShadow) == 0)
        return;

    if ((box.flags & kAlwaysVisible) == 0)
    {
        if (outsideFrustum(box.centerX, box.centerY, box.centerZ, box.extentX, box.extentY, box.extentZ))
            return;
    }

    uint batch = useDepthBatch != 0 ? box.depthBatch : box.gbufferBatch;
    if (batch >= batchCount)
        return;

    uint slot;
    InterlockedAdd(gCounters[counterBase + batch], 1, slot);
    GpuBatchRecord rec = gBatches[batch];
    if (slot < rec.itemCount)
        gIndices[indexBase + rec.itemBegin + slot] = id.x;
}

[numthreads(64, 1, 1)]
void CSArgs(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= batchCount)
        return;

    GpuBatchRecord rec   = gBatches[id.x];
    uint           count = gCounters[counterBase + id.x];
    if (count > rec.itemCount)
        count = rec.itemCount;

    // Five uints, one D3D12_DRAW_INDEXED_ARGUMENTS. StartInstanceLocation stays 0.
    uint base = (argBase + id.x) * 5;
    gArgs[base + 0] = rec.indexCount;
    gArgs[base + 1] = count;
    gArgs[base + 2] = 0;
    gArgs[base + 3] = 0;
    gArgs[base + 4] = 0;
}
