#ifndef DE_MESH_INSTANCE_HLSLI
#define DE_MESH_INSTANCE_HLSLI

// 144 bytes, matching MeshInstanceGpu. Explicit rows: FXC stores
// StructuredBuffer<float4x4> as columns and ignores pack_matrix.
struct MeshInstanceGpu
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
};

float4x4 meshInstanceRows(float4 r0, float4 r1, float4 r2, float4 r3)
{
    return float4x4(r0, r1, r2, r3);
}

#endif
