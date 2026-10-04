#pragma once

#include "Math/Matrix4f.h"
#include "Terrain/FoliageFile.h"

#include <cstdint>
#include <d3d12.h>
#include <vector>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    class Renderer;
    class AssetManager;
    class Camera3D;
    class Frustum3f;
    class FoliagePrototypes;

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

        FoliagePipeline() = default;
        ~FoliagePipeline();
        FoliagePipeline(const FoliagePipeline&) = delete;
        FoliagePipeline& operator=(const FoliagePipeline&) = delete;
        FoliagePipeline(FoliagePipeline&&) = delete;
        FoliagePipeline& operator=(FoliagePipeline&&) = delete;

        bool create(ID3D12Device* device);
        void destroy();

        bool isValid() const { return m_gbPso != nullptr && m_depthPso != nullptr && m_worldMap != nullptr && m_indexMap != nullptr; }

        // editorRecords non-null: that vector, kept only where the tile is resident.
        // null: residentFoliage only (Sandbox). A non-resident tile contributes nothing.
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

    private:
        bool prepare(
            Renderer& renderer,
            AssetManager& assets,
            FoliagePrototypes& prototypes,
            const Terrain::TerrainGrid& grid,
            const std::vector<Terrain::FoliageRecord>* editorRecords,
            const Terrain::FoliageDensity& density,
            const Camera3D& camera,
            const Frustum3f& frustum);

        void ensureDrawSet(
            const Terrain::TerrainGrid& grid,
            const std::vector<Terrain::FoliageRecord>* editorRecords,
            const Camera3D& camera);
        void binEditorRecords(const Terrain::TerrainGrid& grid, const std::vector<Terrain::FoliageRecord>& records);
        void uploadWorlds(uint32_t frameSlot);
        bool allocView(uint32_t frameIndex);

        ComPtr<ID3D12RootSignature> m_gbRoot;
        ComPtr<ID3D12PipelineState> m_gbPso;
        ComPtr<ID3D12RootSignature> m_depthRoot;
        ComPtr<ID3D12PipelineState> m_depthPso;
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
        uint64_t                      m_binnedEnds      = 0;
        std::vector<std::vector<uint32_t>> m_editorTiles;

        uint32_t m_cpuGen = 1;
        uint32_t m_worldSlotGen[2]{};
        uint32_t m_frameIndex = ~0u;
        uint32_t m_viewCursor = 0;
        uint32_t m_activeSlot = 0;
        uint32_t m_activeView = 0;
        float    m_gatherX    = 0.0f;
        float    m_gatherZ    = 0.0f;
        bool     m_haveGather = false;
        uint64_t m_sourceStamp = 0;
        bool     m_loggedView  = false;
    };

} // namespace Dark
