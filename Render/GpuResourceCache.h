#pragma once

#include "Assets/AssetHandle.h"
#include "Render/PackedSrvHeap.h"

#include <cstdint>
#include <d3d12.h>
#include <unordered_map>
#include <vector>

namespace Dark
{

    class Renderer;
    class Material;
    class GpuMaterial;

    // Upload/bind intern for GPU artifacts. M3b: mesh materials only (bridge).
    // PackedSrvHeap* entries are non-owning — Material still owns GpuMaterial until M3c.
    class GpuResourceCache
    {
    public:
        explicit GpuResourceCache(Renderer& renderer);

        GpuResourceCache(const GpuResourceCache&)            = delete;
        GpuResourceCache& operator=(const GpuResourceCache&) = delete;

        // False + log if null, unregistered (id == 0), or not packed. Never inserts key 0.
        bool ensureMaterial(const AssetRef<Material>& material);

        GpuMaterial* material(AssetID materialId) const;

        void bindMaterial(ID3D12GraphicsCommandList* cmd, const Material& material, UINT albedoSrvRootIndex) const;

        // Patches every registered packed heap's shadowSlot. Remembers handle for later ensure*.
        // Does not patch SceneBuffers, sky, or water.
        void setShadowSrv(D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu);

        void collectUnused();
        void clear();

        void unregisterPackedHeap(PackedSrvHeap* heap);

        struct Stats
        {
            uint32_t materials     = 0;
            uint32_t packedHeaps   = 0;
            uint32_t shadowPatches = 0;
        };
        Stats stats() const;

    private:
        struct MatEntry
        {
            AssetWeakRef<Material> cpu;
            GpuMaterial*           gpu = nullptr; // owned by Material::m_gpu (M3b)
        };

        void registerPackedHeap(PackedSrvHeap* heap);

        Renderer*                   m_renderer = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE m_shadowCpu{};
        std::unordered_map<AssetID, MatEntry> m_materials;
        std::vector<PackedSrvHeap*>           m_packedHeaps;
        uint32_t                              m_shadowPatches = 0;
    };

} // namespace Dark
