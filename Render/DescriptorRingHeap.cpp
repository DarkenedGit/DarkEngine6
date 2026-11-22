#include "Render/DescriptorRingHeap.h"

#include "Core/Log.h"

namespace Dark
{

    bool DescriptorRingHeap::init(ID3D12Device* device, uint32_t capacity, uint32_t persistent)
    {
        shutdown();
        if (!device || !m_ring.init(capacity, persistent))
            return false;
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = capacity;
        desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        const HRESULT hr    = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap));
        if (FAILED(hr) || !m_heap)
        {
            DE_LOG_ERROR(LogCategory::Render, "DescriptorRing: CreateDescriptorHeap ({} descriptors) failed (HRESULT 0x{:08X})", capacity, static_cast<unsigned>(hr));
            m_heap.Reset();
            m_ring.reset();
            return false;
        }
        m_heap->SetName(L"DE.DescriptorRing");
        m_device    = device;
        m_cpuStart  = m_heap->GetCPUDescriptorHandleForHeapStart();
        m_gpuStart  = m_heap->GetGPUDescriptorHandleForHeapStart();
        m_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        DE_LOG_INFO(LogCategory::Render, "DescriptorRing: {} descriptors ({} persistent, {} per-frame ring)", capacity, persistent, capacity - persistent);
        return true;
    }

    void DescriptorRingHeap::shutdown()
    {
        m_heap.Reset();
        m_device.Reset();
        m_ring.reset();
        m_cpuStart   = {};
        m_gpuStart   = {};
        m_increment  = 0;
        m_loggedFull = false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorRingHeap::cpu(uint32_t offset) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_cpuStart;
        h.ptr += static_cast<SIZE_T>(offset) * m_increment;
        return h;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorRingHeap::gpu(uint32_t offset) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE h = m_gpuStart;
        h.ptr += static_cast<UINT64>(offset) * m_increment;
        return h;
    }

    void DescriptorRingHeap::bind(ID3D12GraphicsCommandList* cmd) const
    {
        if (!cmd || !m_heap)
            return;
        ID3D12DescriptorHeap* heaps[] = { m_heap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorRingHeap::frameTable(const D3D12_CPU_DESCRIPTOR_HANDLE* sources, uint32_t count)
    {
        if (!m_heap || !sources || count == 0)
            return {};
        const uint32_t offset = m_ring.allocateFrame(count);
        if (offset == DescriptorRing::kInvalid)
        {
            if (!m_loggedFull)
            {
                m_loggedFull = true;
                DE_LOG_ERROR(LogCategory::Render, "DescriptorRing: frame region full ({} in use of {}); table of {} dropped (logs once)", m_ring.frameUsed(), m_ring.frameCount(), count);
            }
            return {};
        }
        // One destination range, `count` single-descriptor source ranges.
        const D3D12_CPU_DESCRIPTOR_HANDLE dst      = cpu(offset);
        const UINT                        dstCount = count;
        m_device->CopyDescriptors(1, &dst, &dstCount, count, sources, nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return gpu(offset);
    }

} // namespace Dark
