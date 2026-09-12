#pragma once

#include "Assets/AssetHandle.h"
#include "Render/GpuMaterial.h"
#include "Render/PackedSrvHeap.h"

#include <cstdint>
#include <d3d12.h>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Dark
{

    class Renderer;
    class Material;

    // GPU artifacts for interned CPU materials. Owns GpuMaterial heaps.
    class GpuResourceCache
    {
    public:
        explicit GpuResourceCache(Renderer& renderer);

        GpuResourceCache(const GpuResourceCache&)            = delete;
        GpuResourceCache& operator=(const GpuResourceCache&) = delete;

        // False + log if null, unregistered (id == 0), or albedo missing. Never inserts key 0.
        bool ensureMaterial(const AssetRef<Material>& material);

        GpuMaterial* material(AssetID materialId) const;

        void bindMaterial(ID3D12GraphicsCommandList* cmd, const Material& material, UINT albedoSrvRootIndex) const;

        // Patches every registered packed heap's shadowSlot. Remembers handle for later ensure*.
        // Does not patch SceneBuffers, sky, or water.
        void setShadowSrv(D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu);

        void collectUnused();
        void clear();

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
            AssetWeakRef<Material>        cpu;
            std::unique_ptr<GpuMaterial>  gpu;
        };

        void registerPackedHeap(PackedSrvHeap* heap);
        void unregisterPackedHeap(PackedSrvHeap* heap);

        Renderer*                   m_renderer = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE m_shadowCpu{};
        std::unordered_map<AssetID, MatEntry> m_materials;
        std::vector<PackedSrvHeap*>           m_packedHeaps;
        uint32_t                              m_shadowPatches = 0;
    };

} // namespace Dark
