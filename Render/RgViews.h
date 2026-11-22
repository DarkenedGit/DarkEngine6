#pragma once

#include "Render/RenderGraph.h"

#include <cstdint>
#include <d3d12.h>
#include <initializer_list>

// D3D12 side of the graph's views (render-graph plan Phase 6 part 2): the graph core keeps
// descriptor handles as integers (it stays D3D12-free); converted passes use these to get typed
// handles back, and imports use importTracked so the plan starts from the frame tracker's layout.

namespace Dark
{

    inline D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle(uint64_t ptr)
    {
        return D3D12_CPU_DESCRIPTOR_HANDLE{ static_cast<SIZE_T>(ptr) };
    }
    inline D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle(uint64_t ptr)
    {
        return D3D12_GPU_DESCRIPTOR_HANDLE{ ptr };
    }

    inline Rg::TextureViews textureViews(D3D12_CPU_DESCRIPTOR_HANDLE srv, D3D12_CPU_DESCRIPTOR_HANDLE rtv = {}, D3D12_CPU_DESCRIPTOR_HANDLE dsv = {},
                                         D3D12_CPU_DESCRIPTOR_HANDLE dsvReadOnly = {})
    {
        Rg::TextureViews v;
        v.srv         = srv.ptr;
        v.rtv         = rtv.ptr;
        v.dsv         = dsv.ptr;
        v.dsvReadOnly = dsvReadOnly.ptr;
        return v;
    }

    inline D3D12_CPU_DESCRIPTOR_HANDLE rgRtv(const Rg::PassContext& c, Rg::TextureHandle h)
    {
        return cpuHandle(c.rtv(h));
    }
    inline D3D12_CPU_DESCRIPTOR_HANDLE rgSrvCpu(const Rg::PassContext& c, Rg::TextureHandle h)
    {
        return cpuHandle(c.srvCpu(h));
    }
    // The declared SRVs, in order, as one table in the frame's descriptor ring (ptr 0 on failure).
    inline D3D12_GPU_DESCRIPTOR_HANDLE rgSrvTable(const Rg::PassContext& c, std::initializer_list<Rg::TextureHandle> hs)
    {
        return gpuHandle(c.srvTable(hs));
    }
    // A table of CPU views (graph views from rgSrvCpu, or views the graph does not own).
    D3D12_GPU_DESCRIPTOR_HANDLE rgTable(const Rg::PassContext& c, std::initializer_list<D3D12_CPU_DESCRIPTOR_HANDLE> cpu);

    // Imports a persistent resource the frame ResourceStateTracker follows (a graph target or the back
    // buffer): the plan's initial layout is the tracker's current one (Undefined when untracked) and
    // there is no final layout (the tracker carries the state to the next owner). Setup time only.
    Rg::TextureHandle importTracked(Rg::RenderGraph& g, const char* name, ID3D12Resource* native, const Rg::TextureViews& views);

} // namespace Dark
