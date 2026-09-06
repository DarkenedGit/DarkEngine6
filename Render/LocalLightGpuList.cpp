#include "Render/LocalLightGpuList.h"
#include "Core/Log.h"

#include <cstring>

namespace Dark
{

    namespace
    {
        bool FailedHr(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return false;
            DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return true;
        }

        ComPtr<ID3D12Resource> CreateUploadBuffer(ID3D12Device* device, UINT64 size, const char* what)
        {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width            = size;
            desc.Height           = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels        = 1;
            desc.SampleDesc       = { 1, 0 };
            desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            ComPtr<ID3D12Resource> res;
            if (FailedHr(
                    device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res)),
                    what))
            {
                return nullptr;
            }
            return res;
        }
    } // namespace

    bool LocalLightGpuList::create(ID3D12Device* device)
    {
        m_lights.Reset();
        m_volumeWorld.Reset();
        m_dummy.Reset();
        m_mappedLights = nullptr;
        m_mappedWorld  = nullptr;
        m_mappedDummy  = nullptr;
        m_lightsGpu    = 0;
        m_worldGpu     = 0;
        m_dummyGpu     = 0;
        m_slot         = 0;
        m_count        = 0;

        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "LocalLightGpuList::create: null device");
            return false;
        }

        static_assert(sizeof(GpuLocalLight) == 64, "light stride");
        static_assert(sizeof(Math::Matrix4f) == 64, "volumeWorld stride");

        m_lights      = CreateUploadBuffer(device, kLightStride * kFrameCount, "CreateCommittedResource local lights");
        m_volumeWorld = CreateUploadBuffer(device, kWorldStride * kFrameCount, "CreateCommittedResource local volumeWorld");
        m_dummy       = CreateUploadBuffer(device, 64, "CreateCommittedResource local light dummy");
        if (!m_lights || !m_volumeWorld || !m_dummy)
        {
            m_lights.Reset();
            m_volumeWorld.Reset();
            m_dummy.Reset();
            return false;
        }

        if (FailedHr(m_lights->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedLights)), "Map local lights")
            || FailedHr(m_volumeWorld->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedWorld)), "Map local volumeWorld")
            || FailedHr(m_dummy->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedDummy)), "Map local light dummy"))
        {
            m_lights.Reset();
            m_volumeWorld.Reset();
            m_dummy.Reset();
            m_mappedLights = nullptr;
            m_mappedWorld  = nullptr;
            m_mappedDummy  = nullptr;
            return false;
        }

        std::memset(m_mappedLights, 0, static_cast<size_t>(kLightStride * kFrameCount));
        std::memset(m_mappedWorld, 0, static_cast<size_t>(kWorldStride * kFrameCount));
        std::memset(m_mappedDummy, 0, 64);

        m_lightsGpu = m_lights->GetGPUVirtualAddress();
        m_worldGpu  = m_volumeWorld->GetGPUVirtualAddress();
        m_dummyGpu  = m_dummy->GetGPUVirtualAddress();

        DE_LOG_INFO(LogCategory::Render, "LocalLightGpuList: ready ({} lights x {} frames)", kMaxLocalLights, kFrameCount);
        return true;
    }

    void LocalLightGpuList::upload(uint32_t frameIndex, const LocalLightDrawLists& lists)
    {
        if (!isValid())
            return;
        m_slot  = frameIndex % kFrameCount;
        m_count = lists.count > kMaxLocalLights ? kMaxLocalLights : lists.count;
        if (m_count == 0)
            return;
        std::memcpy(m_mappedLights + static_cast<size_t>(m_slot) * kLightStride, lists.lights, static_cast<size_t>(m_count) * sizeof(GpuLocalLight));
        std::memcpy(m_mappedWorld + static_cast<size_t>(m_slot) * kWorldStride, lists.volumeWorld, static_cast<size_t>(m_count) * sizeof(Math::Matrix4f));
    }

    D3D12_GPU_VIRTUAL_ADDRESS LocalLightGpuList::lightsGpuVa() const
    {
        if (!isValid())
            return m_dummyGpu;
        return m_lightsGpu + static_cast<UINT64>(m_slot) * kLightStride;
    }

    D3D12_GPU_VIRTUAL_ADDRESS LocalLightGpuList::volumeWorldGpuVa() const
    {
        if (!isValid())
            return m_dummyGpu;
        return m_worldGpu + static_cast<UINT64>(m_slot) * kWorldStride;
    }

    D3D12_GPU_VIRTUAL_ADDRESS LocalLightGpuList::dummyGpuVa() const
    {
        return m_dummyGpu;
    }

} // namespace Dark
