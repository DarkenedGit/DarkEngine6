#pragma once

#include "Assets/AssetHandle.h"
#include "Assets/Image.h"
#include "Render/PackedSrvHeap.h"
#include "Render/TerrainPipeline.h"
#include "Render/Texture2D.h"
#include "Terrain/SplatMap.h"

#include <cstdint>
#include <d3d12.h>
#include <memory>

namespace Dark
{

    class Renderer;
    class GpuResourceCache;

    namespace Terrain
    {

        struct TerrainMaterialParams
        {
            float heightBlendK   = 0.5f;
            float heightBlendT   = 0.1f;
            float triplanarSlope = 0.45f;
        };

        struct TerrainSurfaceDesc
        {
            TerrainLayerDesc  layers[kMaxTerrainLayers];
            AssetRef<Image>   albedo[kMaxTerrainLayers];
            AssetRef<Image>   normal[kMaxTerrainLayers];
            AssetRef<Image>   orm[kMaxTerrainLayers];
            TerrainMaterialParams params;
        };

    } // namespace Terrain

    // 14-slot metal-rough heap: 4×(albedo, normal, ORM) + splat + shadow last.
    class TerrainMaterial
    {
    public:
        static constexpr UINT kLayerCount = Terrain::kMaxTerrainLayers;
        static constexpr UINT kAlbedo0    = 0;
        static constexpr UINT kNormal0    = 1;
        static constexpr UINT kOrm0       = 2;
        static constexpr UINT kSplatSlot  = 12;
        static constexpr UINT kShadowSlot = 13;
        static constexpr UINT kMapSrvCount = 13;
        static constexpr UINT kSrvCount    = 14;
        static constexpr uint32_t kMaxLayerImageSize = 2048;

        static constexpr UINT layerAlbedoSlot(UINT i) { return kAlbedo0 + 3u * i; }
        static constexpr UINT layerNormalSlot(UINT i) { return kNormal0 + 3u * i; }
        static constexpr UINT layerOrmSlot(UINT i) { return kOrm0 + 3u * i; }

        TerrainMaterial() = default;
        ~TerrainMaterial();

        TerrainMaterial(TerrainMaterial&& other) noexcept;
        TerrainMaterial& operator=(TerrainMaterial&& other) noexcept;
        TerrainMaterial(const TerrainMaterial&)            = delete;
        TerrainMaterial& operator=(const TerrainMaterial&) = delete;

        bool createDefault(Renderer& renderer, const Terrain::SplatMap& splat);

        bool create(
            Renderer& renderer,
            Texture2D layers[Terrain::kMaxTerrainLayers],
            Texture2D&& splat,
            const Terrain::TerrainLayerDesc layerDescs[Terrain::kMaxTerrainLayers]);

        bool create(Renderer& renderer, const Terrain::TerrainSurfaceDesc& desc, Texture2D&& splat);

        // Re-upload splat GPU and pack. Layer maps / checkers stay.
        bool uploadSplat(Renderer& renderer, const Terrain::SplatMap& splat);
        // Intern layer maps from desc. Empty albedo keeps createDefault checkers when present.
        bool applySurfaceDesc(Renderer& renderer, const Terrain::TerrainSurfaceDesc& desc);

        void bind(ID3D12GraphicsCommandList* cmd, UINT srvTableRootIndex) const;
        void applySurface(TerrainFrameConstants& constants) const;
        void applySurface(TerrainGBufferConstants& constants, float worldSizeX, float worldSizeZ) const;

        // Albedo slots 0,3,6,9 from cpuHandleRaw vs cpuHandle. Data maps stay UNORM.
        // Stores the flag; packSrvHeap re-applies it after a rebuild (like GpuMaterial pack).
        void setLayerSamplingRaw(ID3D12Device* device, bool raw);

        bool isValid() const { return m_heap.heap != nullptr && m_splat.valid(); }

        Terrain::TerrainLayerDesc&       layer(int i) { return m_layers[i]; }
        const Terrain::TerrainLayerDesc& layer(int i) const { return m_layers[i]; }

        Terrain::TerrainMaterialParams&       params() { return m_params; }
        const Terrain::TerrainMaterialParams& params() const { return m_params; }

    private:
        bool packSrvHeap(Renderer& renderer);
        void copyLayerSampling(ID3D12Device* device);
        void unbindCache();
        const Texture2D* albedoTex(int i) const;

        Texture2D                      m_albedoOwned[Terrain::kMaxTerrainLayers];
        std::shared_ptr<Texture2D>     m_albedoGpu[Terrain::kMaxTerrainLayers];
        std::shared_ptr<Texture2D>     m_normalGpu[Terrain::kMaxTerrainLayers];
        std::shared_ptr<Texture2D>     m_ormGpu[Terrain::kMaxTerrainLayers];
        const Texture2D*               m_albedoSrv[Terrain::kMaxTerrainLayers]{};
        Texture2D                      m_splat;
        Terrain::TerrainLayerDesc      m_layers[Terrain::kMaxTerrainLayers];
        Terrain::TerrainMaterialParams m_params;
        PackedSrvHeap                  m_heap;
        GpuResourceCache*              m_cache = nullptr;
        bool                           m_layerSamplingRaw = false;
    };

    static_assert(TerrainMaterial::kAlbedo0 + 3u * 3u == 9u, "albedo slots 0,3,6,9");
    static_assert(TerrainMaterial::kSplatSlot == 12u, "splat t12");
    static_assert(TerrainMaterial::kShadowSlot == 13u, "shadow last");
    static_assert(TerrainMaterial::kSrvCount == 14u, "forward table");
    static_assert(TerrainMaterial::kMapSrvCount == 13u, "gbuffer table");
    static_assert(TerrainMaterial::kSrvCount == TerrainPipeline::kSrvCount, "terrain heap vs pipeline");
    static_assert(TerrainMaterial::kMapSrvCount == TerrainPipeline::kMapSrvCount, "terrain gbuffer table");

} // namespace Dark
