#pragma once

#include "Render/Mesh.h"
#include "Terrain/GrassTypes.h"

#include <cstdint>
#include <wrl/client.h>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;

namespace Dark
{
    class Renderer;

    using Microsoft::WRL::ComPtr;

    struct GrassLodSpan
    {
        const Terrain::GrassBlade* blades    = nullptr;
        uint32_t                   count     = 0;
        uint8_t                    staleMask = 0; // bit per frame slot; GrassField sets both when the LOD changes
    };

    class GrassPipeline
    {
    public:
        static constexpr UINT kRootCb         = 0;
        static constexpr UINT kRootBlades     = 1;
        static constexpr UINT kRootWind       = 2;
        static constexpr UINT kRootBase       = 3;
        static constexpr UINT kRootBaseDwords = 4; // b1 is one 16-byte constant register
        static constexpr UINT kRootIndices    = 4;

        GrassPipeline();
        ~GrassPipeline();

        GrassPipeline(const GrassPipeline&)            = delete;
        GrassPipeline& operator=(const GrassPipeline&) = delete;
        GrassPipeline(GrassPipeline&&)                 = delete;
        GrassPipeline& operator=(GrassPipeline&&)      = delete;

        bool create(Renderer& renderer);
        void destroy();

        bool isValid() const
        {
            return m_valid;
        }

        // Call after beginFrame. Copies a LOD only when its staleMask bit for this frame slot is set.
        // Each entry in tiles is one 8 m tile's full blade span. The caller already dropped tiles outside the frustum.
        // Returns false when that slot was not published; the caller keeps the mask. tileWind may be null when tileCount is 0.
        bool draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const GrassLodSpan lods[4], const Terrain::GrassTileWind* tileWind, uint32_t tileCount,
                  const Terrain::GrassFrameConstants& frame, const Terrain::GrassTileDraw* tiles, uint32_t tileDraws);

    private:
        bool fail(const char* why);

        Renderer* m_renderer = nullptr;
        bool      m_valid    = false;

        ComPtr<ID3D12RootSignature> m_root;
        ComPtr<ID3D12PipelineState> m_pso[4];
        Mesh                        m_mesh[3];

        ComPtr<ID3D12Resource>    m_blades;
        ComPtr<ID3D12Resource>    m_wind;
        ComPtr<ID3D12Resource>    m_cb;
        uint8_t*                  m_bladeMap = nullptr;
        uint8_t*                  m_windMap  = nullptr;
        uint8_t*                  m_cbMap    = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS m_bladeGpu    = 0;
        D3D12_GPU_VIRTUAL_ADDRESS m_windGpu     = 0;
        D3D12_GPU_VIRTUAL_ADDRESS m_cbGpu       = 0;
        ComPtr<ID3D12Resource>    m_identity;
        D3D12_GPU_VIRTUAL_ADDRESS m_identityGpu = 0;
    };

} // namespace Dark
