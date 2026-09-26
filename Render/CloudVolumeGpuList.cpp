#include "Render/CloudVolumeGpuList.h"
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

    bool CloudVolumeGpuList::create(ID3D12Device* device)
    {
        m_volumes.Reset();
        m_dummy.Reset();
        m_mappedVolumes = nullptr;
        m_mappedDummy   = nullptr;
        m_volumesGpu    = 0;
        m_dummyGpu      = 0;
        m_slot          = 0;
        m_count         = 0;

        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "CloudVolumeGpuList::create: null device");
            return false;
        }

        m_volumes = CreateUploadBuffer(device, kStride * kFrameCount, "CreateCommittedResource cloud volumes");
        m_dummy   = CreateUploadBuffer(device, sizeof(GpuCloudVolume), "CreateCommittedResource cloud dummy");
        if (!m_volumes || !m_dummy)
        {
            m_volumes.Reset();
            m_dummy.Reset();
            return false;
        }

        if (FailedHr(m_volumes->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedVolumes)), "Map cloud volumes")
            || FailedHr(m_dummy->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedDummy)), "Map cloud dummy"))
        {
            m_volumes.Reset();
            m_dummy.Reset();
            m_mappedVolumes = nullptr;
            m_mappedDummy   = nullptr;
            return false;
        }

        std::memset(m_mappedVolumes, 0, static_cast<size_t>(kStride * kFrameCount));
        std::memset(m_mappedDummy, 0, sizeof(GpuCloudVolume));

        m_volumesGpu = m_volumes->GetGPUVirtualAddress();
        m_dummyGpu   = m_dummy->GetGPUVirtualAddress();

        DE_LOG_INFO(LogCategory::Render, "CloudVolumeGpuList: ready ({} volumes x {} frames)", kMaxCloudVolumes, kFrameCount);
        return true;
    }

    void CloudVolumeGpuList::upload(uint32_t frameIndex, const CloudVolumeDrawList& lists)
    {
        if (!isValid())
            return;
        m_slot  = frameIndex % kFrameCount;
        m_count = lists.count > kMaxCloudVolumes ? kMaxCloudVolumes : lists.count;
        if (m_count == 0)
            return;
        std::memcpy(m_mappedVolumes + static_cast<size_t>(m_slot) * kStride, lists.volumes, static_cast<size_t>(m_count) * sizeof(GpuCloudVolume));
    }

    D3D12_GPU_VIRTUAL_ADDRESS CloudVolumeGpuList::volumesGpuVa() const
    {
        if (!isValid() || m_count == 0)
            return m_dummyGpu;
        return m_volumesGpu + static_cast<UINT64>(m_slot) * kStride;
    }

    D3D12_GPU_VIRTUAL_ADDRESS CloudVolumeGpuList::dummyGpuVa() const
    {
        return m_dummyGpu;
    }

} // namespace Dark
