#pragma once

#include "Render/RenderTargetPool.h"

#include <memory>

namespace Dark
{
    class GpuAllocator;

    // D3D12 backend for the graph's persistent target registry. CPU RTV/DSV/SRV descriptor pages
    // belong to this backend; the shader-visible tables remain with the opaque pipelines until
    // Phase 6. Every texture is allocated through GpuAllocator (D3D12MA); there is no aliasing.
    class D3D12TargetBackend final : public Rg::TargetBackend
    {
    public:
        explicit D3D12TargetBackend(GpuAllocator& allocator);
        ~D3D12TargetBackend() override; // the caller must drain the GPU before destruction
        D3D12TargetBackend(const D3D12TargetBackend&)            = delete;
        D3D12TargetBackend& operator=(const D3D12TargetBackend&) = delete;

        std::unique_ptr<Rg::TargetStorage> create(const Rg::TargetDesc& desc, Rg::TargetExtent extent, const char* name) override;
        void                               retire(std::unique_ptr<Rg::TargetStorage> storage) override;

        // Before unregister/replacement, specify the last submitted graphics fence that could
        // reference the target. collect releases WHOLE targets (views, Allocation and resource)
        // only when that fence is complete. No waits or frame-count guesses in this backend.
        void     retireAfter(uint64_t lastSubmittedFence);
        void     collect(uint64_t completedFence);
        uint32_t retiredCount() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Dark
