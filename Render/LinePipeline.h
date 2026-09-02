#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // Unlit line list: worldViewProj + solid color.
    struct LineFrameConstants
    {
        float worldViewProj[16];
        float color[4];
    };

    static_assert(sizeof(LineFrameConstants) == 20 * sizeof(float), "line root constant size");

    class LinePipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;

        LinePipeline() = default;

        bool create(ID3D12Device* device, DXGI_FORMAT colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM);
        void bind(ID3D12GraphicsCommandList* cmd) const;
        void setConstants(ID3D12GraphicsCommandList* cmd, const LineFrameConstants& constants) const;

        bool isValid() const
        {
            return m_pso != nullptr;
        }

    private:
        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12PipelineState> m_pso;
    };

} // namespace Dark
