#pragma once

#include "Render/RenderGraph.h"

#include <d3d12.h>

// Target binds and shadow tables for the passes that draw into the lit HDR image, shared by
// SceneRenderer and the domain render features.

namespace Dark
{

    class Renderer;
    struct HdrCopyTargets;
    struct LightingViews;

    // Graph views of HDR (and the depth target), with the viewport.
    void bindHdr(Rg::PassContext& c, const Renderer& renderer, Rg::TextureHandle hdr);
    void bindHdrDepthRead(Rg::PassContext& c, const Renderer& renderer, Rg::TextureHandle hdr, Rg::TextureHandle depth);
    void bindHdrDepthWrite(Rg::PassContext& c, const Renderer& renderer, Rg::TextureHandle hdr, Rg::TextureHandle depth);

    // `shadow` invalid: the null view (no shadow map this frame).
    D3D12_CPU_DESCRIPTOR_HANDLE shadowSrv(const Rg::PassContext& c, const LightingViews& v, Rg::TextureHandle shadow);
    D3D12_GPU_DESCRIPTOR_HANDLE shadowTable(Rg::PassContext& c, const Renderer& renderer, Rg::TextureHandle shadow);

    // Copies HDR into the entry's HDR copy (Camouflage, Underwater).
    void copyHdr(Rg::PassContext& c, const HdrCopyTargets& d);

    // A list entry with no scene targets to draw into (logs once for all entries).
    void logMissingSceneTargets(const char* entry);

} // namespace Dark
