#pragma once

#include "Render/FrameFeatures.h"

#include <cstdint>

// What the declared lighting passes read (render-graph plan Phase 6 part 4). CPU only, so the
// native graph tests drive the same declarations SceneRenderer uses.
//
// The Gtao and Ssr entries publish their outputs on the graph blackboard (plan §3.3: "Lighting reads
// GtaoOut if present, else the authored AO"); Lighting and LocalVolumes read them through
// lightingReads(), so the graph plans the barriers from the GTAO compose and SSR Full straight into
// the lighting passes (the Phase 6 part 3 hand-off passes are gone).

namespace Dark
{

    // Published by addGtaoPasses when the compose ran this frame.
    struct GtaoOutput
    {
        Rg::TextureHandle compose;
    };
    // Published by addSsrPasses when the upsample ran this frame.
    struct SsrOutput
    {
        Rg::TextureHandle full;
    };

    // Graph handles a lighting pass reads. ao is the GTAO compose when published, else the authored
    // AO (G-buffer MRT3); ssr is SSR Full when published, else invalid (the pass binds the dummy).
    // shadow / localShadow are the shadow maps as their entries left them, invalid when the frame
    // has none (the pass binds a null view, or the local-shadow dummy).
    struct LightingReads
    {
        Rg::TextureHandle albedo, attrib, depth, ao, ssr, hdr, shadow, localShadow;

        bool valid() const
        {
            return albedo.valid() && attrib.valid() && depth.valid() && ao.valid() && hdr.valid();
        }
    };

    inline LightingReads lightingReads(FeatureBuilder& b)
    {
        LightingReads r;
        r.albedo      = b.handle(RenderResource::Albedo);
        r.attrib      = b.handle(RenderResource::Attrib);
        r.depth       = b.handle(RenderResource::Depth);
        r.hdr         = b.handle(RenderResource::Hdr);
        r.ao          = b.handle(RenderResource::Ao);
        r.shadow      = b.handle(RenderResource::Shadow);
        r.localShadow = b.handle(RenderResource::LocalShadow);
        if (const GtaoOutput* gtao = b.graph().blackboard().tryGet<GtaoOutput>(); gtao && gtao->compose.valid())
            r.ao = gtao->compose;
        if (const SsrOutput* ssr = b.graph().blackboard().tryGet<SsrOutput>(); ssr && ssr->full.valid())
            r.ssr = ssr->full;
        return r;
    }

    enum class LightingPass : uint8_t
    {
        Lighting,     // the sun (directional shadow map) and SSR
        LocalVolumes, // the shadowed local lights (local shadow array)
    };

    // Declares a lighting pass's uses: every input as a pixel-shader SRV and HDR as a read-write render
    // target. Neither pass overwrites all of HDR: Lighting discards sky pixels (they keep the Decals
    // clear until Sky draws) and LocalVolumes blends additively. Returns the new HDR version; `d` gets
    // the versions the pass uses. The caller records it with b.set(RenderResource::Hdr, ...).
    inline Rg::TextureHandle declareLightingUses(Rg::PassBuilder& pb, const LightingReads& in, LightingPass pass, LightingReads& d)
    {
        const bool lighting = pass == LightingPass::Lighting;
        d.albedo            = pb.read(in.albedo, Rg::Use::PixelSrv);
        d.attrib            = pb.read(in.attrib, Rg::Use::PixelSrv);
        d.depth             = pb.read(in.depth, Rg::Use::PixelSrv);
        d.ao                = pb.read(in.ao, Rg::Use::PixelSrv);
        d.ssr               = lighting && in.ssr.valid() ? pb.read(in.ssr, Rg::Use::PixelSrv) : Rg::TextureHandle{};
        d.shadow            = lighting && in.shadow.valid() ? pb.read(in.shadow, Rg::Use::PixelSrv) : Rg::TextureHandle{};
        d.localShadow       = !lighting && in.localShadow.valid() ? pb.read(in.localShadow, Rg::Use::PixelSrv) : Rg::TextureHandle{};
        d.hdr               = pb.readWrite(in.hdr, Rg::Use::RenderTarget);
        return d.hdr;
    }

} // namespace Dark
