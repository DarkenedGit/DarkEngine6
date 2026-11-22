#pragma once

#include "Render/FrameFeatures.h"

#include <utility>

// The passes that draw into the lit HDR image with read-only depth (Sky, Water, Clouds, Underwater).
// CPU only, so the native graph tests drive the same declarations SceneRenderer uses.

namespace Dark
{

    // Published by addSsrCapturePasses when the capture ran this frame: the scene-color slot it
    // wrote, which Water and the underwater ceiling sample for their SSR march.
    struct SsrCaptureOutput
    {
        Rg::TextureHandle sceneColor;
    };

    // hdr: read and written as the render target (these passes blend onto it, or keep pixels they
    // skip). depth: the read-only depth target, also sampled. sceneColor: this frame's capture when
    // the pass asked for it and SsrCapture published one.
    struct HdrDrawTargets
    {
        Rg::TextureHandle hdr, depth, sceneColor;

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

    // One pass drawing into HDR with read-only depth. `exec(PassContext&, const HdrDrawTargets&)` gets
    // the versions it uses. Returns false (and adds nothing) without HDR or depth.
    template <class Exec> bool addHdrDrawPass(FeatureBuilder& b, const char* name, bool readsCapture, Exec&& exec)
    {
        HdrDrawTargets in;
        in.hdr   = b.handle(RenderResource::Hdr);
        in.depth = b.handle(RenderResource::Depth);
        if (!in.valid())
            return false;
        if (readsCapture)
            in.sceneColor = publishedSceneColor(b);
        HdrDrawTargets out;
        b.addPass<HdrDrawTargets>(
            name,
            [&](Rg::PassBuilder& pb, HdrDrawTargets& d)
            {
                d.depth      = pb.read(in.depth, Rg::Use::DepthRead);
                d.sceneColor = in.sceneColor.valid() ? pb.read(in.sceneColor, Rg::Use::PixelSrv) : Rg::TextureHandle{};
                d.hdr        = pb.readWrite(in.hdr, Rg::Use::RenderTarget);
                out          = d;
            },
            std::forward<Exec>(exec));
        b.set(RenderResource::Hdr, out.hdr);
        return true;
    }

    // The underwater entry: a copy of HDR, then one pass that samples the copy and depth, rewrites HDR
    // and draws the water ceiling onto it (which samples this frame's capture when there is one).
    struct UnderwaterTargets
    {
        Rg::TextureHandle hdr, depth, copy, sceneColor;

        bool valid() const
        {
            return hdr.valid() && depth.valid() && copy.valid();
        }
    };

    // `copy`: the HDR copy target imported for this frame. `copyExec(PassContext&, const
    // UnderwaterTargets&)` records the copy (hdr and copy are set); `exec` draws. Returns false (and
    // adds nothing) when a target is missing.
    template <class CopyExec, class Exec> bool addUnderwaterPasses(FeatureBuilder& b, Rg::TextureHandle copy, CopyExec&& copyExec, Exec&& exec)
    {
        UnderwaterTargets in;
        in.hdr        = b.handle(RenderResource::Hdr);
        in.depth      = b.handle(RenderResource::Depth);
        in.copy       = copy;
        in.sceneColor = publishedSceneColor(b);
        if (!in.valid())
            return false;
        Rg::TextureHandle copied;
        b.addPass<UnderwaterTargets>(
            "Underwater.Copy",
            [&](Rg::PassBuilder& pb, UnderwaterTargets& d)
            {
                d.hdr  = pb.read(in.hdr, Rg::Use::CopySrc);
                d.copy = copied = pb.write(in.copy, Rg::Use::CopyDst);
            },
            std::forward<CopyExec>(copyExec));
        UnderwaterTargets out;
        b.addPass<UnderwaterTargets>(
            "Underwater",
            [&](Rg::PassBuilder& pb, UnderwaterTargets& d)
            {
                d.copy       = pb.read(copied, Rg::Use::PixelSrv);
                d.depth      = pb.read(in.depth, Rg::Use::DepthRead);
                d.sceneColor = in.sceneColor.valid() ? pb.read(in.sceneColor, Rg::Use::PixelSrv) : Rg::TextureHandle{};
                d.hdr        = pb.readWrite(in.hdr, Rg::Use::RenderTarget);
                out          = d;
            },
            std::forward<Exec>(exec));
        b.set(RenderResource::Hdr, out.hdr);
        return true;
    }

} // namespace Dark
