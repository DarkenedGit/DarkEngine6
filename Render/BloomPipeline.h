#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;

    using Microsoft::WRL::ComPtr;

    class BloomPipeline
    {
    public:
        static constexpr UINT  kRootConstants   = 0;
        static constexpr UINT  kRootSrv         = 1;
        static constexpr float kDefaultStrength = 0.06f;
        static constexpr float kThreshold       = 1.0f;
        static constexpr float kKnee            = 0.5f;
        static constexpr UINT  kDownCount       = 5;
        static constexpr UINT  kMipCount        = 6; // extract + down[0..4]

        BloomPipeline() = default;

        bool create(ID3D12Device* device, uint32_t width, uint32_t height);
        bool resize(ID3D12Device* device, uint32_t width, uint32_t height);
        void draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, float strength) const;

        bool isValid() const { return m_psoExtract != nullptr && m_mips[0].res != nullptr; }

    private:
        static constexpr UINT kHdrSlots      = 2;
        static constexpr UINT kConstantCount = 8;
        static constexpr UINT kSrvCount      = kHdrSlots + kMipCount;

        struct Mip
        {
            ComPtr<ID3D12Resource>        res;
            uint32_t                      w     = 0;
            uint32_t                      h     = 0;
            mutable D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        };

        bool createPsos(ID3D12Device* device);
        bool createTargets(ID3D12Device* device, uint32_t width, uint32_t height);
        void resetTargets();
        void transitionMip(ID3D12GraphicsCommandList* cmd, UINT index, D3D12_RESOURCE_STATES after) const;
        D3D12_CPU_DESCRIPTOR_HANDLE rtvCpu(UINT mip) const;
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu(UINT slot) const;
        void drawFullscreen(ID3D12GraphicsCommandList* cmd, ID3D12PipelineState* pso, D3D12_GPU_DESCRIPTOR_HANDLE srcGpu,
                            D3D12_CPU_DESCRIPTOR_HANDLE rtv, uint32_t dstW, uint32_t dstH, uint32_t srcW, uint32_t srcH, float strength) const;

        ComPtr<ID3D12RootSignature>  m_rootSignature;
        ComPtr<ID3D12PipelineState>  m_psoExtract;
        ComPtr<ID3D12PipelineState>  m_psoDownsample;
        ComPtr<ID3D12PipelineState>  m_psoUpsample;
        ComPtr<ID3D12PipelineState>  m_psoComposite;
        ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
        ComPtr<ID3D12DescriptorHeap> m_srvHeap;
        D3D12_CPU_DESCRIPTOR_HANDLE  m_rtvCpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_srvCpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE  m_srvGpu{};
        UINT                         m_rtvIncr = 0;
        UINT                         m_srvIncr = 0;
        uint32_t                     m_width   = 0;
        uint32_t                     m_height  = 0;
        Mip                          m_mips[kMipCount]{};
        mutable bool                 m_loggedSkip = false;
    };

} // namespace Dark
