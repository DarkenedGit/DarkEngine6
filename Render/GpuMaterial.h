#pragma once

#include "Assets/AssetHandle.h"
#include "Render/PackedSrvHeap.h"

#include <cstdint>
#include <d3d12.h>

namespace Dark
{

    class Texture2D;

    // D3D12 bind group for a mesh material: maps + shadow (last) in one SHADER_VISIBLE heap.
    class GpuMaterial
    {
    public:
        static constexpr UINT kAlbedoSlot   = 0;
        static constexpr UINT kNormalSlot   = 1;
        static constexpr UINT kOrmSlot      = 2;
        static constexpr UINT kEmissiveSlot = 3;
        static constexpr UINT kUvSlot       = 4; // space1 t0. t4 in space0 stays free for instance buffers.
        static constexpr UINT kShadowSlot   = 5;
        static constexpr UINT kMapSrvCount  = 5; // G-buffer table (maps + UV, no shadow)
        static constexpr UINT kSrvCount     = 6; // forward table

        bool pack(ID3D12Device* device, const Texture2D& albedo, const Texture2D& normal, const Texture2D& orm, const Texture2D& emissive, const Texture2D& uv);
        void bind(ID3D12GraphicsCommandList* cmd, UINT albedoSrvRootIndex) const;
        void setShadowSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu);

        void           setPackedMapIds(AssetID albedo, AssetID normal, AssetID orm, AssetID emissive, AssetID uv);
        const AssetID* packedMapIds() const { return m_packedMapIds; }

        bool isValid() const { return m_heap.heap != nullptr; }

        PackedSrvHeap&       packedHeap() { return m_heap; }
        const PackedSrvHeap& packedHeap() const { return m_heap; }

    private:
        PackedSrvHeap m_heap;
        AssetID       m_packedMapIds[kMapSrvCount]{};
    };

    // G-buffer table: t0-t3 space0, then the UV mask at t0 space1. One root DWORD.
    inline void fillGBufferMapRanges(D3D12_DESCRIPTOR_RANGE out[2])
    {
        out[0]                                 = {};
        out[0].RangeType                       = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        out[0].NumDescriptors                  = GpuMaterial::kEmissiveSlot + 1u;
        out[0].BaseShaderRegister              = 0;
        out[0].RegisterSpace                   = 0;
        out[0].OffsetInDescriptorsFromTableStart = 0;

        out[1]                                 = {};
        out[1].RangeType                       = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        out[1].NumDescriptors                  = 1;
        out[1].BaseShaderRegister              = 0;
        out[1].RegisterSpace                   = 1;
        out[1].OffsetInDescriptorsFromTableStart = GpuMaterial::kUvSlot;
    }

    // Forward table appends the shadow map at t4 space0. SHADOW_T stays t4.
    inline void fillForwardMapRanges(D3D12_DESCRIPTOR_RANGE out[3])
    {
        D3D12_DESCRIPTOR_RANGE maps[2]{};
        fillGBufferMapRanges(maps);
        out[0] = maps[0];
        out[1] = maps[1];
        out[2]                                 = {};
        out[2].RangeType                       = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        out[2].NumDescriptors                  = 1;
        out[2].BaseShaderRegister              = 4;
        out[2].RegisterSpace                   = 0;
        out[2].OffsetInDescriptorsFromTableStart = GpuMaterial::kShadowSlot;
    }

} // namespace Dark
