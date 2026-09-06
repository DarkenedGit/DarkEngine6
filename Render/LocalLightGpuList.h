#pragma once

#include "Render/LocalLightGather.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // 2-frame UPLOAD ring for packed lights + volume world matrices. Root SRV VAs only — no descriptor heap.
    class LocalLightGpuList
    {
    public:
        static constexpr uint32_t kFrameCount = 2;

        LocalLightGpuList() = default;

        bool create(ID3D12Device* device);
        void upload(uint32_t frameIndex, const LocalLightDrawLists& lists);

        D3D12_GPU_VIRTUAL_ADDRESS lightsGpuVa() const;
        D3D12_GPU_VIRTUAL_ADDRESS volumeWorldGpuVa() const;
        D3D12_GPU_VIRTUAL_ADDRESS dummyGpuVa() const;

        bool     isValid() const { return m_lights && m_volumeWorld && m_mappedLights && m_mappedWorld; }
        uint32_t count() const { return m_count; }

    private:
        static constexpr UINT64 kLightStride = static_cast<UINT64>(kMaxLocalLights) * sizeof(GpuLocalLight);
        static constexpr UINT64 kWorldStride = static_cast<UINT64>(kMaxLocalLights) * sizeof(Math::Matrix4f);

        ComPtr<ID3D12Resource> m_lights;
        ComPtr<ID3D12Resource> m_volumeWorld;
        ComPtr<ID3D12Resource> m_dummy;
        UINT8*                 m_mappedLights = nullptr;
        UINT8*                 m_mappedWorld  = nullptr;
        UINT8*                 m_mappedDummy  = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS m_lightsGpu = 0;
        D3D12_GPU_VIRTUAL_ADDRESS m_worldGpu  = 0;
        D3D12_GPU_VIRTUAL_ADDRESS m_dummyGpu  = 0;
        uint32_t               m_slot         = 0;
        uint32_t               m_count        = 0;
    };

} // namespace Dark
