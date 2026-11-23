#pragma once

#include "Render/FrameFeatures.h"
#include "Render/LightingInputs.h"
#include "Render/LocalShadowMath.h"
#include "Render/ShadowCascades.h"

#include <cstdint>
#include <utility>

// The debug texture tiles (the Overlay entry): one pass after Post that draws the requested
// G-buffer, depth, AO, SSR and shadow-map previews onto the back buffer, declaring a read of every
// texture it samples. CPU only, so the native graph tests drive the same declarations and layout
// SceneRenderer uses.

namespace Dark
{

    // What the host shows this frame.
    struct DebugTileRequest
    {
        bool gbuffer     = false; // albedo and attrib
        bool velocity    = false; // also on with gbuffer
        bool ssao        = false; // GTAO's full-resolution AO
        bool ssrFull     = false; // SSR Full
        bool ssrConf     = false; // the SSR confidence debug target
        bool depth       = false;
        bool sunShadow   = false; // one tile per cascade of this frame's half
        bool localShadow = false; // one tile per local-shadow slice of this frame's half

        bool any() const
        {
            return gbuffer || velocity || ssao || ssrFull || ssrConf || depth || sunShadow || localShadow;
        }
    };

    // The versions the pass used; a texture that was not requested or does not exist this frame is
    // invalid and its tiles are not drawn.
    struct DebugTileReads
    {
        Rg::TextureHandle swap, albedo, attrib, velocity, depth, ao, ssr, shadow, localShadow;
    };

    // Screen rectangles. Top row from the left: albedo, attrib, velocity, AO, SSR. Bottom row: depth,
    // then the cascades. The local slices sit above the bottom row when it is used, else on it.
    struct DebugTileRect
    {
        int32_t x = 0, y = 0, w = 0, h = 0;
    };
    struct DebugTileLayout
    {
        // Every cascade and every local slice of a frame's half (ShadowSystem, LocalShadowSystem).
        static constexpr int kMaxCascades    = kMaxShadowCascades;
        static constexpr int kMaxLocalSlices = static_cast<int>(kLocalShadowSlicesPerFrame);
        DebugTileRect        albedo, attrib, velocity, ao, ssr, depth;
        DebugTileRect        cascades[kMaxCascades];
        DebugTileRect        localSlices[kMaxLocalSlices];
        int                  cascadeCount = 0, localCount = 0;
    };

    // `tile` 0: a fifth of the height, clamped to 96..220. Only requested tiles get a rectangle.
    inline DebugTileLayout layoutDebugTiles(const DebugTileRequest& r, int32_t width, int32_t height, int32_t tile, int cascades, int localSlices)
    {
        constexpr int32_t pad = 12, gap = 8;
        if (tile <= 0)
            tile = height / 5 < 96 ? 96 : (height / 5 > 220 ? 220 : height / 5);
        DebugTileLayout l;
        const auto      row = [&](DebugTileRect* out, int n, int32_t x0, int32_t y)
        {
            int32_t tw = tile;
            if (n > 0 && x0 + n * tw + (n - 1) * gap > width - pad)
            {
                const int32_t avail = width - pad - x0 - (n - 1) * gap;
                if (avail > 64)
                    tw = avail / n;
            }
            for (int i = 0; i < n; ++i)
                out[i] = { x0 + i * (tw + gap), y, tw, tile };
        };

        int32_t x = pad;
        if (r.gbuffer)
        {
            l.albedo = { x, pad, tile, tile };
            l.attrib = { x + tile + gap, pad, tile, tile };
            x += 2 * (tile + gap);
        }
        if (r.gbuffer || r.velocity)
        {
            l.velocity = { x, pad, tile, tile };
            x += tile + gap;
        }
        if (r.ssao)
        {
            l.ao = { x, pad, tile, tile };
            x += tile + gap;
        }
        if (r.ssrFull || r.ssrConf)
            l.ssr = { x, pad, tile, tile };

        const int32_t bottom = height - pad - tile;
        int32_t       x0     = pad;
        if (r.depth)
        {
            l.depth = { pad, bottom, tile, tile };
            x0 += tile + pad;
        }
        if (r.sunShadow)
        {
            l.cascadeCount = cascades < 0 ? 0 : (cascades > DebugTileLayout::kMaxCascades ? DebugTileLayout::kMaxCascades : cascades);
            row(l.cascades, l.cascadeCount, x0, bottom);
        }
        if (r.localShadow)
        {
            l.localCount = localSlices < 0 ? 0 : (localSlices > DebugTileLayout::kMaxLocalSlices ? DebugTileLayout::kMaxLocalSlices : localSlices);
            row(l.localSlices, l.localCount, pad, r.depth || r.sunShadow ? bottom - gap - tile : bottom);
        }
        return l;
    }

    // The Overlay entry: reads what `r` asks for (GTAO's full AO and SSR's targets from their
    // published outputs, the rest from the frame's resources) and draws onto the back buffer Post
    // wrote. False (nothing added) without a back buffer or a request.
    // `exec(PassContext&, const DebugTileReads&)`.
    template <class Exec> bool addDebugTilePass(FeatureBuilder& b, const DebugTileRequest& r, Exec&& exec)
    {
        using R = RenderResource;
        DebugTileReads in;
        in.swap = b.handle(R::Swap);
        if (!in.swap.valid() || !r.any())
            return false;
        if (r.gbuffer)
        {
            in.albedo = b.handle(R::Albedo);
            in.attrib = b.handle(R::Attrib);
        }
        if (r.gbuffer || r.velocity)
            in.velocity = b.handle(R::Velocity);
        if (r.depth)
            in.depth = b.handle(R::Depth);
        if (r.sunShadow)
            in.shadow = b.handle(R::Shadow);
        if (r.localShadow)
            in.localShadow = b.handle(R::LocalShadow);
        const Rg::Blackboard& bb = b.graph().blackboard();
        if (const GtaoOutput* gtao = bb.tryGet<GtaoOutput>(); r.ssao && gtao)
            in.ao = gtao->full;
        if (const SsrOutput* ssr = bb.tryGet<SsrOutput>(); ssr)
            in.ssr = r.ssrConf ? ssr->conf : (r.ssrFull ? ssr->full : Rg::TextureHandle{});
        DebugTileReads out;
        b.addPass<DebugTileReads>(
            "Overlay",
            [&](Rg::PassBuilder& pb, DebugTileReads& d)
            {
                const auto read = [&pb](Rg::TextureHandle h) { return h.valid() ? pb.read(h, Rg::Use::PixelSrv) : Rg::TextureHandle{}; };
                d.albedo        = read(in.albedo);
                d.attrib        = read(in.attrib);
                d.velocity      = read(in.velocity);
                d.depth         = read(in.depth);
                d.ao            = read(in.ao);
                d.ssr           = read(in.ssr);
                d.shadow        = read(in.shadow);
                d.localShadow   = read(in.localShadow);
                d.swap          = pb.readWrite(in.swap, Rg::Use::RenderTarget);
                out             = d;
            },
            std::forward<Exec>(exec));
        b.set(R::Swap, out.swap);
        return true;
    }

} // namespace Dark
