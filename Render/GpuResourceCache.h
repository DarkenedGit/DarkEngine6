#pragma once

#include "Assets/AssetHandle.h"
#include "Math/Color.h"
#include "Render/GpuIbl.h"
#include "Render/GpuMaterial.h"
#include "Render/GpuModel.h"
#include "Render/PackedSrvHeap.h"
#include "Render/Texture2D.h"

#include <cstdint>
#include <d3d12.h>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Dark
{

    class Renderer;
    class Material;
    class Image;
    class Model;

    enum class CachedTextureReuse : uint8_t
    {
        Create = 0, // missing / invalid GPU — upload
        Reuse,      // valid GPU, same usage
        Conflict,   // valid GPU, different usage — keep first
    };

    inline CachedTextureReuse classifyCachedTextureReuse(bool gpuValid, Color::TextureUsage cachedUsage, Color::TextureUsage requestedUsage)
    {
        if (!gpuValid)
            return CachedTextureReuse::Create;
        return (cachedUsage == requestedUsage) ? CachedTextureReuse::Reuse : CachedTextureReuse::Conflict;
    }

    // GPU artifacts for interned CPU materials. Owns GpuMaterial heaps.
    class GpuResourceCache
    {
    public:
        explicit GpuResourceCache(Renderer& renderer);
        explicit GpuResourceCache(Renderer* renderer);

        GpuResourceCache(const GpuResourceCache&)            = delete;
        GpuResourceCache& operator=(const GpuResourceCache&) = delete;

        bool ensureTexture(const AssetRef<Image>& image, Color::TextureUsage usage);
        bool ensureMaterial(const AssetRef<Material>& material);
        bool ensureModel(const AssetRef<Model>& model);
        bool ensureIbl(const AssetRef<Image>& hdrEquirect);

        std::shared_ptr<Texture2D> texture(AssetID imageId) const;
        GpuMaterial*               material(AssetID materialId) const;
        GpuModel*                  model(AssetID modelId) const;
        GpuIbl*                    ibl(AssetID imageId) const;
        AssetRef<Material>         cpuMaterial(AssetID materialId) const;

        const Texture2D* defaultNormal() const { return m_defaultNormal.get(); }
        const Texture2D* defaultOrm() const { return m_defaultOrm.get(); }
        const Texture2D* defaultEmissive() const { return m_defaultEmissive.get(); }
        const Texture2D* defaultTerrainAlbedo() const { return m_defaultTerrainAlbedo.get(); }
        const Texture2D* defaultTerrainOrm() const { return m_defaultTerrainOrm.get(); }

        bool ensureTerrainDefaults();
        const Texture2D* resolveMapOrDefault(const AssetRef<Image>& image, Color::TextureUsage usage, const Texture2D* fallback);

        void bindMaterial(ID3D12GraphicsCommandList* cmd, const Material& material, UINT albedoSrvRootIndex) const;
        void bindMaterial(ID3D12GraphicsCommandList* cmd, AssetID materialId, UINT albedoSrvRootIndex) const;

        // Patches every registered packed heap's shadowSlot. Remembers handle for later ensure*.
        // Does not patch SceneBuffers, sky, or water.
        void setShadowSrv(D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu);

        // Pack GpuMaterial albedo from cpuHandleRaw (true) vs cpuHandle (false, _SRGB).
        // Iterates interned materials only — not terrain splat / other packed heaps.
        void setAlbedoSamplingRaw(bool raw);

        void collectUnused();
        void clear();

        // Non-asset heaps (terrain). Pointer identity; caller unregisters before the heap moves/dies.
        void registerPackedHeap(PackedSrvHeap* heap);
        void unregisterPackedHeap(PackedSrvHeap* heap);

        struct Stats
        {
            uint32_t textures      = 0;
            uint32_t materials     = 0;
            uint32_t models        = 0;
            uint32_t packedHeaps   = 0;
            uint32_t shadowPatches = 0;
        };
        Stats stats() const;

    private:
        struct TexEntry
        {
            AssetWeakRef<Image>        cpu;
            std::shared_ptr<Texture2D> gpu;
            Color::TextureUsage        usage = Color::TextureUsage::Albedo;
        };
        struct MatEntry
        {
            AssetWeakRef<Material>       cpu;
            std::unique_ptr<GpuMaterial> gpu;
        };
        struct ModEntry
        {
            AssetWeakRef<Model>       cpu;
            std::unique_ptr<GpuModel> gpu;
        };
        struct IblEntry
        {
            AssetWeakRef<Image>     cpu;
            std::unique_ptr<GpuIbl> gpu;
        };

        bool ensureDefaultMaps();

        Renderer*                   m_renderer = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE m_shadowCpu{};
        bool                        m_albedoSamplingRaw = false;
        std::unique_ptr<Texture2D>  m_defaultNormal;
        std::unique_ptr<Texture2D>  m_defaultOrm;
        std::unique_ptr<Texture2D>  m_defaultEmissive;
        std::unique_ptr<Texture2D>  m_defaultTerrainAlbedo;
        std::unique_ptr<Texture2D>  m_defaultTerrainOrm;
        std::unordered_map<AssetID, TexEntry> m_textures;
        std::unordered_map<AssetID, MatEntry> m_materials;
        std::unordered_map<AssetID, ModEntry> m_models;
        std::unordered_map<AssetID, IblEntry> m_ibl;
        std::vector<PackedSrvHeap*>           m_packedHeaps;
        uint32_t                              m_shadowPatches = 0;
    };

} // namespace Dark
