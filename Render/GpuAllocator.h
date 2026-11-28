#pragma once

#include <cstdint>
#include <d3d12.h>
#include <unknwn.h>
#include <wrl/client.h>

struct IDXGIAdapter;

// D3D12 Memory Allocator wrapper (render-graph plan §3.15, Phase 5). Persistent textures and buffers
// are created through D3D12MA (committed or placed, D3D12MA decides) and every allocation is named
// so PIX memory captures stay readable. D3D12MA types stay in GpuAllocator.cpp.

namespace Dark
{

    // One D3D12MA allocation and its resource. Move-only. Dropping it releases the allocation, so
    // anything the GPU may still read must go through a deferred release first (RenderTargetPool,
    // Renderer::deferRelease). It keeps the allocator alive, so destruction order does not matter.
    class GpuResource
    {
    public:
        GpuResource() = default;
        ~GpuResource();
        GpuResource(GpuResource&& other) noexcept;
        GpuResource& operator=(GpuResource&& other) noexcept;
        GpuResource(const GpuResource&)            = delete;
        GpuResource& operator=(const GpuResource&) = delete;

        ID3D12Resource* get() const
        {
            return m_resource.Get();
        }
        explicit operator bool() const
        {
            return m_resource != nullptr;
        }
        uint64_t bytes() const
        {
            return m_bytes;
        }
        // Allocation (D3D12MA::Allocation as IUnknown, or nullptr when empty).
        IUnknown* allocation() const
        {
            return m_allocation.Get();
        }
        void reset();

    private:
        friend class GpuAllocator;
        // Released in reverse order: resource, allocation, its custom pool, then the allocator reference.
        Microsoft::WRL::ComPtr<IUnknown>       m_allocator;
        Microsoft::WRL::ComPtr<IUnknown>       m_pool; // D3D12MA::Pool of placement memory, or nullptr
        Microsoft::WRL::ComPtr<IUnknown>       m_allocation;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_resource;
        uint64_t                               m_bytes = 0;
    };

    struct GpuMemoryStats
    {
        uint32_t allocations     = 0; // D3D12MA allocations (all heap types)
        uint32_t blocks          = 0; // ID3D12Heap blocks + committed resources
        uint64_t allocationBytes = 0;
        uint64_t blockBytes      = 0;
        uint64_t localUsage      = 0; // DXGI local segment usage / budget (QueryVideoMemoryInfo)
        uint64_t localBudget     = 0;
    };

    class GpuAllocator
    {
    public:
        GpuAllocator() = default;
        ~GpuAllocator();
        GpuAllocator(const GpuAllocator&)            = delete;
        GpuAllocator& operator=(const GpuAllocator&) = delete;

        // The adapter is found from the device LUID when null (works for WARP test devices too).
        // False (logged) when D3D12MA cannot be created; createTexture/createBuffer then fail.
        bool init(ID3D12Device* device, IDXGIAdapter* adapter = nullptr);
        void shutdown();
        bool valid() const
        {
            return m_allocator != nullptr;
        }
        ID3D12Device* device() const
        {
            return m_device.Get();
        }

        // DEFAULT heap texture. name is set on the resource and the allocation.
        bool createTexture(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initial, const D3D12_CLEAR_VALUE* clear, const wchar_t* name, GpuResource& out);
        // Buffer on the given heap type (DEFAULT, UPLOAD or READBACK).
        bool createBuffer(uint64_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES initial, D3D12_RESOURCE_FLAGS flags, const wchar_t* name, GpuResource& out);

        // Memory for placed (aliasing) resources and no resource of its own: one block of exactly
        // `bytes` in its own D3D12MA custom pool on a DEFAULT heap that holds buffers and textures
        // together (Resource Heap Tier 2). The memory keeps its pool alive. Not zeroed: every placed
        // texture's first use must be a discard or a clear.
        bool allocatePlacementMemory(uint64_t bytes, const wchar_t* name, GpuResource& out);
        // A texture at `offset` in `memory` (from allocatePlacementMemory), born in LAYOUT_UNDEFINED.
        // It keeps the memory alive; it must be released (deferred) before the memory is reused for
        // a different placement that the GPU could still be reading.
        bool createPlacedTexture(const GpuResource& memory, uint64_t offset, const D3D12_RESOURCE_DESC& desc, const D3D12_CLEAR_VALUE* clear, const wchar_t* name, GpuResource& out);
        // GetResourceAllocationInfo for one resource. False when the device rejects the desc.
        bool allocationInfo(const D3D12_RESOURCE_DESC& desc, uint64_t& bytes, uint64_t& alignment) const;

        GpuMemoryStats stats() const;
        void           logStats(const char* when) const;

    private:
        Microsoft::WRL::ComPtr<ID3D12Device> m_device;
        Microsoft::WRL::ComPtr<IUnknown>     m_allocator; // D3D12MA::Allocator
    };

} // namespace Dark
