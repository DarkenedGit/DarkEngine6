#pragma once

#include "Render/RenderItem.h"

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
        ComPtr<ID3D12DescriptorHeap> m_cpuHeap;
        ComPtr<ID3D12Resource>       m_indices[kFrameSlots];
        ComPtr<ID3D12Resource>       m_counters[kFrameSlots];
        // m_cpuStart is non-shader-visible. ClearUnorderedAccessViewUint reads that descriptor.
        // m_gpuCpuStart writes the same view into the shader-visible heap bound on the list.
        D3D12_CPU_DESCRIPTOR_HANDLE  m_cpuStart{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_gpuCpuStart{};
        D3D12_GPU_DESCRIPTOR_HANDLE  m_gpuStart{};
        UINT                         m_increment = 0;
        D3D12_RESOURCE_STATES        m_indexState[kFrameSlots] = {
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        };
    };

    // Skinned cull. Own arguments, indices, and counters. Not the static 4096-wide lists.
    // kStride is the part cap. One view owns that many indices and that many batch slots.
    class SkinnedInstanceCull
    {
    public:
        static constexpr uint32_t kFrameSlots = 2;
        static constexpr uint32_t kStride     = kMaxSkinnedParts;

        SkinnedInstanceCull();
        ~SkinnedInstanceCull();
        SkinnedInstanceCull(const SkinnedInstanceCull&)            = delete;
        SkinnedInstanceCull& operator=(const SkinnedInstanceCull&) = delete;

        bool create(ID3D12Device* device);
        bool isReady() const;

        bool upload(uint32_t frameSlot, const MeshCullAabb* aabbs, uint32_t aabbCount, const GpuBatchRecord* gbuffer, uint32_t gbufferCount, const GpuBatchRecord* depth, uint32_t depthCount);
        bool dispatch(ID3D12GraphicsCommandList* cmd, uint32_t frameSlot, const CullViewDesc* views, uint32_t viewCount);

        D3D12_GPU_VIRTUAL_ADDRESS indexAddress(uint32_t frameSlot, uint32_t viewIndex) const;
        ID3D12Resource*           indexResource(uint32_t frameSlot) const;
        ID3D12Resource*           argumentResource(uint32_t frameSlot) const;
        ID3D12CommandSignature*   signature() const;
        static UINT64             argumentOffset(uint32_t viewIndex, uint32_t batchIndex);

    private:
        bool createShaderVisibleHeap(ID3D12Device* device);
        bool createBuffers(ID3D12Device* device);
        D3D12_GPU_VIRTUAL_ADDRESS slotBase(uint32_t frameSlot) const;

        ComPtr<ID3D12Device>              m_device;
        ComPtr<ID3D12RootSignature>       m_root;
        ComPtr<ID3D12PipelineState>       m_compact;
        ComPtr<ID3D12PipelineState>       m_argsPso;
        ComPtr<ID3D12DescriptorHeap>      m_heap;
        ComPtr<ID3D12DescriptorHeap>      m_cpuHeap;
        ComPtr<ID3D12CommandSignature>    m_signature;
        ComPtr<ID3D12Resource>            m_indices[kFrameSlots];
        ComPtr<ID3D12Resource>            m_counters[kFrameSlots];
        ComPtr<ID3D12Resource>            m_args[kFrameSlots];
        ComPtr<ID3D12Resource>            m_upload;
        uint8_t*                          m_uploadBytes = nullptr;
        // m_cpuStart is non-shader-visible. ClearUnorderedAccessViewUint reads that descriptor.
        // m_gpuCpuStart writes the same view into the shader-visible heap bound on the list.
        D3D12_CPU_DESCRIPTOR_HANDLE       m_cpuStart{};
        D3D12_CPU_DESCRIPTOR_HANDLE       m_gpuCpuStart{};
        D3D12_GPU_DESCRIPTOR_HANDLE       m_gpuStart{};
        UINT                              m_increment = 0;
        D3D12_RESOURCE_STATES             m_argState[kFrameSlots] = {
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        };
        D3D12_RESOURCE_STATES             m_indexState[kFrameSlots] = {
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        };
    };

} // namespace Dark
