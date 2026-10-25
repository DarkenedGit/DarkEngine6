#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;

    using Microsoft::WRL::ComPtr;

    // Fullscreen Beer-Lambert on scene HDR. amount 0 skips the draw.
    struct UnderwaterFrame
    {
        float invViewProj[16]{};
        float cameraPos[3]{};
        float waterLevel = 0.0f;
        float amount     = 0.0f;
    };

    class UnderwaterPipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrv       = 1;

        UnderwaterPipeline() = default;

        bool create(ID3D12Device* device);
        void draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const UnderwaterFrame& frame);

        bool isValid() const { return m_pso != nullptr && m_rootSignature != nullptr; }

    private:
        static constexpr UINT kBufferedFrames = 2;
        static constexpr UINT kSrvPerFrame    = 2; // HDR copy + depth
        static constexpr UINT kConstantCount  = 21;

        bool ensureCopy(Renderer& renderer);
        void transitionCopy(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);

        ComPtr<ID3D12RootSignature>  m_rootSignature;
        ComPtr<ID3D12PipelineState>  m_pso;
        ComPtr<ID3D12DescriptorHeap> m_srvHeap;
        ComPtr<ID3D12DescriptorHeap> m_copySrvHeap;
        ComPtr<ID3D12Resource>       m_copy;
        D3D12_CPU_DESCRIPTOR_HANDLE  m_cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE  m_gpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_copyCpu{};
        UINT                         m_srvIncr = 0;
        D3D12_RESOURCE_STATES        m_copyState = D3D12_RESOURCE_STATE_COMMON;
        uint32_t                     m_copyW = 0;
        uint32_t                     m_copyH = 0;
    };

} // namespace Dark
