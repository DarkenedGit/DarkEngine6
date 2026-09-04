#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;

    using Microsoft::WRL::ComPtr;

    struct TaaSettings
    {
        float invViewProj[16]{};
        float prevViewProj[16]{};
        float blend = 0.1f; // current-frame weight
        bool  reset = false;
    };

    class TaaPipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrv       = 1;

        TaaPipeline() = default;

        bool create(ID3D12Device* device);
        void draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const TaaSettings& settings) const;

        bool isValid() const { return m_pso != nullptr; }

    private:
        static constexpr UINT kBufferedFrames = 2;
        static constexpr UINT kSrvPerFrame    = 4; // current + history + velocity + depth
        static constexpr UINT kConstantCount  = 36;

        ComPtr<ID3D12RootSignature>  m_rootSignature;
        ComPtr<ID3D12PipelineState>  m_pso;
        ComPtr<ID3D12DescriptorHeap> m_srvHeap;
        D3D12_CPU_DESCRIPTOR_HANDLE  m_cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE  m_gpu{};
        UINT                         m_srvIncr = 0;
    };

} // namespace Dark
