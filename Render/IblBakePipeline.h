#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    class IblBakePipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrv       = 1;
        static constexpr UINT kRtvCount      = 30; // 6 faces × 5 mips
        static constexpr UINT kSrvCount      = 2;

        IblBakePipeline() = default;

        bool create(ID3D12Device* device);
        bool valid() const { return m_psoEquirect != nullptr && m_psoIrradiance != nullptr && m_psoPrefilter != nullptr; }

        bool resetCommands(ID3D12Device* device);

        ID3D12RootSignature*        rootSignature() const { return m_rootSignature.Get(); }
        ID3D12PipelineState*        psoEquirect() const { return m_psoEquirect.Get(); }
        ID3D12PipelineState*        psoIrradiance() const { return m_psoIrradiance.Get(); }
        ID3D12PipelineState*        psoPrefilter() const { return m_psoPrefilter.Get(); }
        ID3D12DescriptorHeap*       rtvHeap() const { return m_rtvHeap.Get(); }
        ID3D12DescriptorHeap*       srvHeap() const { return m_srvHeap.Get(); }
        ID3D12CommandAllocator*     allocator() const { return m_allocator.Get(); }
        ID3D12GraphicsCommandList*  commandList() const { return m_list.Get(); }

        D3D12_CPU_DESCRIPTOR_HANDLE rtvCpu(uint32_t index) const;
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpu(uint32_t slot) const;
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu(uint32_t slot) const;

    private:
        bool createPsos(ID3D12Device* device);

        ComPtr<ID3D12RootSignature>       m_rootSignature;
        ComPtr<ID3D12PipelineState>       m_psoEquirect;
        ComPtr<ID3D12PipelineState>       m_psoIrradiance;
        ComPtr<ID3D12PipelineState>       m_psoPrefilter;
        ComPtr<ID3D12DescriptorHeap>      m_rtvHeap;
        ComPtr<ID3D12DescriptorHeap>      m_srvHeap;
        ComPtr<ID3D12CommandAllocator>    m_allocator;
        ComPtr<ID3D12GraphicsCommandList> m_list;
        D3D12_CPU_DESCRIPTOR_HANDLE       m_rtvStart{};
        D3D12_CPU_DESCRIPTOR_HANDLE       m_srvCpuStart{};
        D3D12_GPU_DESCRIPTOR_HANDLE       m_srvGpuStart{};
        UINT                              m_rtvIncr = 0;
        UINT                              m_srvIncr = 0;
    };

} // namespace Dark
