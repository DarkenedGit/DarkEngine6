#pragma once

#include "Render/GpuAllocator.h"
#include "Render/RenderGraph.h"

#include <cstdint>
#include <d3d12.h>
#include <memory>

namespace Dark
{

    // Transient heap policy (render-graph plan §3.8): the first block is 256 MiB, it grows in 64 MiB
    // steps and never past 768 MiB. A plan that needs more falls back to one committed texture per
    // transient. A one-time warning fires above 85% of the local memory budget.
    inline constexpr uint64_t kTransientFirstBlock        = 256ull << 20;
    inline constexpr uint64_t kTransientGrowth            = 64ull << 20;
    inline constexpr uint64_t kTransientCap               = 768ull << 20;
    inline constexpr uint32_t kTransientBudgetWarnPercent = 85;
    // Placed transients per plan (one RTV, two DSVs and one SRV slot each).
    inline constexpr uint32_t kMaxTransients = 64;

    // Heap capacity for a plan needing `needed` bytes when `current` is allocated: unchanged while it
    // fits (no shrinking, so toggling a feature does not reallocate), otherwise the first block grown
    // in whole steps; 0 when `needed` is past the cap.
    constexpr uint64_t transientCapacity(uint64_t needed, uint64_t current)
    {
        if (needed > kTransientCap)
            return 0;
        if (needed <= current)
            return current;
        uint64_t c = kTransientFirstBlock;
        while (c < needed)
            c += kTransientGrowth;
        return c;
    }

    constexpr bool transientBudgetExceeded(uint64_t usage, uint64_t budget)
    {
        return budget > 0 && usage * 100 > budget * kTransientBudgetWarnPercent; // bytes * 100 stays far below 2^64
    }

    // The D3D12 desc of a transient: the graph's format (typeless when a depth target is also
    // sampled), flags from the usage bits. Width 0 when the desc cannot be a transient.
    D3D12_RESOURCE_DESC transientResourceDesc(uint32_t width, uint32_t height, const Rg::TextureDesc& desc, uint8_t usage);
    // View formats of a transient: SRV/RTV use viewFormat (else the format); a DSV uses the depth
    // format behind a typeless one. DXGI_FORMAT_UNKNOWN when there is none.
    DXGI_FORMAT transientSrvFormat(const Rg::TextureDesc& desc);
    DXGI_FORMAT transientDsvFormat(const Rg::TextureDesc& desc);

    // Places the graph's transient textures (render-graph plan Phase 9, §3.8). After every compile it
    // gives each live transient of the plan a resource placed at the plan's heap offset in one block
    // of placement memory, plus CPU views. The placement is rebuilt only when the plan or its heap
    // size changes; frames run in order on the graphics queue, so one set serves every frame slot.
    // Replaced resources and memory are released behind the fence (retireAfter/collect).
    // A frame with no transients allocates nothing.
    class TransientHeap
    {
    public:
        TransientHeap();
        ~TransientHeap();
        TransientHeap(const TransientHeap&)            = delete;
        TransientHeap& operator=(const TransientHeap&) = delete;

        bool init(GpuAllocator& allocator);
        void shutdown(); // GPU must be idle

        // Rg::TransientSizeFn over GetResourceAllocationInfo; `user` is the TransientHeap.
        static Rg::TransientSize sizeTransient(void* user, uint32_t width, uint32_t height, const Rg::TextureDesc& desc, uint8_t usage);

        // After compile, before execute. False (logged once) when a live transient could not be
        // placed; the frame must then not execute.
        bool realize(Rg::RenderGraph& graph);

        void retireAfter(uint64_t fence);
        void collect(uint64_t completedFence);

        uint64_t capacity() const;          // bytes of placement memory held (0 = none)
        uint32_t placedCount() const;       // transients in the current set
        uint32_t rebuilds() const;          // placements built since init
        bool     committedFallback() const; // the current set is committed textures (plan past the cap)
        uint32_t retiredCount() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace Dark
