#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;

    using Microsoft::WRL::ComPtr;

    // B8G8R8A8_UNORM typed UAV store. Optional at feature level 11_0. Software
    // copies never need it. The NV12 convert shader stores through this view.
    inline bool movieBgraTypedUavStoreSupported(ID3D12Device* device)
    {
        if (!device)
            return false;
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
        support.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))))
            return false;
        const bool typed = (support.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) != 0;
        const bool store = (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0;
        return typed && store;
    }

    // Software textures are sampled copies. Hardware conversion is the only UAV.
    inline D3D12_RESOURCE_FLAGS movieFrameResourceFlags(bool hardware)
    {
        if (!hardware)
            return D3D12_RESOURCE_FLAG_NONE;
        return D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    // Two B8G8R8A8_UNORM slots, one per swap-chain frame. Software upload writes
    // the slot Renderer::frameIndex() names and does not request a UAV. Hardware
    // convert is one dispatch on that slot's upload DIRECT list, and only when
    // movieBgraTypedUavStoreSupported is true. No Media Foundation and no Media/.
    class MovieFrameTexture
    {
    public:
        static constexpr uint32_t kSlotCount       = 2;
        static constexpr uint32_t kNv12Surfaces    = 3;
        static constexpr uint32_t kColorLimited601 = 0;
        static constexpr uint32_t kColorLimited709 = 1;
        static constexpr uint32_t kColorFull601    = 2;
        static constexpr uint32_t kColorFull709    = 3;

        MovieFrameTexture() = default;
        ~MovieFrameTexture();

        MovieFrameTexture(const MovieFrameTexture&)            = delete;
        MovieFrameTexture& operator=(const MovieFrameTexture&) = delete;
        MovieFrameTexture(MovieFrameTexture&&)                 = delete;
        MovieFrameTexture& operator=(MovieFrameTexture&&)      = delete;

        bool create(Renderer& renderer, uint32_t width, uint32_t height, bool hardware);

        bool valid() const
        {
            return m_frame[0] != nullptr;
        }
        bool isHardware() const
        {
            return m_hardware;
        }
        uint32_t width() const
        {
            return m_width;
        }
        uint32_t height() const
        {
            return m_height;
        }

        // Tight top-down BGRA, byteCount = width * height * 4. Software only.
        bool upload(const void* bgra, uint32_t byteCount);

        // R8_UNORM plane 0 and R8G8_UNORM plane 1. Stored in a non-shader-visible heap
        // so the CPU handles are legal CopyDescriptors sources.
        bool placeNv12(uint32_t surface, ID3D12Resource* nv12);
        D3D12_CPU_DESCRIPTOR_HANDLE nv12PlaneCpu(uint32_t surface, uint32_t plane) const;

        // ySrv and uvSrv must not be shader-visible; nv12PlaneCpu() is a legal source.
        // Copied into this slot's shader-visible table. Incomplete decode records nothing.
        bool convertNv12(D3D12_CPU_DESCRIPTOR_HANDLE ySrv, D3D12_CPU_DESCRIPTOR_HANDLE uvSrv, uint32_t colorMode, uint32_t flip180, bool decodeFenceComplete);

        ID3D12Resource*             resource(uint32_t slot) const;
        ID3D12DescriptorHeap*       shaderHeap() const;
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpu(uint32_t slot) const;
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu(uint32_t slot) const;
        D3D12_RESOURCE_STATES       state(uint32_t slot) const;

    private:
        static constexpr uint32_t kPerSlotViews  = 2;
        static constexpr uint32_t kDispatchViews = 3;

        D3D12_CPU_DESCRIPTOR_HANDLE cpuAt(uint32_t index) const;
        D3D12_GPU_DESCRIPTOR_HANDLE gpuAt(uint32_t index) const;
        D3D12_CPU_DESCRIPTOR_HANDLE nv12Cpu(uint32_t surface, uint32_t plane) const;
        uint32_t slotSrv(uint32_t slot) const;
        uint32_t dispatchIndex(uint32_t slot) const;
        void     clear();

        Renderer*                          m_renderer = nullptr;
        ComPtr<ID3D12Resource>             m_frame[kSlotCount];
        ComPtr<ID3D12Resource>             m_upload[kSlotCount];
        uint8_t*                           m_mapped[kSlotCount]{};
        D3D12_RESOURCE_STATES              m_state[kSlotCount]{};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_footprint{};
        ComPtr<ID3D12DescriptorHeap>       m_heap;
        ComPtr<ID3D12DescriptorHeap>       m_nv12Heap;
        ComPtr<ID3D12RootSignature>        m_root;
        ComPtr<ID3D12PipelineState>        m_pso;
        D3D12_CPU_DESCRIPTOR_HANDLE        m_cpuStart{};
        D3D12_GPU_DESCRIPTOR_HANDLE        m_gpuStart{};
        D3D12_CPU_DESCRIPTOR_HANDLE        m_nv12CpuStart{};
        UINT                               m_incr     = 0;
        uint32_t                           m_width    = 0;
        uint32_t                           m_height   = 0;
        bool                               m_hardware = false;
    };

} // namespace Dark
