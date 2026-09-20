#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // Bake-only compute PSO (first cs_5_0). Private DIRECT allocator/list — never Renderer::commandList().
    // UAV targets come from Texture2D::createUavR32Float, not general albedo/G-buffer/HDR.
    class TerrainErosionPipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrv       = 1;
        static constexpr UINT kRootUav       = 2;
        static constexpr UINT kHeapCount     = 2; // slot 0 SRV, slot 1 UAV

        TerrainErosionPipeline() = default;

        bool create(ID3D12Device* device);
        bool isValid() const
        {
            return m_psoThermal != nullptr && m_psoPipe != nullptr;
        }

        bool resetCommands(ID3D12Device* device);

        ID3D12RootSignature* rootSignature() const
        {
            return m_rootSignature.Get();
        }
        ID3D12PipelineState* psoThermal() const
        {
            return m_psoThermal.Get();
        }
        ID3D12PipelineState* psoPipe() const
        {
            return m_psoPipe.Get();
        }
        ID3D12DescriptorHeap* heap() const
        {
            return m_heap.Get();
        }
        ID3D12CommandAllocator* allocator() const
        {
            return m_allocator.Get();
        }
        ID3D12GraphicsCommandList* commandList() const
        {
            return m_list.Get();
        }

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle(uint32_t slot) const;
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle(uint32_t slot) const;

    private:
        bool createPsos(ID3D12Device* device);

        ComPtr<ID3D12RootSignature>       m_rootSignature;
        ComPtr<ID3D12PipelineState>       m_psoThermal;
        ComPtr<ID3D12PipelineState>       m_psoPipe;
        ComPtr<ID3D12DescriptorHeap>      m_heap;
        ComPtr<ID3D12CommandAllocator>    m_allocator;
        ComPtr<ID3D12GraphicsCommandList> m_list;
        D3D12_CPU_DESCRIPTOR_HANDLE       m_cpuStart{};
        D3D12_GPU_DESCRIPTOR_HANDLE       m_gpuStart{};
        UINT                              m_incr = 0;
    };

} // namespace Dark
