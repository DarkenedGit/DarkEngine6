#pragma once

namespace Dark
{

    // Root-constant layouts for BasicMesh / SkinnedMesh. Float arrays only — no D3D12 types.
    // Must match content/shaders/BasicMesh.hlsl and BasicMeshGBuffer.hlsl cbuffers.

    struct MeshGBufferConstants
    {
        float worldViewProj[16];
        float world[16];
        float color[4]; // rgb linear tint; a = emissiveScalar * Rec.709 luma(emissiveColor)
        float prevWorldViewProj[16];
        float roughness;
        float metallic;
        float ao;
        float normalScale;
        float alphaCutoff;
        float alphaModeMask;
    };

    static_assert(sizeof(MeshGBufferConstants) == 58 * sizeof(float), "gbuffer mesh CB");
    // Skinned G-buffer RS: 58 constants + 1 table + 2 shadow CBV + 2 bone CBV = 63 (<= 64).
    static_assert(58 + 1 + 2 + 2 <= 64, "skinned G-buffer root signature DWORD budget");

    struct MeshFrameConstants
    {
        float worldViewProj[16];
        float world[16];
        float color[4];
        float lightDirWS[3];
        float ambientScale;
        float lightColor[3];
        float pad1;
        float cameraPos[3];
        float lighting; // 1 = Lambert+shadow, 0 = albedo only
        // Append-only after the frozen 48 floats (must match BasicMesh.hlsl bit-for-bit).
        float normalScale;
        float ao;
        float alphaCutoff;
        float alphaModeMask;
        float emissive; // same premultiplied scalar as G-buffer color.a
    };

    static_assert(sizeof(MeshFrameConstants) == 53 * sizeof(float), "forward mesh CB");

} // namespace Dark
