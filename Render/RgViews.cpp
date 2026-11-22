#include "Render/RgViews.h"

#include "Render/EnhancedBarrierEmitter.h"
#include "Render/ResourceStateTracker.h"

namespace Dark
{

    D3D12_GPU_DESCRIPTOR_HANDLE rgTable(const Rg::PassContext& c, std::initializer_list<D3D12_CPU_DESCRIPTOR_HANDLE> cpu)
    {
        constexpr uint32_t kMax = 32;
        if (cpu.size() == 0 || cpu.size() > kMax)
            return {};
        uint64_t raw[kMax];
        uint32_t n = 0;
        for (const D3D12_CPU_DESCRIPTOR_HANDLE h : cpu)
            raw[n++] = h.ptr;
        return gpuHandle(c.descriptorTable(raw, n));
    }

    Rg::TextureHandle importTracked(Rg::RenderGraph& g, const char* name, ID3D12Resource* native, const Rg::TextureViews& views)
    {
        if (!native)
            return {};
        const D3D12_RESOURCE_DESC rd = native->GetDesc();
        Rg::TextureDesc           desc;
        desc.sizeMode  = Rg::SizeMode::Absolute;
        desc.width     = static_cast<uint32_t>(rd.Width);
        desc.height    = static_cast<uint32_t>(rd.Height);
        desc.format    = static_cast<uint32_t>(rd.Format);
        desc.mips      = static_cast<uint16_t>(rd.MipLevels);
        desc.arraySize = static_cast<uint16_t>(rd.DepthOrArraySize);
        Rg::Layout initial = Rg::Layout::Undefined;
        if (const ResourceStateTracker* tracker = frameStateTracker())
            if (const Rg::UseState* now = tracker->current(native))
                initial = now->layout;
        const Rg::TextureHandle h = g.importTexture(name, desc, native, initial, Rg::Layout::Undefined);
        g.setViews(h, views);
        return h;
    }

} // namespace Dark
