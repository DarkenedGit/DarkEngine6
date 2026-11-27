#include "Render/ColorRemapCb.h"

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
    } // namespace

    bool ColorRemapCb::create(ID3D12Device* device)
    {
        destroy();
        if (!device)
            return false;

        D3D12_HEAP_PROPERTIES uploadHeap{};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC   cbDesc{};
        cbDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        cbDesc.Width            = static_cast<UINT64>(kSlotBytes) * kFrames;
        cbDesc.Height           = 1;
        cbDesc.DepthOrArraySize = 1;
        cbDesc.MipLevels        = 1;
        cbDesc.SampleDesc       = { 1, 0 };
        cbDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FailedHr(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_upload)),
                     "CreateCommittedResource ColorRemap CBV"))
            return false;
        if (FailedHr(m_upload->Map(0, nullptr, reinterpret_cast<void**>(&m_mapped)), "Map ColorRemap CBV"))
        {
            destroy();
            return false;
        }
        m_gpu = m_upload->GetGPUVirtualAddress();
        std::memset(m_mapped, 0, static_cast<size_t>(kSlotBytes) * kFrames);

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = kFrames;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_cpuHeap)), "CreateDescriptorHeap ColorRemap CBV"))
        {
            destroy();
            return false;
        }
        const UINT                            incr  = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const D3D12_CPU_DESCRIPTOR_HANDLE     start = m_cpuHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < kFrames; ++i)
        {
            m_cpu[i] = D3D12_CPU_DESCRIPTOR_HANDLE{ start.ptr + static_cast<SIZE_T>(i) * incr };
            D3D12_CONSTANT_BUFFER_VIEW_DESC view{};
            view.BufferLocation = m_gpu + static_cast<UINT64>(i) * kSlotBytes;
            view.SizeInBytes    = kSlotBytes;
            device->CreateConstantBufferView(&view, m_cpu[i]);
        }
        write(0, ColorRemap{}, ColorRemap{});
        write(1, ColorRemap{}, ColorRemap{});
        return true;
    }

    void ColorRemapCb::destroy()
    {
        if (m_upload && m_mapped)
            m_upload->Unmap(0, nullptr);
        m_mapped = nullptr;
        m_gpu    = 0;
        m_cpu[0] = {};
        m_cpu[1] = {};
        m_cpuHeap.Reset();
        m_upload.Reset();
    }

    void ColorRemapCb::write(uint32_t frameIndex, const ColorRemap& first, const ColorRemap& fog)
    {
        if (!m_mapped)
            return;
        const UINT i = slotIndex(frameIndex);
        struct Packed
        {
            ColorRemapGpu a;
            ColorRemapGpu fog;
        };
        Packed p{};
        p.a   = packColorRemap(first);
        p.fog = packColorRemap(fog);
        std::memcpy(m_mapped + static_cast<size_t>(i) * kSlotBytes, &p, sizeof(p));
    }

    D3D12_CPU_DESCRIPTOR_HANDLE ColorRemapCb::cpu(uint32_t frameIndex) const
    {
        if (!m_mapped)
            return {};
        return m_cpu[slotIndex(frameIndex)];
    }

} // namespace Dark
