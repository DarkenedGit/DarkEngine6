#pragma once

#include "Render/FrameFeatures.h"

#include <utility>

// CPU only, so the native graph tests drive the same declarations SceneRenderer uses.

namespace Dark
{

    // The G-buffer pass's targets: four MRTs it clears and draws, and scene depth.
    struct GBufferTargets
    {
        Rg::TextureHandle albedo, attrib, velocity, ao, depth;

        bool valid() const
        {
            return albedo.valid() && attrib.valid() && velocity.valid() && ao.valid() && depth.valid();
        }
    };

    // What the decal pass touches: depth (tested read-only and sampled) and the two MRTs it blends
    // into through rasterizer-ordered UAVs.
    struct DecalTargets
    {
        Rg::TextureHandle depth, albedo, attrib;

        bool valid() const
        {
            return depth.valid() && albedo.valid() && attrib.valid();
        }
    };

    struct HdrTarget
    {
        Rg::TextureHandle hdr;
    };

    inline GBufferTargets gbufferTargets(FeatureBuilder& b)
    {
        GBufferTargets t;
        t.albedo   = b.handle(RenderResource::Albedo);
        t.attrib   = b.handle(RenderResource::Attrib);
        t.velocity = b.handle(RenderResource::Velocity);
        t.ao       = b.handle(RenderResource::Ao);
        t.depth    = b.handle(RenderResource::Depth);
        return t;
    }

    // The G-buffer pass. The MRTs are cleared in the pass, so they are written (their old contents
    // are dead); depth was cleared when the frame began (Renderer::beginFrame), so it is read and
    // written. `exec(PassContext&, const GBufferTargets&)` gets the versions it uses. Returns false
    // (and adds nothing) when a target is missing.
    template <class Exec> bool addGBufferPass(FeatureBuilder& b, Exec&& exec)
    {
        const GBufferTargets in = gbufferTargets(b);
        if (!in.valid())
            return false;
        GBufferTargets out;
        b.addPass<GBufferTargets>(
            "GBuffer",
            [&](Rg::PassBuilder& pb, GBufferTargets& d)
            {
                d.albedo   = pb.write(in.albedo, Rg::Use::RenderTarget);
                d.attrib   = pb.write(in.attrib, Rg::Use::RenderTarget);
                d.velocity = pb.write(in.velocity, Rg::Use::RenderTarget);
                d.ao       = pb.write(in.ao, Rg::Use::RenderTarget);
                d.depth    = pb.readWrite(in.depth, Rg::Use::DepthWrite);
                out        = d;
            },
            std::forward<Exec>(exec));
        b.set(RenderResource::Albedo, out.albedo);
        b.set(RenderResource::Attrib, out.attrib);
        b.set(RenderResource::Velocity, out.velocity);
        b.set(RenderResource::Ao, out.ao);
        b.set(RenderResource::Depth, out.depth);
        return true;
    }

    // The decal pass: blends into albedo and attrib (read and written through UAVs; decals keep what
    // is under them) and reads depth both as the read-only depth target and as an SRV.
    template <class Exec> bool addDecalPass(FeatureBuilder& b, Exec&& exec)
    {
        DecalTargets in;
        in.depth  = b.handle(RenderResource::Depth);
        in.albedo = b.handle(RenderResource::Albedo);
        in.attrib = b.handle(RenderResource::Attrib);
        if (!in.valid())
            return false;
        DecalTargets out;
        b.addPass<DecalTargets>(
            "Decals",
            [&](Rg::PassBuilder& pb, DecalTargets& d)
            {
                d.depth  = pb.read(in.depth, Rg::Use::DepthRead);
                d.albedo = pb.readWrite(in.albedo, Rg::Use::Uav);
                d.attrib = pb.readWrite(in.attrib, Rg::Use::Uav);
                out      = d;
            },
            std::forward<Exec>(exec));
        b.set(RenderResource::Albedo, out.albedo);
        b.set(RenderResource::Attrib, out.attrib);
        return true;
    }

    // The HDR clear at the end of the Decals entry (as before): a full overwrite the lighting passes
    // draw onto.
    template <class Exec> bool addHdrClearPass(FeatureBuilder& b, Exec&& exec)
    {
        const Rg::TextureHandle in = b.handle(RenderResource::Hdr);
        if (!in.valid())
            return false;
        HdrTarget out;
        b.addPass<HdrTarget>(
            "Decals.ClearHdr",
            [&](Rg::PassBuilder& pb, HdrTarget& d)
            {
                d.hdr = pb.write(in, Rg::Use::RenderTarget);
                out   = d;
            },
            std::forward<Exec>(exec));
        b.set(RenderResource::Hdr, out.hdr);
        return true;
    }

} // namespace Dark
