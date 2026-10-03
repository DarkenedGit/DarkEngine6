#pragma once

#include "Render/DecalTypes.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // 2-frame UPLOAD ring of DecalGpuInstance. Root SRV virtual addresses, no descriptor.
    class DecalGpuList
    {
    public:
        static constexpr uint32_t kFrameCount = 2;
        static constexpr uint32_t kCapacity   = 256;

        DecalGpuList() = default;

        bool create(ID3D12Device* device);
        void upload(uint32_t frameIndex, const DecalGpuInstance* outside, uint32_t outsideCount, const DecalGpuInstance* inside, uint32_t insideCount);

        D3D12_GPU_VIRTUAL_ADDRESS outsideGpuVa() const;
        D3D12_GPU_VIRTUAL_ADDRESS insideGpuVa() const;

        bool isValid() const { return m_buffer != nullptr && m_mapped != nullptr; }

    private:
        ComPtr<ID3D12Resource>    m_buffer;
        uint8_t*                  m_mapped       = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS m_gpu          = 0;
        uint32_t                  m_slot         = 0;
        uint32_t                  m_outsideCount = 0;
        uint32_t                  m_insideCount  = 0;
    };

} // namespace Dark
