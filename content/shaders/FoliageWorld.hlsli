#ifndef FOLIAGE_WORLD_HLSLI
#define FOLIAGE_WORLD_HLSLI

#pragma pack_matrix(row_major)

// Matrix4f is row-major, translation in the last row. FXC stores
// StructuredBuffer<float4x4> as columns and ignores pack_matrix, so that
// translation becomes homogeneous w and one tree face fills the screen.
struct FoliageWorld
{
    float4 row0;
    float4 row1;
    float4 row2;
    float4 row3;
};

float4x4 FoliageWorldMatrix(FoliageWorld rows)
{
    return float4x4(rows.row0, rows.row1, rows.row2, rows.row3);
}

#endif
