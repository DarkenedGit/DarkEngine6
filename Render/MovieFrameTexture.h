#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;

    using Microsoft::WRL::ComPtr;

    // Two B8G8R8A8_UNORM slots, one per swap-chain frame. Software upload writes
    // the slot Renderer::frameIndex() names. Hardware convert is one dispatch on
    // that slot's upload DIRECT list. No Media Foundation and no Media/.
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

        // R8_UNORM plane 0 and R8G8_UNORM plane 1 into the reserved NV12 slots.
        bool placeNv12(uint32_t surface, ID3D12Resource* nv12);
        D3D12_CPU_DESCRIPTOR_HANDLE nv12PlaneCpu(uint32_t surface, uint32_t plane) const;

        // ySrv and uvSrv are plane SRVs the caller created. Copied into this slot's
        // table. decodeFenceComplete == false records nothing and changes no slot.
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
        uint32_t slotSrv(uint32_t slot) const;
        uint32_t nv12Index(uint32_t surface, uint32_t plane) const;
        uint32_t dispatchIndex(uint32_t slot) const;
        void     clear();

        Renderer*                          m_renderer = nullptr;
        ComPtr<ID3D12Resource>             m_frame[kSlotCount];
        ComPtr<ID3D12Resource>             m_upload[kSlotCount];
        uint8_t*                           m_mapped[kSlotCount]{};
        D3D12_RESOURCE_STATES              m_state[kSlotCount]{};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_footprint{};
        ComPtr<ID3D12DescriptorHeap>       m_heap;
        ComPtr<ID3D12RootSignature>        m_root;
        ComPtr<ID3D12PipelineState>        m_pso;
        D3D12_CPU_DESCRIPTOR_HANDLE        m_cpuStart{};
        D3D12_GPU_DESCRIPTOR_HANDLE        m_gpuStart{};
        UINT                               m_incr     = 0;
        uint32_t                           m_width    = 0;
        uint32_t                           m_height   = 0;
        bool                               m_hardware = false;
    };

} // namespace Dark
