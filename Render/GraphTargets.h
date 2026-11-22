#pragma once

#include "Render/D3D12TargetBackend.h"
#include "Render/GpuAllocator.h"
#include "Render/RenderTargetPool.h"
#include "Render/TemporalHistory.h"

#include <cstdint>
#include <d3d12.h>

namespace Dark
{

    // One graph-registered target as an opaque pass sees it: the id plus the D3D12 state the owner
    // tracks (Phase 6 moves barriers into the graph). A recreated target (resize) starts again in
    // its desc's initial state; GraphTargets::refresh() notices that through the generation.
    struct GraphTarget
    {
        Rg::TargetId          id;
        D3D12_RESOURCE_STATES initial    = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES state      = D3D12_RESOURCE_STATE_COMMON;
        uint64_t              generation = 0;
    };

    // A history pair as a pass binds it: read prev (when prevValid), write curr. Both targets rest in
    // PIXEL_SHADER_RESOURCE between frames; the writing pass moves curr to RENDER_TARGET and back.
    struct HistoryView
    {
        ID3D12Resource*             curr = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE currRtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE prevSrv{};
        bool                        prevValid = false;

        bool valid() const
        {
            return curr != nullptr && currRtv.ptr != 0 && prevSrv.ptr != 0;
        }
    };

    // The render graph's persistent targets (render-graph plan Phase 5): D3D12MA allocator, the
    // D3D12 backend (graph-owned CPU RTV/DSV/SRV views, fence-based retirement) and the registry.
    // Owned by Renderer; tests can own one directly. Owners register output-relative targets once;
    // Renderer::resize recreates them after its single GPU drain and bumps their generation.
    // Must outlive every owner holding a TargetId (Application destroys Renderer last).
    class GraphTargets
    {
    public:
        GraphTargets();
        ~GraphTargets();
        GraphTargets(const GraphTargets&)            = delete;
        GraphTargets& operator=(const GraphTargets&) = delete;

        bool init(ID3D12Device* device, uint32_t width, uint32_t height);
        void shutdown(); // GPU must be idle
        bool valid() const
        {
            return m_allocator.valid();
        }
        ID3D12Device* device() const
        {
            return m_allocator.device();
        }

        Rg::RenderTargetPool& pool()
        {
            return m_pool;
        }
        const Rg::RenderTargetPool& pool() const
        {
            return m_pool;
        }
        GpuAllocator& allocator()
        {
            return m_allocator;
        }
        D3D12TargetBackend& backend()
        {
            return m_backend;
        }

        // Retired targets are released once `fence` (the fence value that will cover work recorded
        // from now on) has completed; collect() frees what `completed` covers.
        void retireAfter(uint64_t fence)
        {
            m_backend.retireAfter(fence);
        }
        void collect(uint64_t completed)
        {
            m_backend.collect(completed);
        }

        // Called by Renderer::resize after its waitForGpu (and by tests). Transactional.
        bool resizeAfterDrain(uint32_t width, uint32_t height);

        Rg::TargetId                registerTexture(const char* name, const Rg::TargetDesc& desc);
        void                        unregisterTexture(Rg::TargetId& id); // resets id
        ID3D12Resource*             resource(Rg::TargetId id) const;
        D3D12_CPU_DESCRIPTOR_HANDLE srv(Rg::TargetId id) const;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv(Rg::TargetId id) const;
        D3D12_CPU_DESCRIPTOR_HANDLE dsv(Rg::TargetId id) const;
        D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDsv(Rg::TargetId id) const;
        uint32_t                    width(Rg::TargetId id) const;
        uint32_t                    height(Rg::TargetId id) const;
        uint64_t                    generation(Rg::TargetId id) const;

        // GraphTarget helpers for owners that record their own barriers.
        GraphTarget registerTarget(const char* name, const Rg::TargetDesc& desc);
        void        unregisterTarget(GraphTarget& target);
        bool        refresh(GraphTarget& target) const; // true when recreated since the last refresh
        void        transition(ID3D12GraphicsCommandList* cmd, GraphTarget& target, D3D12_RESOURCE_STATES after) const;
        HistoryView historyView(const Rg::HistoryTargets& history) const;

        void logStats(const char* when) const;

    private:
        GpuAllocator         m_allocator;
        D3D12TargetBackend   m_backend;
        Rg::RenderTargetPool m_pool;
    };

    // Plain color render target registered by the opaque owners (SRV + RTV, optimized clear).
    Rg::TargetDesc colorTargetDesc(DXGI_FORMAT format, Rg::TargetSize size, uint32_t divisor, D3D12_RESOURCE_STATES initial, const float clear[4]);
    // Output-sized copy destination sampled as an SRV (no RTV, no clear value).
    Rg::TargetDesc copyTargetDesc(DXGI_FORMAT format, D3D12_RESOURCE_STATES initial);

} // namespace Dark
