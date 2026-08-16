#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // Root constants layout (all 32-bit slots), matches BasicMesh.hlsl cbuffer.
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
        float pad2;
    };

    static_assert(sizeof(MeshFrameConstants) == 48 * sizeof(float), "root constant size");

    // PSO + root signature for MeshGen meshes (pos/normal/uv) with one albedo texture.
    class MeshPipeline
    {
    public:
        // Root parameter indices (must match create()).
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootAlbedoSrv = 1;
        static constexpr UINT kRootShadowCbv = 2;
        static constexpr UINT kSrvCount      = 2; // albedo + shadow

        MeshPipeline() = default;

        // Build root signature + PSO. Returns false on failure (no exceptions).
        bool create(ID3D12Device* device);

        void bind(ID3D12GraphicsCommandList* cmd) const;
        void setConstants(ID3D12GraphicsCommandList* cmd, const MeshFrameConstants& constants) const;

        bool isValid() const
        {
            return m_pso != nullptr;
        }

    private:
        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12PipelineState> m_pso;
    };

} // namespace Dark
