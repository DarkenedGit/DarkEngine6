#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;

    using Microsoft::WRL::ComPtr;

    struct MotionBlurSettings
    {
        float invViewProj[16]{};
        float prevViewProj[16]{};
        float strength   = 1.0f;
        float maxPixels  = 40.0f;
        bool  readPost   = false; // true: sample TAA/post, write scene HDR
    };

    class MotionBlurPipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrv       = 1;

        MotionBlurPipeline() = default;

        bool create(ID3D12Device* device);
        void draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const MotionBlurSettings& settings) const;

        bool isValid() const { return m_pso != nullptr; }

    private:
        static constexpr UINT kBufferedFrames = 2;
        static constexpr UINT kSrvPerFrame    = 3; // HDR + velocity + depth
        static constexpr UINT kConstantCount  = 36;

        ComPtr<ID3D12RootSignature>  m_rootSignature;
        ComPtr<ID3D12PipelineState>  m_pso;
        ComPtr<ID3D12DescriptorHeap> m_srvHeap;
        D3D12_CPU_DESCRIPTOR_HANDLE  m_cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE  m_gpu{};
        UINT                         m_srvIncr = 0;
    };

} // namespace Dark
