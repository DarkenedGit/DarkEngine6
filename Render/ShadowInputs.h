#pragma once

#include "Render/FrameFeatures.h"
#include "Render/RenderGraphBarriers.h"
#include "Render/ResourceStateTracker.h"

#include <cstdint>
#include <utility>

struct ID3D12Resource;

// The shadow-map writers as declared passes. CPU only, so the native graph tests drive the same
// declarations SceneRenderer uses.
//
// The directional cascades may be recorded on the shadow list (on the main thread or the shadow
// worker), which Renderer::endFrame submits ahead of the main list in the same ExecuteCommandLists.
// No barrier the graph records on the main list can come before that work, so the map starts every
// frame in its write state: it is created in DEPTH_WRITE, and FrameComposer::execute moves it back
// there after the last reader (endShadowFrame). The Shadows pass then needs no barrier of its own,
// and the barrier into the first reader waits for its depth writes whichever list recorded them.
// The local shadow array is recorded on the main list, so its barriers are planned like any other.
// Both arrays are tracked whole: a frame uses one half of each, and the other half moves with it.

namespace Dark
{

    // The shadow maps a frame imports, with their shader-resource views (null when the map is
    // missing: the import is skipped and its handle stays invalid).
    struct FrameShadows
    {
        ID3D12Resource* map         = nullptr;
        uint64_t        mapSrv      = 0;
        ID3D12Resource* localMap    = nullptr;
        uint64_t        localMapSrv = 0;
    };

    struct ShadowTargets
    {
        Rg::TextureHandle map;
    };

    // The state the directional shadow map holds when a frame starts.
    inline Rg::UseState shadowFrameStartState()
    {
        return Rg::useState(Rg::Use::DepthWrite, Rg::ResourceKind::Texture, Rg::Queue::Graphics);
    }

    // After the frame's last access: moves the tracked map to shadowFrameStartState(). True (with
    // `before`) when a barrier has to be recorded; false when it is already there or untracked.
    inline bool endShadowFrame(ResourceStateTracker& tracker, const void* map, Rg::UseState& before)
    {
        return map && tracker.transition(map, shadowFrameStartState(), before);
    }

    // The Shadows entry: one pass that records the frame's GPU cull and the cascades, writing the
    // whole map (no barrier: it starts the frame writable). It always runs (the camera cull is in it),
    // so it is a side effect even without a map. `exec(PassContext&, const ShadowTargets&)`.
    template <class Exec> void addShadowPass(FeatureBuilder& b, Exec&& exec)
    {
        const Rg::TextureHandle in = b.handle(RenderResource::Shadow);
        ShadowTargets           out;
        b.addPass<ShadowTargets>(
            "Shadows",
            [&](Rg::PassBuilder& pb, ShadowTargets& d)
            {
                pb.flags(Rg::PassFlags::SideEffect);
                d.map = in.valid() ? pb.write(in, Rg::Use::DepthWrite) : Rg::TextureHandle{};
                out   = d;
            },
            std::forward<Exec>(exec));
        if (out.map.valid())
            b.set(RenderResource::Shadow, out.map);
    }

    // The LocalShadows entry: one pass that clears this frame's half of the array and draws each face
    // into it. Returns false (and adds nothing) without the array.
    template <class Exec> bool addLocalShadowPass(FeatureBuilder& b, Exec&& exec)
    {
        const Rg::TextureHandle in = b.handle(RenderResource::LocalShadow);
        if (!in.valid())
            return false;
        ShadowTargets out;
        b.addPass<ShadowTargets>(
            "LocalShadows",
            [&](Rg::PassBuilder& pb, ShadowTargets& d)
            {
                d.map = pb.write(in, Rg::Use::DepthWrite);
                out   = d;
            },
            std::forward<Exec>(exec));
        b.set(RenderResource::LocalShadow, out.map);
        return true;
    }

} // namespace Dark
