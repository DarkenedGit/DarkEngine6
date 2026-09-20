#include "Render/PackedSrvHeap.h"
#include "Core/Log.h"

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

    bool packFromCpuHandles(ID3D12Device* device, PackedSrvHeap& out, const D3D12_CPU_DESCRIPTOR_HANDLE* src, UINT count)
    {
        out.heap.Reset();
        out.gpu = {};
        if (!device || !src || count == 0)
        {
            DE_LOG_ERROR(LogCategory::Render, "PackedSrvHeap: invalid pack args");
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = count;
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&out.heap)), "CreateDescriptorHeap packed SRVs"))
            return false;

        out.srvCount = count;
        const UINT incr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE dst = out.heap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < count; ++i)
        {
            if (src[i].ptr != 0)
                device->CopyDescriptorsSimple(1, dst, src[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            dst.ptr += static_cast<SIZE_T>(incr);
        }
        out.gpu = out.heap->GetGPUDescriptorHandleForHeapStart();
        return true;
    }

    void copyShadow(ID3D12Device* device, PackedSrvHeap& heap, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu)
    {
        if (!device || !heap.heap || shadowCpu.ptr == 0)
            return;
        if (heap.shadowSlot >= heap.srvCount)
        {
            DE_LOG_ERROR(LogCategory::Render, "PackedSrvHeap: shadowSlot {} >= srvCount {}", heap.shadowSlot, heap.srvCount);
            return;
        }
        const UINT incr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE dst = heap.heap->GetCPUDescriptorHandleForHeapStart();
        dst.ptr += static_cast<SIZE_T>(heap.shadowSlot) * incr;
        device->CopyDescriptorsSimple(1, dst, shadowCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    void copySplat(ID3D12Device* device, PackedSrvHeap& heap, D3D12_CPU_DESCRIPTOR_HANDLE splatCpu)
    {
        heap.splatCpu = splatCpu;
        if (!device || !heap.heap || splatCpu.ptr == 0)
            return;
        if (heap.splatSlot >= heap.srvCount)
        {
            DE_LOG_ERROR(LogCategory::Render, "PackedSrvHeap: splatSlot {} >= srvCount {}", heap.splatSlot, heap.srvCount);
            return;
        }
        const UINT incr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE dst = heap.heap->GetCPUDescriptorHandleForHeapStart();
        dst.ptr += static_cast<SIZE_T>(heap.splatSlot) * incr;
        device->CopyDescriptorsSimple(1, dst, splatCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

} // namespace Dark
