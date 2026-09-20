// Bake-only terrain erosion CS (first cs_5_0). Identity stub — Mei/thermal math is PR3.
// Dispatch on a private DIRECT list, never the frame command list. UAV only on bake R32F.
#pragma pack_matrix(row_major)

cbuffer ErosionConstants : register(b0)
{
    uint width;
    uint height;
    uint _pad0;
    uint _pad1;
};

Texture2D<float>   gSrcHeight : register(t0);
RWTexture2D<float> gDstHeight : register(u0);

[numthreads(8, 8, 1)]
void CSThermal(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height)
        return;
    gDstHeight[id.xy] = gSrcHeight[id.xy];
}

[numthreads(8, 8, 1)]
void CSPipe(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height)
        return;
    gDstHeight[id.xy] = gSrcHeight[id.xy];
}
