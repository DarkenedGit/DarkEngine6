#pragma once

#include "Render/FrameSetup.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    // 256-byte lighting / local-light upload of albedoRemap then fogRemap. Bound from a
    // descriptor-table range (lighting b2, local lights b1).
    class ColorRemapCb
    {
    public:
        static constexpr UINT kSlotBytes = 256;
        static constexpr UINT kFrames    = 2;

        bool create(ID3D12Device* device);
        void destroy();

        void write(uint32_t frameIndex, const ColorRemap& first, const ColorRemap& fog);
        D3D12_CPU_DESCRIPTOR_HANDLE cpu(uint32_t frameIndex) const
        {
            if (!m_mapped)
                return {};
            return m_cpu[slotIndex(frameIndex)];
        }
        bool valid() const { return m_mapped != nullptr; }

    private:
        UINT slotIndex(uint32_t frameIndex) const { return frameIndex % kFrames; }

        Microsoft::WRL::ComPtr<ID3D12Resource>      m_upload;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_cpuHeap;
        uint8_t*                                    m_mapped = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS                   m_gpu    = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE                 m_cpu[kFrames]{};
    };

} // namespace Dark
