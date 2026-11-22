#pragma once

#include "Render/DecalTypes.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Mesh;

    using Microsoft::WRL::ComPtr;

    struct DecalPassConstants
    {
        float invViewProj[16]; // inverse of the view-proj that wrote depth (jitter included)
        float viewProj[16];
    };
    static_assert(sizeof(DecalPassConstants) == 32 * sizeof(float), "decal pass CB");

    // Registers: depth t0, albedo/attrib R32_UINT UAVs u0..u1, a definition's albedo + normal t1..t2.
    struct DecalTables
    {
        D3D12_GPU_DESCRIPTOR_HANDLE depth{};
        D3D12_GPU_DESCRIPTOR_HANDLE uavs{};
        D3D12_GPU_DESCRIPTOR_HANDLE textures[static_cast<uint32_t>(DecalDefId::Count)]{};
    };

    // 2 CBV + 1 depth table + 1 texture table + 1 UAV table + 2 instance SRV = 7.
    static constexpr UINT kDecalRootDwords = 2 + 1 + 1 + 1 + 2;
    static_assert(kDecalRootDwords == 7, "decal root signature DWORD budget");

    class DecalPipeline
    {
    public:
        static constexpr UINT kRootCbv       = 0;
        static constexpr UINT kRootDepth     = 1;
        static constexpr UINT kRootTextures  = 2;
        static constexpr UINT kRootUav       = 3;
        static constexpr UINT kRootInstances = 4;

        DecalPipeline() = default;

        bool create(ID3D12Device* device);
        bool isValid() const;

        // R32_UINT views in the FLAG_NONE heap. Recreate when the resource pointers change.
        bool syncTargets(ID3D12Device* device, ID3D12Resource* albedo, ID3D12Resource* attrib);
        // The CPU UAVs syncTargets made (0 until then), for the pass's ring table.
        D3D12_CPU_DESCRIPTOR_HANDLE albedoUavCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE attribUavCpu() const;

        void uploadPass(uint32_t frameIndex, const DecalPassConstants& cb);

        // The ring heap is bound by the graph executor; these only set the root signature and tables.
        void drawOutside(ID3D12GraphicsCommandList* cmd, const DecalTables& tables, const Mesh& cube, D3D12_GPU_VIRTUAL_ADDRESS instances, uint32_t count, DecalDefId id) const;
        void drawInside(ID3D12GraphicsCommandList* cmd, const DecalTables& tables, D3D12_GPU_VIRTUAL_ADDRESS instances, uint32_t count, DecalDefId id) const;

        void uavBarrier(ID3D12GraphicsCommandList* cmd) const;

    private:
        static constexpr UINT kFrameCount = 2;
        static constexpr UINT kCbBytes    = 256;
        static constexpr UINT kUavCount   = 2;

        bool                        bind(ID3D12GraphicsCommandList* cmd, const DecalTables& tables, D3D12_GPU_VIRTUAL_ADDRESS instances, DecalDefId id) const;
        D3D12_CPU_DESCRIPTOR_HANDLE cpuUav(UINT slot) const;
        void reset();

        ComPtr<ID3D12RootSignature>  m_rootSignature;
        ComPtr<ID3D12PipelineState>  m_psoOutside;
        ComPtr<ID3D12PipelineState>  m_psoInside;
        ComPtr<ID3D12DescriptorHeap> m_uavHeap;
        ComPtr<ID3D12Resource>       m_cbUpload;
        uint8_t*                     m_cbMapped = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS    m_cbGpu    = 0;
        UINT                         m_srvIncr  = 0;
        uint32_t                     m_frameSlot = 0;
        ID3D12Resource*              m_albedoRes = nullptr;
        ID3D12Resource*              m_attribRes = nullptr;
        bool                         m_uavsReady = false;
    };

} // namespace Dark
