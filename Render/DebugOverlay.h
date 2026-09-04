#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    struct DebugOverlayConstants
    {
        float slice;
        float contrast;
        float invert;
        float pad;
    };

    static_assert(sizeof(DebugOverlayConstants) == 4 * sizeof(float), "overlay constants");

    // Draws a grayscale depth preview into a screen rectangle (no mesh).
    class DebugOverlay
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrv       = 1;

        bool create(ID3D12Device* device);

        // Pin descriptor slots to this Renderer::frameIndex() so in-flight frames
        // do not overwrite each other's shader-visible SRVs.
        void beginFrame(uint32_t frameIndex);

        void draw2D(
            ID3D12GraphicsCommandList* cmd,
            ID3D12Device* device,
            D3D12_CPU_DESCRIPTOR_HANDLE srcSrv,
            LONG x,
            LONG y,
            LONG w,
            LONG h,
            float contrast,
            bool invert);

        void drawArray(
            ID3D12GraphicsCommandList* cmd,
            ID3D12Device* device,
            D3D12_CPU_DESCRIPTOR_HANDLE srcSrv,
            LONG x,
            LONG y,
            LONG w,
            LONG h,
            float slice,
            float contrast,
            bool invert);

        void drawColor(
            ID3D12GraphicsCommandList* cmd,
            ID3D12Device* device,
            D3D12_CPU_DESCRIPTOR_HANDLE srcSrv,
            LONG x,
            LONG y,
            LONG w,
            LONG h);

        // Signed RG velocity. contrast scales UV motion into the color range (higher = more sensitive).
        void drawVelocity(
            ID3D12GraphicsCommandList* cmd,
            ID3D12Device* device,
            D3D12_CPU_DESCRIPTOR_HANDLE srcSrv,
            LONG x,
            LONG y,
            LONG w,
            LONG h,
            float contrast);

        bool isValid() const
        {
            return m_pso2D != nullptr && m_psoArray != nullptr && m_psoColor != nullptr && m_psoVelocity != nullptr;
        }

    private:
        void draw(
            ID3D12GraphicsCommandList* cmd,
            ID3D12Device* device,
            ID3D12PipelineState* pso,
            D3D12_CPU_DESCRIPTOR_HANDLE srcSrv,
            LONG x,
            LONG y,
            LONG w,
            LONG h,
            const DebugOverlayConstants& constants);

        static constexpr UINT kDrawsPerFrame  = 8;
        static constexpr UINT kBufferedFrames = 2;
        static constexpr UINT kHeapSize       = kDrawsPerFrame * kBufferedFrames;

        ComPtr<ID3D12RootSignature>  m_rootSignature;
        ComPtr<ID3D12PipelineState>  m_pso2D;
        ComPtr<ID3D12PipelineState>  m_psoArray;
        ComPtr<ID3D12PipelineState>  m_psoColor;
        ComPtr<ID3D12PipelineState>  m_psoVelocity;
        ComPtr<ID3D12DescriptorHeap> m_srvHeap;
        D3D12_GPU_DESCRIPTOR_HANDLE  m_gpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_cpu{};
        UINT                         m_srvIncr = 0;
        UINT                         m_frame   = 0;
        UINT                         m_draw    = 0;
    };

} // namespace Dark
