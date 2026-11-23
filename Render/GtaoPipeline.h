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

    // GTAO's own targets, imported into the frame graph each frame (render-graph plan Phase 6 part 3).
    enum class GtaoTarget : uint8_t
    {
        Half,    // produce output (ceil half res)
        Full,    // upsample + temporal output
        Compose, // authored MRT3 * AO, read by lighting
    };

    class GtaoPipeline
    {
    public:
        static constexpr float kThickness     = 1.0f;
        static constexpr UINT  kRootCbv       = 0;
        static constexpr UINT  kRootSrv       = 1;
        static constexpr UINT  kRootVelocity  = 2;
        static constexpr UINT  kSrvCount      = 5; // every step's ring table: t0..t3, then t4 (velocity root table)

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

        // The three steps as declared graph passes (render-graph plan Phase 6 part 3): each binds one
        // descriptor-ring table of kSrvCount SRVs and records no barrier.
        //   produce:  depth, attrib, attrib, attrib, depth          -> Half
        //   upsample: Half, depth, attrib, history prev, velocity   -> Full + history curr (MRT)
        //   compose:  authored MRT3, Full, Full, Full, Full         -> Compose
        // uploadParams writes this frame's constants (call it from the first step's execute: the
        // camera is jittered by then) and returns their address for every step.
        D3D12_GPU_VIRTUAL_ADDRESS uploadParams(const Camera3D& camera, const Math::Matrix4f& prevViewProj, const GtaoSettings& settings, bool resetHistory, uint32_t frameIndex);
        D3D12_GPU_VIRTUAL_ADDRESS paramsAddress(uint32_t frameIndex) const; // what uploadParams returned this frame
        void                      drawProduce(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS params, D3D12_GPU_DESCRIPTOR_HANDLE table) const;
        void                      drawUpsample(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS params, D3D12_GPU_DESCRIPTOR_HANDLE table, D3D12_CPU_DESCRIPTOR_HANDLE historyRtv) const;
        void                      drawCompose(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS params, D3D12_GPU_DESCRIPTOR_HANDLE table) const;

        ID3D12Resource*             resource(GtaoTarget target) const;
        D3D12_CPU_DESCRIPTOR_HANDLE srv(GtaoTarget target) const;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv(GtaoTarget target) const;
        static const char*          name(GtaoTarget target); // the registered target's name

        bool isValid() const;
        D3D12_CPU_DESCRIPTOR_HANDLE composeSrvCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE aoFullSrvCpu() const; // FLAG_NONE

    private:
        static constexpr UINT kFrameCount    = 2;
        static constexpr UINT kCbBytes       = 256;

        bool createPsos(ID3D12Device* device);
        bool createCbv(ID3D12Device* device);
        bool               createTargets();
        void resetTargets();
        const GraphTarget& target(GtaoTarget which) const;
        void fillParams(GtaoGpuParams& out, const Camera3D& camera, const Math::Matrix4f& prevViewProj, const GtaoSettings& settings, bool resetHistory,
                        uint32_t frameIndex) const;
        void drawFullscreen(ID3D12GraphicsCommandList* cmd, ID3D12PipelineState* pso, UINT nRtv, const D3D12_CPU_DESCRIPTOR_HANDLE* rtvs, uint32_t w, uint32_t h, D3D12_GPU_VIRTUAL_ADDRESS params,
                            D3D12_GPU_DESCRIPTOR_HANDLE tableGpu) const;

        GraphTargets*                m_targets = nullptr;
        ComPtr<ID3D12RootSignature>  m_rootSignature;
        ComPtr<ID3D12PipelineState>  m_psoGtao;
        ComPtr<ID3D12PipelineState>  m_psoUpsample;
        ComPtr<ID3D12PipelineState>  m_psoCompose;
        ComPtr<ID3D12Resource>       m_cbUpload;
        uint8_t*                     m_cbMapped = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS    m_cbGpu    = 0;
        UINT                         m_srvIncr  = 0; // the ring's (CBV/SRV/UAV) increment: t4 is table + 4
        GraphTarget                  m_aoHalf;
        GraphTarget                  m_aoFull;
        GraphTarget                  m_aoCompose;
    };

} // namespace Dark
