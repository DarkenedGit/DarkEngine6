#pragma once

#include "Render/DescriptorRing.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

// The descriptor ring (render-graph plan §3.9, Phase 6): one shader-visible CBV/SRV/UAV heap for
// the frame, split into a persistent region and fence-retired per-frame tables (DescriptorRing).
// Converted pipelines bind it once per list and copy their views into a frame table each draw
// instead of owning a shader-visible heap; Renderer owns it and retires it with the frame fence.
// Sources for frameTable() must be CPU-only (non-shader-visible) descriptors, as CopyDescriptors
// requires.

namespace Dark
{

    class DescriptorRingHeap
    {
    public:
        static constexpr uint32_t kDefaultCapacity   = 262144; // ~8 MB at 32 B per descriptor (Tier 2+ allows 1M)
        static constexpr uint32_t kDefaultPersistent = 65536;

        bool init(ID3D12Device* device, uint32_t capacity = kDefaultCapacity, uint32_t persistent = kDefaultPersistent);
        void shutdown();
        bool valid() const
        {
            return m_heap != nullptr;
        }

        ID3D12DescriptorHeap*       heap() const
        {
            return m_heap.Get();
        }
        D3D12_CPU_DESCRIPTOR_HANDLE cpu(uint32_t offset) const;
        D3D12_GPU_DESCRIPTOR_HANDLE gpu(uint32_t offset) const;

        // Binds the ring as the list's CBV/SRV/UAV heap (no sampler heap).
        void bind(ID3D12GraphicsCommandList* cmd) const;

        // Copies `count` CPU descriptors into one contiguous frame table and returns its GPU handle
        // (ptr 0 when the frame region is full; logged once).
        D3D12_GPU_DESCRIPTOR_HANDLE frameTable(const D3D12_CPU_DESCRIPTOR_HANDLE* sources, uint32_t count);

        // Persistent tables (views that live as long as their resource).
        uint32_t allocatePersistent(uint32_t count)
        {
            return m_ring.allocatePersistent(count);
        }
        void freePersistent(uint32_t offset, uint32_t count)
        {
            m_ring.freePersistent(offset, count);
        }

        // Frame bookkeeping: endFrame after the frame's list is signalled with `fence`; retire at
        // the start of a frame with the fence's completed value.
        void endFrame(uint64_t fence)
        {
            m_ring.endFrame(fence);
        }
        void retire(uint64_t completedFence)
        {
            m_ring.retire(completedFence);
        }

        const DescriptorRing& ring() const
        {
            return m_ring;
        }

    private:
        Microsoft::WRL::ComPtr<ID3D12Device>         m_device;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
        D3D12_CPU_DESCRIPTOR_HANDLE                  m_cpuStart{};
        D3D12_GPU_DESCRIPTOR_HANDLE                  m_gpuStart{};
        uint32_t                                     m_increment = 0;
        DescriptorRing                               m_ring;
        bool                                         m_loggedFull = false;
    };

} // namespace Dark
