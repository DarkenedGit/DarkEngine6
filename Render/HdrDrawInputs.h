#pragma once

#include "Render/FrameFeatures.h"

#include <utility>

// The passes that draw into the lit HDR image after the lighting passes: Sky, Water, Clouds, the
// game slot, Translucent, the debug overlays, the Editor grid and gizmos, Particles and Underwater.
// CPU only, so the native graph tests drive the same declarations SceneRenderer uses.

namespace Dark
{

    // Published by addSsrCapturePasses when the capture ran this frame: the scene-color slot it
    // wrote, which Water and the underwater ceiling sample for their SSR march.
    struct SsrCaptureOutput
    {
        Rg::TextureHandle sceneColor;
    };

    // What an HDR draw pass uses besides HDR (a render target, read and written: these passes blend
    // onto it or keep the pixels they skip) and read-only depth (tested and sampled by pixel shaders).
    struct HdrDrawUses
    {
        bool sceneColor    = false; // this frame's SSR capture, when SsrCapture published one
        bool depthWrite    = false; // depth tested and written instead (a body drawn over the scene)
        bool computeDepth  = false; // depth also sampled by compute work recorded in the pass
        bool computeAttrib = false; // the G-buffer attrib target sampled by compute work
        bool shadow        = false; // the directional shadow map sampled by pixel shaders
    };

    // The passes that sample the directional shadow map: Sky (in-scattering), Water and the
    // underwater ceiling (water also reads this frame's capture), and Translucent (the forward
    // materials' heaps carry the shadow SRV).
    inline HdrDrawUses skyUses()
    {
        HdrDrawUses u;
        u.shadow = true;
        return u;
    }
    inline HdrDrawUses waterUses()
    {
        HdrDrawUses u;
        u.sceneColor = true;
        u.shadow     = true;
        return u;
    }
    inline HdrDrawUses translucentUses()
    {
        return skyUses();
    }

    // The versions a pass used. sceneColor / attrib / shadow are invalid when not used (or, for the
    // shadow map, when the frame has none).
    struct HdrDrawTargets
    {
        Rg::TextureHandle hdr, depth, sceneColor, attrib, shadow;

        bool valid() const
        {
            return hdr.valid() && depth.valid();
        }
    };

    inline Rg::TextureHandle publishedSceneColor(FeatureBuilder& b)
    {
        const SsrCaptureOutput* capture = b.graph().blackboard().tryGet<SsrCaptureOutput>();
        return capture ? capture->sceneColor : Rg::TextureHandle{};
    }

    namespace HdrDraw
    {
        // Depth (and the extra reads `uses` names) for one pass; returns the depth version after it.
        inline Rg::TextureHandle declareDepth(Rg::PassBuilder& pb, Rg::TextureHandle depth, const HdrDrawUses& uses)
        {
            if (uses.depthWrite)
                return pb.readWrite(depth, Rg::Use::DepthWrite);
            const Rg::TextureHandle h = pb.read(depth, Rg::Use::DepthRead);
            if (uses.computeDepth)
                pb.read(depth, Rg::Use::ComputeSrv); // one layout (DIRECT_QUEUE_GENERIC_READ) serves both
            return h;
        }
    } // namespace HdrDraw

    // One pass drawing into HDR. `exec(PassContext&, const HdrDrawTargets&)` gets the versions it uses.
    // A pass that writes depth publishes the new version for the passes after it. Returns false (and
    // adds nothing) without HDR or depth, or without attrib when it asks for it.
    template <class Exec> bool addHdrDrawPass(FeatureBuilder& b, const char* name, const HdrDrawUses& uses, Exec&& exec)
    {
        HdrDrawTargets in;
        in.hdr   = b.handle(RenderResource::Hdr);
        in.depth = b.handle(RenderResource::Depth);
        if (uses.computeAttrib)
        {
            in.attrib = b.handle(RenderResource::Attrib);
            if (!in.attrib.valid())
                return false;
        }
        if (!in.valid())
            return false;
        if (uses.sceneColor)
            in.sceneColor = publishedSceneColor(b);
        if (uses.shadow)
            in.shadow = b.handle(RenderResource::Shadow);
        HdrDrawTargets out;
        b.addPass<HdrDrawTargets>(
            name,
            [&](Rg::PassBuilder& pb, HdrDrawTargets& d)
            {
                d.depth      = HdrDraw::declareDepth(pb, in.depth, uses);
                d.sceneColor = in.sceneColor.valid() ? pb.read(in.sceneColor, Rg::Use::PixelSrv) : Rg::TextureHandle{};
                d.attrib     = in.attrib.valid() ? pb.read(in.attrib, Rg::Use::ComputeSrv) : Rg::TextureHandle{};
                d.shadow     = in.shadow.valid() ? pb.read(in.shadow, Rg::Use::PixelSrv) : Rg::TextureHandle{};
                d.hdr        = pb.readWrite(in.hdr, Rg::Use::RenderTarget);
                out          = d;
            },
            std::forward<Exec>(exec));
        b.set(RenderResource::Hdr, out.hdr);
        if (uses.depthWrite)
            b.set(RenderResource::Depth, out.depth);
        return true;
    }

    template <class Exec> bool addHdrDrawPass(FeatureBuilder& b, const char* name, bool readsCapture, Exec&& exec)
    {
        HdrDrawUses uses;
        uses.sceneColor = readsCapture;
        return addHdrDrawPass(b, name, uses, std::forward<Exec>(exec));
    }

    // A copy of HDR, then one pass that samples the copy and rewrites HDR (Underwater, game passes).
    struct HdrCopyTargets
    {
        Rg::TextureHandle hdr, depth, copy, sceneColor, shadow;

        bool valid() const
        {
            return hdr.valid() && depth.valid() && copy.valid();
        }
    };
    using UnderwaterTargets = HdrCopyTargets;

    // `copy`: the HDR copy target imported for this frame. `copyExec(PassContext&, const
    // HdrCopyTargets&)` records the copy (hdr and copy are set); `exec` draws (`uses` as for
    // addHdrDrawPass; the compute reads are not offered). Returns false (and adds nothing) when a
    // target is missing.
    template <class CopyExec, class Exec>
    bool addHdrCopyPasses(FeatureBuilder& b, const char* copyName, const char* drawName, Rg::TextureHandle copy, const HdrDrawUses& uses, CopyExec&& copyExec, Exec&& exec)
    {
        HdrCopyTargets in;
        in.hdr   = b.handle(RenderResource::Hdr);
        in.depth = b.handle(RenderResource::Depth);
        in.copy  = copy;
        if (uses.sceneColor)
            in.sceneColor = publishedSceneColor(b);
        if (uses.shadow)
            in.shadow = b.handle(RenderResource::Shadow);
        if (!in.valid())
            return false;
        Rg::TextureHandle copied;
        b.addPass<HdrCopyTargets>(
            copyName,
            [&](Rg::PassBuilder& pb, HdrCopyTargets& d)
            {
                d.hdr  = pb.read(in.hdr, Rg::Use::CopySrc);
                d.copy = copied = pb.write(in.copy, Rg::Use::CopyDst);
            },
            std::forward<CopyExec>(copyExec));
        HdrDrawUses drawUses   = uses;
        drawUses.computeDepth  = false;
        drawUses.computeAttrib = false;
        HdrCopyTargets out;
        b.addPass<HdrCopyTargets>(
            drawName,
            [&](Rg::PassBuilder& pb, HdrCopyTargets& d)
            {
                d.copy       = pb.read(copied, Rg::Use::PixelSrv);
                d.depth      = HdrDraw::declareDepth(pb, in.depth, drawUses);
                d.sceneColor = in.sceneColor.valid() ? pb.read(in.sceneColor, Rg::Use::PixelSrv) : Rg::TextureHandle{};
                d.shadow     = in.shadow.valid() ? pb.read(in.shadow, Rg::Use::PixelSrv) : Rg::TextureHandle{};
                d.hdr        = pb.readWrite(in.hdr, Rg::Use::RenderTarget);
                out          = d;
            },
            std::forward<Exec>(exec));
        b.set(RenderResource::Hdr, out.hdr);
        if (drawUses.depthWrite)
            b.set(RenderResource::Depth, out.depth);
        return true;
    }

    // The underwater entry: the fog samples the copy and depth, then the water ceiling draws onto HDR
    // (sampling this frame's capture when there is one, and the shadow map).
    template <class CopyExec, class Exec> bool addUnderwaterPasses(FeatureBuilder& b, Rg::TextureHandle copy, CopyExec&& copyExec, Exec&& exec)
    {
        return addHdrCopyPasses(b, "Underwater.Copy", "Underwater", copy, waterUses(), std::forward<CopyExec>(copyExec), std::forward<Exec>(exec));
    }

    // As addHdrCopyPasses with no extra reads, but the draw also writes depth, so the passes after it
    // test against what it drew. `copyName` and `drawName` must outlive the frame (literals).
    template <class CopyExec, class Exec> bool addHdrCopyDepthWritePasses(FeatureBuilder& b, const char* copyName, const char* drawName, Rg::TextureHandle copy, CopyExec&& copyExec, Exec&& exec)
    {
        HdrDrawUses uses;
        uses.depthWrite = true;
        return addHdrCopyPasses(b, copyName, drawName, copy, uses, std::forward<CopyExec>(copyExec), std::forward<Exec>(exec));
    }

} // namespace Dark
