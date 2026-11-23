#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // Window UV rect (x0, y0, x1, y1), origin top-left, plus opacity. Eight root constants.
    struct MoviePresentConstants
    {
        float rect[4];
        float opacity;
        float pad[3];
    };

    static_assert(sizeof(MoviePresentConstants) == 8 * sizeof(float), "movie present root constant size");

    // Opaque fullscreen present of one B8G8R8A8_UNORM SRV. The caller supplies the rect.
    class MoviePresentPipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootFrameSrv  = 1;
        static constexpr UINT kConstantCount = 8;

        MoviePresentPipeline() = default;

        bool create(ID3D12Device* device);

        // shaderHeap is bound for this draw only. frameSrv must live in that heap.
        void draw(ID3D12GraphicsCommandList* cmd, ID3D12DescriptorHeap* shaderHeap, D3D12_GPU_DESCRIPTOR_HANDLE frameSrv, const float rect[4], float opacity) const;

        bool isValid() const
        {
            return m_pso != nullptr;
        }

    private:
        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12PipelineState> m_pso;
    };

} // namespace Dark
