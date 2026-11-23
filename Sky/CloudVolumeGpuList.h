#pragma once

#include "Sky/CloudVolume.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    class CloudVolumeGpuList
    {
    public:
        static constexpr uint32_t kFrameCount = 2;

        CloudVolumeGpuList() = default;

        bool create(ID3D12Device* device);
        void upload(uint32_t frameIndex, const CloudVolumeDrawList& lists);

        D3D12_GPU_VIRTUAL_ADDRESS volumesGpuVa() const;
        D3D12_GPU_VIRTUAL_ADDRESS dummyGpuVa() const;

        bool     isValid() const { return m_volumes && m_dummy && m_mappedVolumes && m_mappedDummy; }
        uint32_t count() const { return m_count; }

    private:
        static constexpr UINT64 kStride = static_cast<UINT64>(kMaxCloudVolumes) * sizeof(GpuCloudVolume);

        ComPtr<ID3D12Resource>        m_volumes;
        ComPtr<ID3D12Resource>        m_dummy;
        UINT8*                        m_mappedVolumes = nullptr;
        UINT8*                        m_mappedDummy   = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS     m_volumesGpu    = 0;
        D3D12_GPU_VIRTUAL_ADDRESS     m_dummyGpu      = 0;
        uint32_t                      m_slot          = 0;
        uint32_t                      m_count         = 0;
    };

} // namespace Dark
