#pragma once

#include "Math/Matrix4f.h"
#include "Math/Sphere3f.h"
#include "Render/Frustum3f.h"
#include "Terrain/FoliageFile.h"

#include <cstdint>
#include <d3d12.h>
#include <memory>
#include <vector>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    class Renderer;
    class AssetManager;
    class Camera3D;
    class FoliagePrototypes;
    class GpuResourceCache;
    struct FoliageCullGpu;

    // One compute cull. Worlds and kinds are the gathered instance buffers. Bounds are local model AABBs.
    // partCount is the opaque draws for that kind. indexCount[kind][part] is IndexCountPerInstance.
    struct FoliageCullDesc
    {
        D3D12_GPU_VIRTUAL_ADDRESS worlds = 0;
        D3D12_GPU_VIRTUAL_ADDRESS kinds  = 0;
        uint32_t                  instanceCount = 0;
        uint32_t                  kindResident[4]{};
        float                     boundsMin[4][3]{};
        float                     boundsMax[4][3]{};
        uint32_t                  kindValid[4]{};
        float                     planes[6][4]{};
        float                     sphere[4]{};
        uint32_t                  useSphere  = 0;
        uint32_t                  shadowView = 0;
        uint32_t                  viewIndex  = 0;
        uint32_t                  partCount[4]{};
        uint32_t                  indexCount[4][8]{};
    };

    namespace Terrain
    {
        class TerrainGrid;
    }

    // Instanced deferred G-buffer and cascade depth for terrain foliage.
    class FoliagePipeline
    {
    public:
        static constexpr UINT kGbConstants = 0;
        static constexpr UINT kGbMaterial  = 1;
        static constexpr UINT kGbWorlds    = 2;
        static constexpr UINT kGbIndices   = 3;

        static constexpr UINT kDepthConstants = 0;
        static constexpr UINT kDepthWorlds    = 1;
        static constexpr UINT kDepthIndices   = 2;
        static constexpr UINT kDepthMaterial  = 3;

        static constexpr uint32_t kCullFrameSlots    = 2;
        static constexpr uint32_t kCullViewsPerFrame = 8;
        static constexpr uint32_t kCullKindCount     = 4;
        static constexpr uint32_t kCullPartsPerKind  = 8;
        static constexpr uint32_t kCullConstantBytes = 432;
        static constexpr uint32_t kGrassKindIndex    = static_cast<uint32_t>(Terrain::FoliageKind::Grass);
        static_assert(kGrassKindIndex == 3u, "FoliageCull.hlsl grass kind");

        FoliagePipeline();
        ~FoliagePipeline();
        FoliagePipeline(const FoliagePipeline&) = delete;
        FoliagePipeline& operator=(const FoliagePipeline&) = delete;
        FoliagePipeline(FoliagePipeline&&) = delete;
        FoliagePipeline& operator=(FoliagePipeline&&) = delete;

        bool create(ID3D12Device* device);
        void destroy();

        // Compute cull resources. Failure leaves the CPU index lists. create() calls this.
        bool createCull(ID3D12Device* device);
        bool cullReady() const;

        // Clears this slot's four counters, compacts the gathered instances, then writes one
        // draw argument per opaque part. StartInstanceLocation stays 0. The kind base is the index-SRV offset.
        bool recordCull(ID3D12GraphicsCommandList* cmd, uint32_t frameSlot, const FoliageCullDesc& desc);

        ID3D12Resource* cullArgumentResource(uint32_t frameSlot) const;
        ID3D12Resource* cullIndexResource(uint32_t frameSlot) const;

        static UINT64 cullArgumentOffset(uint32_t view, uint32_t kind, uint32_t part)
        {
            return (static_cast<UINT64>(view) * kCullKindCount * kCullPartsPerKind + static_cast<UINT64>(kind) * kCullPartsPerKind + part)
                * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        }

        static UINT64 cullIndexByteOffset(uint32_t view, uint32_t kind)
        {
            return (static_cast<UINT64>(view) * kCullKindCount + kind) * Terrain::kMaxFoliageDraw * sizeof(uint32_t);
        }

        bool isValid() const
        {
            return m_gbPso != nullptr && m_gbPsoTwoSided != nullptr && m_depthPso != nullptr && m_depthPsoTwoSided != nullptr
                && m_worldMap != nullptr && m_indexMap != nullptr;
        }

        // editorRecords non-null: that vector, kept only where the tile is resident.
        // null: residentFoliage only (Sandbox). A non-resident tile contributes nothing.
        void setThermal(bool on) { m_thermal = on; }
        void setUltraviolet(bool on) { m_ultraviolet = on; }

        void drawGBuffer(
            ID3D12GraphicsCommandList* cmd,
            Renderer& renderer,
            AssetManager& assets,
            FoliagePrototypes& prototypes,
            const Terrain::TerrainGrid& grid,
            const std::vector<Terrain::FoliageRecord>* editorRecords,
            const Terrain::FoliageDensity& density,
            const Camera3D& camera,
            const Math::Matrix4f& viewProj,
            const Math::Matrix4f& prevViewProj,
            const Frustum3f& frustum);

        void drawDepth(
            ID3D12GraphicsCommandList* cmd,
            Renderer& renderer,
            AssetManager& assets,
            FoliagePrototypes& prototypes,
            const Terrain::TerrainGrid& grid,
            const std::vector<Terrain::FoliageRecord>* editorRecords,
            const Terrain::FoliageDensity& density,
            const Camera3D& camera,
            const Math::Matrix4f& lightViewProj,
            const Frustum3f& casterFrustum);

        // casterSphere wins over casterFrustum when non-null. One view per shadowed light:
        // a point light draws the prepared list once per cube face.
        bool beginDepthView(
            Renderer& renderer,
            AssetManager& assets,
            FoliagePrototypes& prototypes,
            const Terrain::TerrainGrid& grid,
            const std::vector<Terrain::FoliageRecord>* editorRecords,
            const Terrain::FoliageDensity& density,
            const Camera3D& camera,
            const Frustum3f& casterFrustum,
            const Math::Sphere3f* casterSphere);

        void drawPreparedDepth(ID3D12GraphicsCommandList* cmd, const Math::Matrix4f& lightViewProj);

    private:
        bool prepare(
            Renderer& renderer,
            AssetManager& assets,
            FoliagePrototypes& prototypes,
            const Terrain::TerrainGrid& grid,
            const std::vector<Terrain::FoliageRecord>* editorRecords,
            const Terrain::FoliageDensity& density,
            const Camera3D& camera,
            const Frustum3f& frustum,
            const Math::Sphere3f* casterSphere);

        void ensureDrawSet(
            const Terrain::TerrainGrid& grid,
            const std::vector<Terrain::FoliageRecord>* editorRecords,
            const Camera3D& camera);
        void binEditorRecords(const Terrain::TerrainGrid& grid, const std::vector<Terrain::FoliageRecord>& records);
        void uploadWorlds(uint32_t frameSlot);
        void uploadKinds(uint32_t frameSlot);
        bool allocView(uint32_t frameIndex);
        void snapshotKindBounds(FoliagePrototypes& prototypes);
        bool buildCpuIndices(FoliagePrototypes& prototypes);
        void publishCpuIndices(FoliagePrototypes& prototypes);
        FoliageCullDesc makeCullDesc(bool shadow) const;
        bool fillPartArgs(GpuResourceCache& gpu, FoliagePrototypes& prototypes, bool depth, FoliageCullDesc& desc);
        bool tryGpuCull(ID3D12GraphicsCommandList* cmd, GpuResourceCache& gpu, FoliagePrototypes& prototypes, bool depth, bool shadow);

        ComPtr<ID3D12RootSignature> m_gbRoot;
        ComPtr<ID3D12PipelineState> m_gbPso;
        ComPtr<ID3D12PipelineState> m_gbPsoTwoSided;
        ComPtr<ID3D12PipelineState> m_gbThermal;
        ComPtr<ID3D12PipelineState> m_gbThermalTwoSided;
        ComPtr<ID3D12PipelineState> m_gbUv;
        ComPtr<ID3D12PipelineState> m_gbUvTwoSided;
        bool                        m_thermal = false;
        bool                        m_ultraviolet = false;
        ComPtr<ID3D12RootSignature> m_depthRoot;
        ComPtr<ID3D12PipelineState> m_depthPso;
        ComPtr<ID3D12PipelineState> m_depthPsoTwoSided;
        ComPtr<ID3D12Resource>      m_worlds;
        ComPtr<ID3D12Resource>      m_indices;
        uint8_t*                    m_worldMap = nullptr;
        uint8_t*                    m_indexMap = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS   m_worldGpu = 0;
        D3D12_GPU_VIRTUAL_ADDRESS   m_indexGpu = 0;

        std::vector<Math::Matrix4f> m_cpuWorlds;
        std::vector<uint8_t>        m_cpuKinds;
        std::vector<uint32_t>       m_cpuIndex[static_cast<int>(Terrain::FoliageKind::Count)];

        const Terrain::FoliageRecord* m_binnedData  = nullptr;
        size_t                        m_binnedCount = 0;
        int                           m_binnedTilesX = 0;
        int                           m_binnedTilesZ = 0;
        float                         m_binnedTileWorld = 0.0f;
        float                         m_binnedOriginX   = 0.0f;
        float                         m_binnedOriginZ   = 0.0f;
        uint32_t                      m_binnedFrontX    = 0;
        uint32_t                      m_binnedFrontY    = 0;
        uint32_t                      m_binnedBackZ     = 0;
        uint8_t                       m_binnedFrontKind = 0;
        std::vector<std::vector<uint32_t>> m_editorTiles;

        uint32_t m_cpuGen = 1;
        uint32_t m_worldSlotGen[2]{};
        uint32_t m_kindSlotGen[2]{};
        uint32_t m_frameIndex = ~0u;
        uint32_t m_viewCursor = 0;
        bool     m_depthReady = false;
        bool     m_gpuCull    = false;
        bool     m_depthCullPending = false;
        bool     m_cpuUseSphere = false;
        FoliagePrototypes* m_depthPrototypes = nullptr;
        Renderer*          m_depthRenderer   = nullptr;
        Renderer*          m_renderer        = nullptr;
        std::unique_ptr<FoliageCullGpu> m_cull;
        uint32_t           m_kindResident[4]{};
        uint32_t           m_kindValid[4]{};
        float              m_boundsMin[4][3]{};
        float              m_boundsMax[4][3]{};
        Frustum3f          m_cpuFrustum{};
        Math::Sphere3f     m_cpuSphere{};
        uint32_t m_activeSlot = 0;
        uint32_t m_activeView = 0;
        float    m_gatherX    = 0.0f;
        float    m_gatherZ    = 0.0f;
        bool     m_haveGather = false;
        uint64_t m_sourceStamp = 0;
        bool     m_loggedView  = false;
    };

} // namespace Dark
