#pragma once

#include "Render/RenderItem.h"

#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // G-buffer, depth, and forward PSOs for static batches.
    // Solid fill only. Wire and point stay on the immediate mesh PSOs.
    // A failed forward PSO leaves g-buffer and depth instancing up.
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

        static constexpr UINT kForwardConstants = 0;
        static constexpr UINT kForwardMaterial  = 1;
        static constexpr UINT kForwardShadow    = 2;
        static constexpr UINT kForwardInstances = 3;
        static constexpr UINT kForwardIndices   = 4;

        InstancedMeshPipeline() = default;

        bool create(ID3D12Device* device);
        bool isValid() const { return m_gbPso != nullptr && m_depthPso != nullptr; }
        bool forwardReady() const { return m_forwardPso != nullptr; }

        void bindGBuffer(ID3D12GraphicsCommandList* cmd) const;
        void setThermal(bool on) { m_thermal = on; }
        void setGBufferConstants(ID3D12GraphicsCommandList* cmd, const MeshBatchConstants& constants) const;
        void setGBufferBuffers(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS instances, D3D12_GPU_VIRTUAL_ADDRESS indices) const;

        void bindDepth(ID3D12GraphicsCommandList* cmd) const;
        void setDepthConstants(ID3D12GraphicsCommandList* cmd, const MeshDepthBatchConstants& constants) const;
        void setDepthBuffers(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS instances, D3D12_GPU_VIRTUAL_ADDRESS indices) const;

        void bindForward(ID3D12GraphicsCommandList* cmd) const;
        void setForwardConstants(ID3D12GraphicsCommandList* cmd, const MeshForwardBatchConstants& constants) const;
        void setForwardBuffers(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS instances, D3D12_GPU_VIRTUAL_ADDRESS indices) const;
        void setForwardShadow(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS shadowCbv) const;

    private:
        void reset();
        bool createForward(ID3D12Device* device);

        ComPtr<ID3D12RootSignature> m_gbRoot;
        ComPtr<ID3D12PipelineState> m_gbPso;
        ComPtr<ID3D12PipelineState> m_gbThermal;
        bool                        m_thermal = false;
        ComPtr<ID3D12RootSignature> m_depthRoot;
        ComPtr<ID3D12PipelineState> m_depthPso;
        ComPtr<ID3D12RootSignature> m_forwardRoot;
        ComPtr<ID3D12PipelineState> m_forwardPso;
    };

} // namespace Dark
