#pragma once

#include "Render/RenderItem.h"

#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // G-buffer and depth PSOs for static DrawIndexedInstanced batches.
    // Solid fill only. Wire and point stay on the immediate mesh PSOs.
    class InstancedMeshPipeline
    {
    public:
        static constexpr UINT kGbConstants = 0;
        static constexpr UINT kGbMaterial  = 1;
        static constexpr UINT kGbInstances = 2;
        static constexpr UINT kGbIndices   = 3;

        static constexpr UINT kDepthConstants = 0;
        static constexpr UINT kDepthInstances = 1;
        static constexpr UINT kDepthIndices   = 2;

        InstancedMeshPipeline() = default;

        bool create(ID3D12Device* device);
        bool isValid() const { return m_gbPso != nullptr && m_depthPso != nullptr; }

        void bindGBuffer(ID3D12GraphicsCommandList* cmd) const;
        void setGBufferConstants(ID3D12GraphicsCommandList* cmd, const MeshBatchConstants& constants) const;
        void setGBufferBuffers(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS instances, D3D12_GPU_VIRTUAL_ADDRESS indices) const;

        void bindDepth(ID3D12GraphicsCommandList* cmd) const;
        void setDepthConstants(ID3D12GraphicsCommandList* cmd, const MeshDepthBatchConstants& constants) const;
        void setDepthBuffers(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS instances, D3D12_GPU_VIRTUAL_ADDRESS indices) const;

    private:
        void reset();

        ComPtr<ID3D12RootSignature> m_gbRoot;
        ComPtr<ID3D12PipelineState> m_gbPso;
        ComPtr<ID3D12RootSignature> m_depthRoot;
        ComPtr<ID3D12PipelineState> m_depthPso;
    };

} // namespace Dark
