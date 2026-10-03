#include "Render/DecalGpuList.h"
#include "Render/DecalPool.h"
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

        constexpr UINT64 kRegionBytes = static_cast<UINT64>(DecalGpuList::kCapacity) * sizeof(DecalGpuInstance);
        constexpr UINT64 kFrameBytes  = kRegionBytes * 2;
    } // namespace

    bool DecalGpuList::create(ID3D12Device* device)
    {
        if (m_buffer && m_mapped)
            m_buffer->Unmap(0, nullptr);
        m_buffer.Reset();
        m_mapped       = nullptr;
        m_gpu          = 0;
        m_slot         = 0;
        m_outsideCount = 0;
        m_insideCount  = 0;

        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "DecalGpuList::create: null device");
            return false;
        }

        static_assert(sizeof(DecalGpuInstance) == 224, "decal instance bytes");
        static_assert(DecalGpuList::kCapacity == DecalPool::kCapacity, "decal upload cap");

        m_buffer = CreateUploadBuffer(device, kFrameBytes * kFrameCount, "CreateCommittedResource decal instances");
        if (!m_buffer)
            return false;

        if (FailedHr(m_buffer->Map(0, nullptr, reinterpret_cast<void**>(&m_mapped)), "Map decal instances"))
        {
            m_buffer.Reset();
            m_mapped = nullptr;
            return false;
        }

        std::memset(m_mapped, 0, static_cast<size_t>(kFrameBytes * kFrameCount));
        m_gpu = m_buffer->GetGPUVirtualAddress();
        DE_LOG_INFO(LogCategory::Render, "DecalGpuList: ready ({} x {} frames)", kCapacity, kFrameCount);
        return true;
    }

    void DecalGpuList::upload(uint32_t frameIndex, const DecalGpuInstance* outside, uint32_t outsideCount, const DecalGpuInstance* inside, uint32_t insideCount)
    {
        if (!isValid())
            return;
        m_slot         = frameIndex % kFrameCount;
        m_outsideCount = outsideCount > kCapacity ? kCapacity : outsideCount;
        m_insideCount  = insideCount > kCapacity ? kCapacity : insideCount;

        uint8_t* base = m_mapped + static_cast<size_t>(m_slot) * static_cast<size_t>(kFrameBytes);
        if (m_outsideCount > 0 && outside)
            std::memcpy(base, outside, static_cast<size_t>(m_outsideCount) * sizeof(DecalGpuInstance));
        if (m_insideCount > 0 && inside)
            std::memcpy(base + static_cast<size_t>(kRegionBytes), inside, static_cast<size_t>(m_insideCount) * sizeof(DecalGpuInstance));
    }

    D3D12_GPU_VIRTUAL_ADDRESS DecalGpuList::outsideGpuVa() const
    {
        if (!isValid())
            return 0;
        return m_gpu + static_cast<UINT64>(m_slot) * kFrameBytes;
    }

    D3D12_GPU_VIRTUAL_ADDRESS DecalGpuList::insideGpuVa() const
    {
        if (!isValid())
            return 0;
        return outsideGpuVa() + kRegionBytes;
    }

} // namespace Dark
