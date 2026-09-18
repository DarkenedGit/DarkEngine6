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
        static constexpr UINT kShadowSlot   = 4;
        static constexpr UINT kMapSrvCount  = 4; // G-buffer table (prefix, no shadow)
        static constexpr UINT kSrvCount     = 5; // forward table

        bool pack(ID3D12Device* device, const Texture2D& albedo, const Texture2D& normal, const Texture2D& orm, const Texture2D& emissive);
        void bind(ID3D12GraphicsCommandList* cmd, UINT albedoSrvRootIndex) const;
        void setShadowSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu);

        void           setPackedMapIds(AssetID albedo, AssetID normal, AssetID orm, AssetID emissive);
        const AssetID* packedMapIds() const { return m_packedMapIds; }

        bool isValid() const { return m_heap.heap != nullptr; }

        PackedSrvHeap&       packedHeap() { return m_heap; }
        const PackedSrvHeap& packedHeap() const { return m_heap; }

    private:
        PackedSrvHeap m_heap;
        AssetID       m_packedMapIds[kMapSrvCount]{ NULL_ASSET, NULL_ASSET, NULL_ASSET, NULL_ASSET };
    };

} // namespace Dark
