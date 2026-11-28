#pragma once

#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;

    // Root constants b0 of VisionModes.hlsl. 16-byte aligned to match the HLSL cbuffer.
    struct VisionHeatConstants
    {
        float invViewProj[16];
        float cameraPos[3];
        float pad0;
        float sunDir[3];
        float pad1;
    };
    static_assert(sizeof(VisionHeatConstants) == 24 * sizeof(float), "VisionHeatConstants is 24 root DWORDs");
    static_assert(offsetof(VisionHeatConstants, cameraPos) == 64, "cameraPos follows invViewProj");
    static_assert(offsetof(VisionHeatConstants, sunDir) == 80, "sunDir is 16-byte aligned");

    // The full-screen heat pass: one PSO that blends by the blend factor into HDR, and a per-frame
    // upload of the 256-entry heat LUT (b1).
    class VisionModesPipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootLut       = 1;
        static constexpr UINT kRootGBuffer   = 2; // t0 albedo, t1 attrib, t2 depth, t3 SurfaceTag
        static constexpr UINT kGBufferSrvs   = 4;
        static constexpr UINT kLutEntries    = 256;
        static constexpr UINT kLutBytes      = kLutEntries * sizeof(float); // a multiple of 256
        static constexpr UINT kFrames        = 2;
        static_assert(kLutBytes % D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT == 0, "LUT slots are CBV-aligned");

        VisionModesPipeline() = default;
        ~VisionModesPipeline();
        VisionModesPipeline(const VisionModesPipeline&)            = delete;
        VisionModesPipeline& operator=(const VisionModesPipeline&) = delete;

        bool create(ID3D12Device* device, DXGI_FORMAT hdrFormat);
        void release();
        bool isValid() const
        {
            return m_pso != nullptr && m_lutMapped != nullptr;
        }

        // Copies `count` (<= kLutEntries; the rest is 0) LUT entries into this frame's slot and draws.
        // `gbufferTable` holds kGBufferSrvs SRVs in root order; `hdrRtv` is bound with the renderer's
        // viewport. Restores the blend factor to 1.
        void drawHeat(ID3D12GraphicsCommandList* cmd, const Renderer& renderer, D3D12_GPU_DESCRIPTOR_HANDLE gbufferTable, D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv, const VisionHeatConstants& constants,
                      const float* lut, uint32_t count, float strength);

    private:
        Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
        Microsoft::WRL::ComPtr<ID3D12Resource>      m_lutUpload;
        uint8_t*                                    m_lutMapped = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS                   m_lutGpu    = 0;
    };

} // namespace Dark
