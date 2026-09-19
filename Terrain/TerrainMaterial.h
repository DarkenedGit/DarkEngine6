#pragma once

#include "Assets/AssetHandle.h"
#include "Assets/Image.h"
#include "Render/PackedSrvHeap.h"
#include "Render/TerrainPipeline.h"
#include "Render/Texture2D.h"
#include "Terrain/SplatMap.h"

#include <cstdint>
#include <d3d12.h>

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

    // Four albedo layers + splat + shadow in one PackedSrvHeap (kSrvCount = 6, shadowSlot = 5).
    class TerrainMaterial
    {
    public:
        static constexpr UINT kLayerCount = Terrain::kMaxTerrainLayers;
        static constexpr UINT kSplatSlot  = 4;
        static constexpr UINT kShadowSlot = 5;

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

        void bind(ID3D12GraphicsCommandList* cmd, UINT srvTableRootIndex) const;
        void applySurface(TerrainFrameConstants& constants) const;
        void applySurface(TerrainGBufferConstants& constants) const;

        // Layers 0–3 from cpuHandleRaw vs cpuHandle. Splat stays UNORM.
        // Stores the flag; packSrvHeap re-applies it after a rebuild (like GpuMaterial pack).
        void setLayerSamplingRaw(ID3D12Device* device, bool raw);

        bool isValid() const { return m_heap.heap != nullptr && m_splat.valid(); }

        Terrain::TerrainLayerDesc&       layer(int i) { return m_layers[i]; }
        const Terrain::TerrainLayerDesc& layer(int i) const { return m_layers[i]; }

    private:
        bool packSrvHeap(Renderer& renderer);
        void copyLayerSampling(ID3D12Device* device);
        void unbindCache();

        Texture2D                 m_layerTex[Terrain::kMaxTerrainLayers];
        Texture2D                 m_splat;
        Terrain::TerrainLayerDesc m_layers[Terrain::kMaxTerrainLayers];
        PackedSrvHeap             m_heap;
        GpuResourceCache*         m_cache = nullptr;
        bool                      m_layerSamplingRaw = false;
    };

} // namespace Dark
