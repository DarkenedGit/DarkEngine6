#pragma once

namespace Dark
{

    // Root-constant layouts for BasicMesh / SkinnedMesh. Float arrays only — no D3D12 types.
    // Must match content/shaders/BasicMesh.hlsl and BasicMeshGBuffer.hlsl cbuffers.

    struct MeshGBufferConstants
    {
        float worldViewProj[16];
        float world[16];
        float color[4];
        float prevWorldViewProj[16];
        float roughness;
        float metallic;
    };

    static_assert(sizeof(MeshGBufferConstants) == 54 * sizeof(float), "gbuffer mesh CB");

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
    };

    static_assert(sizeof(MeshFrameConstants) == 48 * sizeof(float), "root constant size");

} // namespace Dark
