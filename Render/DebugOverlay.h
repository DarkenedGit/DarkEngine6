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

    // Draws debug texture previews (depth, array slices, color, velocity) into screen rectangles (no mesh).
    class DebugOverlay
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrv       = 1;

        bool create(ID3D12Device* device);

        // Each draw samples the one SRV in `srv`, a descriptor-ring table (PassContext::srvTable) on
        // the heap the executor bound, into the screen rectangle (x, y, w, h) of the bound target.
        void draw2D(ID3D12GraphicsCommandList* cmd, D3D12_GPU_DESCRIPTOR_HANDLE srv, LONG x, LONG y, LONG w, LONG h, float contrast, bool invert);
        void drawArray(ID3D12GraphicsCommandList* cmd, D3D12_GPU_DESCRIPTOR_HANDLE srv, LONG x, LONG y, LONG w, LONG h, float slice, float contrast, bool invert);
        void drawColor(ID3D12GraphicsCommandList* cmd, D3D12_GPU_DESCRIPTOR_HANDLE srv, LONG x, LONG y, LONG w, LONG h);
        // Signed RG velocity. contrast scales UV motion into the color range (higher = more sensitive).
        void drawVelocity(ID3D12GraphicsCommandList* cmd, D3D12_GPU_DESCRIPTOR_HANDLE srv, LONG x, LONG y, LONG w, LONG h, float contrast);

        bool isValid() const
        {
            return m_pso2D != nullptr && m_psoArray != nullptr && m_psoColor != nullptr && m_psoVelocity != nullptr;
        }

    private:
        void draw(ID3D12GraphicsCommandList* cmd, ID3D12PipelineState* pso, D3D12_GPU_DESCRIPTOR_HANDLE srv, LONG x, LONG y, LONG w, LONG h, const DebugOverlayConstants& constants);

        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12PipelineState> m_pso2D;
        ComPtr<ID3D12PipelineState> m_psoArray;
        ComPtr<ID3D12PipelineState> m_psoColor;
        ComPtr<ID3D12PipelineState> m_psoVelocity;
    };

} // namespace Dark
