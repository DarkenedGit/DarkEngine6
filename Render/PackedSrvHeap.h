#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // Shader-visible CBV_SRV_UAV heap packed from FLAG_NONE CPU sources.
    // Slot indices are explicit so terrain (14 slots, shadow at 13) can share this type.
    struct PackedSrvHeap
    {
        ComPtr<ID3D12DescriptorHeap> heap;
        D3D12_GPU_DESCRIPTOR_HANDLE  gpu{};
        UINT                         shadowSlot = 4; // GpuMaterial last slot; terrain sets 13
        UINT                         splatSlot  = 12;
        UINT                         srvCount   = 5;
        // Last copySplat source. CPU tests inspect this without a device / shader-visible heap.
        D3D12_CPU_DESCRIPTOR_HANDLE  splatCpu{};
    };

    bool packFromCpuHandles(ID3D12Device* device, PackedSrvHeap& out, const D3D12_CPU_DESCRIPTOR_HANDLE* src, UINT count);
    void copyShadow(ID3D12Device* device, PackedSrvHeap& heap, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu);
    void copySplat(ID3D12Device* device, PackedSrvHeap& heap, D3D12_CPU_DESCRIPTOR_HANDLE splatCpu);

} // namespace Dark
