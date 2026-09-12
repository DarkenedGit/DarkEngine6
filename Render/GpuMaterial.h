#pragma once

#include "Render/PackedSrvHeap.h"

#include <cstdint>
#include <d3d12.h>

namespace Dark
{

    class Texture2D;

    // D3D12 bind group for a mesh material: albedo + shadow in one SHADER_VISIBLE heap.
    class GpuMaterial
    {
    public:
        static constexpr UINT kAlbedoSlot = 0;
        static constexpr UINT kShadowSlot = 1;
        static constexpr UINT kSrvCount   = 2; // MeshPipeline::kSrvCount

        bool pack(ID3D12Device* device, const Texture2D& albedo);
        void bind(ID3D12GraphicsCommandList* cmd, UINT albedoSrvRootIndex) const;
        void setShadowSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu);

        bool isValid() const { return m_heap.heap != nullptr; }

        PackedSrvHeap&       packedHeap() { return m_heap; }
        const PackedSrvHeap& packedHeap() const { return m_heap; }

    private:
        PackedSrvHeap m_heap;
    };

} // namespace Dark
