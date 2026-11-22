#pragma once

#include "Render/GraphTargets.h"
#include "Math/Matrix4f.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;
    class Camera3D;

    using Microsoft::WRL::ComPtr;

    struct GtaoSettings
    {
        bool  enabled   = false;
        float radius    = 0.5f;
        float power     = 1.5f;
        float intensity = 1.0f;
    };

    struct GtaoGpuParams
    {
        float invSizeHalf[2];
        float invSizeFull[2];
        float invProj[16];
        float view[16];
        float reprojection[16];
        float radius;
        float power;
        float intensity;
        float thickness;
        float nearZ;
        float farZ;
        float reset;
        float _pad;
        float _pad64[4];
    };
    static_assert(sizeof(GtaoGpuParams) == 64 * sizeof(float), "GtaoGpuParams is 64 floats (256-byte CBV)");

    class GtaoPipeline
    {
    public:
        static constexpr float kThickness     = 1.0f;
        static constexpr UINT  kRootCbv       = 0;
        static constexpr UINT  kRootSrv       = 1;
        static constexpr UINT  kRootVelocity  = 2;

        GtaoPipeline() = default;
        ~GtaoPipeline();
        GtaoPipeline(const GtaoPipeline&)            = delete;
        GtaoPipeline& operator=(const GtaoPipeline&) = delete;

        // Graph targets (render-graph plan Phase 5): AoHalf (ceil half), AoFull and AoCompose are
        // registered output-relative; Renderer::resize recreates them. The temporal history is the
        // graph's GtaoHistory pair (historyDesc), passed to draw. `targets` must outlive this.
        bool create(GraphTargets& targets);
        void release();
        static Rg::TargetDesc historyDesc();
        void draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const Camera3D& camera, const Math::Matrix4f& prevViewProj,
                  const GtaoSettings& settings, const HistoryView& history, bool resetHistory);

        bool isValid() const;
        D3D12_CPU_DESCRIPTOR_HANDLE composeSrvCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE aoFullSrvCpu() const; // FLAG_NONE, overlay tile
        void transitionAoFull(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);
        void clearAoFullIdentity(ID3D12GraphicsCommandList* cmd);

    private:
        static constexpr UINT kSrvPerPass    = 5;
        static constexpr UINT kPassCount     = 3;
        static constexpr UINT kFrameCount    = 2;
        static constexpr UINT kCbBytes       = 256;

        bool createPsos(ID3D12Device* device);
        bool createCbv(ID3D12Device* device);
        bool createTargets(ID3D12Device* device);
        void resetTargets();
        D3D12_GPU_DESCRIPTOR_HANDLE passSrvGpu(UINT frame, UINT pass) const;
        D3D12_CPU_DESCRIPTOR_HANDLE passSrvCpu(UINT frame, UINT pass) const;
        void copySrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE dst, D3D12_CPU_DESCRIPTOR_HANDLE src) const;
        void fillParams(GtaoGpuParams& out, const Camera3D& camera, const Math::Matrix4f& prevViewProj, const GtaoSettings& settings, bool resetHistory,
                        uint32_t frameIndex) const;
        void drawFullscreen(ID3D12GraphicsCommandList* cmd, ID3D12PipelineState* pso, UINT nRtv, const D3D12_CPU_DESCRIPTOR_HANDLE* rtvs, uint32_t w, uint32_t h,
                            D3D12_GPU_DESCRIPTOR_HANDLE tableGpu) const;

        GraphTargets*                m_targets = nullptr;
        ComPtr<ID3D12RootSignature>  m_rootSignature;
        ComPtr<ID3D12PipelineState>  m_psoGtao;
        ComPtr<ID3D12PipelineState>  m_psoUpsample;
        ComPtr<ID3D12PipelineState>  m_psoCompose;
        ComPtr<ID3D12DescriptorHeap> m_srvHeap;
        ComPtr<ID3D12Resource>       m_cbUpload;
        uint8_t*                     m_cbMapped = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS    m_cbGpu    = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE  m_srvCpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE  m_srvGpu{};
        UINT                         m_srvIncr = 0;
        GraphTarget                  m_aoHalf;
        GraphTarget                  m_aoFull;
        GraphTarget                  m_aoCompose;
        bool                         m_loggedSkip = false;
    };

} // namespace Dark
