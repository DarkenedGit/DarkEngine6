#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // One built static view. planes[] are the normalized inward frustum.
    struct CullViewDesc
    {
        uint32_t viewIndex     = 0;
        uint32_t batchCount    = 0;
        uint32_t itemCount     = 0;
        uint32_t useDepthBatch = 0;
        float    planes[6][4]{};
    };

    // Compute cull of the static argument buffer on the caller's DIRECT list.
    class InstanceCull
    {
    public:
        bool create(ID3D12Device* device);
        bool isReady() const;

        // Culls every view while the argument buffer, counters, and indices stay unordered,
        // then transitions arguments to INDIRECT_ARGUMENT and indices to NON_PIXEL_SHADER_RESOURCE.
        bool dispatch(ID3D12GraphicsCommandList* cmd, uint32_t frameSlot, D3D12_GPU_VIRTUAL_ADDRESS aabbVA, D3D12_GPU_VIRTUAL_ADDRESS gbufferRecordsVA, D3D12_GPU_VIRTUAL_ADDRESS depthRecordsVA, ID3D12Resource* args, D3D12_RESOURCE_STATES& argState, const CullViewDesc* views, uint32_t viewCount);

        D3D12_GPU_VIRTUAL_ADDRESS indexAddress(uint32_t frameSlot, uint32_t viewIndex) const;
        ID3D12Resource*           indexResource(uint32_t frameSlot) const;

    private:
        static constexpr uint32_t kFrameSlots = 2;

        bool createShaderVisibleHeap(ID3D12Device* device);
        bool createBuffers(ID3D12Device* device);

        ComPtr<ID3D12Device>         m_device;
        ComPtr<ID3D12RootSignature>  m_root;
        ComPtr<ID3D12PipelineState>  m_compact;
        ComPtr<ID3D12PipelineState>  m_argsPso;
        ComPtr<ID3D12DescriptorHeap> m_heap;
        ComPtr<ID3D12Resource>       m_indices[kFrameSlots];
        ComPtr<ID3D12Resource>       m_counters[kFrameSlots];
        D3D12_CPU_DESCRIPTOR_HANDLE  m_cpuStart{};
        D3D12_GPU_DESCRIPTOR_HANDLE  m_gpuStart{};
        UINT                         m_increment = 0;
        D3D12_RESOURCE_STATES        m_indexState[kFrameSlots] = {
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        };
    };

} // namespace Dark
