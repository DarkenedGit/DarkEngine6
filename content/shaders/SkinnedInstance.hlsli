#ifndef DE_SKINNED_INSTANCE_HLSLI
#define DE_SKINNED_INSTANCE_HLSLI

// 160 bytes, matching SkinnedInstanceGpu. Explicit rows: FXC stores
// StructuredBuffer<float4x4> as columns and ignores pack_matrix.
struct SkinnedInstanceGpu
{
    float4 world0;
    float4 world1;
    float4 world2;
    float4 world3;
    float4 prev0;
    float4 prev1;
    float4 prev2;
    float4 prev3;
    float4 color;
    uint   paletteIndex;
    uint   pad0;
    uint   pad1;
    uint   pad2;
};

// SM 5.0 structured elements cannot exceed 2048 bytes. One BonePaletteCB (8192 bytes)
// is four of these, in order: curr bones 0-31, curr 32-63, prev 0-31, prev 32-63.
struct PaletteChunk
{
    float4 rows[128];
};

StructuredBuffer<SkinnedInstanceGpu> gInstances : register(t4);
StructuredBuffer<uint>               gIndices   : register(t5);
StructuredBuffer<PaletteChunk>       gPalettes  : register(t6);

float4x4 skinnedRows(float4 r0, float4 r1, float4 r2, float4 r3)
{
    return float4x4(r0, r1, r2, r3);
}

float4x4 paletteBone(uint paletteIndex, uint bone, uint chunkBase)
{
    uint chunk = bone >> 5;
    uint local = bone & 31u;
    uint elem  = paletteIndex * 4u + chunkBase + chunk;
    uint row   = local * 4u;
    return float4x4(
        gPalettes[elem].rows[row],
        gPalettes[elem].rows[row + 1u],
        gPalettes[elem].rows[row + 2u],
        gPalettes[elem].rows[row + 3u]);
}

float4x4 boneCurr(uint paletteIndex, uint bone)
{
    return paletteBone(paletteIndex, bone, 0u);
}

float4x4 bonePrev(uint paletteIndex, uint bone)
{
    return paletteBone(paletteIndex, bone, 2u);
}

float4 skinPosCurr(uint paletteIndex, float3 p, uint4 j, float4 w)
{
    float4 hp = float4(p, 1.0f);
    return w.x * mul(hp, boneCurr(paletteIndex, j.x)) + w.y * mul(hp, boneCurr(paletteIndex, j.y))
         + w.z * mul(hp, boneCurr(paletteIndex, j.z)) + w.w * mul(hp, boneCurr(paletteIndex, j.w));
}

float4 skinPosPrev(uint paletteIndex, float3 p, uint4 j, float4 w)
{
    float4 hp = float4(p, 1.0f);
    return w.x * mul(hp, bonePrev(paletteIndex, j.x)) + w.y * mul(hp, bonePrev(paletteIndex, j.y))
         + w.z * mul(hp, bonePrev(paletteIndex, j.z)) + w.w * mul(hp, bonePrev(paletteIndex, j.w));
}

float3 skinNrm(uint paletteIndex, float3 n, uint4 j, float4 w)
{
    float3 r = w.x * mul(n, (float3x3)boneCurr(paletteIndex, j.x))
             + w.y * mul(n, (float3x3)boneCurr(paletteIndex, j.y))
             + w.z * mul(n, (float3x3)boneCurr(paletteIndex, j.z))
             + w.w * mul(n, (float3x3)boneCurr(paletteIndex, j.w));
    return r;
}

#endif
