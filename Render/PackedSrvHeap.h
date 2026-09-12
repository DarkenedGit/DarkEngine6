#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // Shader-visible CBV_SRV_UAV heap packed from FLAG_NONE CPU sources.
    // Slot indices are explicit so terrain (6 slots, shadow at 5) can share this type.
    struct PackedSrvHeap
    {
        ComPtr<ID3D12DescriptorHeap> heap;
        D3D12_GPU_DESCRIPTOR_HANDLE  gpu{};
        UINT                         shadowSlot = 1;
        UINT                         srvCount   = 2;
    };

    bool packFromCpuHandles(ID3D12Device* device, PackedSrvHeap& out, const D3D12_CPU_DESCRIPTOR_HANDLE* src, UINT count);
    void copyShadow(ID3D12Device* device, PackedSrvHeap& heap, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu);

} // namespace Dark
