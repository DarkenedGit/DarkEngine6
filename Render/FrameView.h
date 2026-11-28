#pragma once

#include "Render/FrameSetup.h"

namespace Dark
{

    // Per-frame view state the host hands FrameComposer::begin (render-graph plan Phase 5).
    struct DebugRenderState;

    struct FrameView
    {
        // A strobe-like frame (a game pulse effect) whose image must not feed temporal filters:
        // every graph history reads as invalid this frame and the next, and motion blur is off.
        bool holdTemporal = false;
        // The engine passes that run (Render/FrameSetup.h). FrameComposer::resolveSetup (or begin, if that was not called)
        // folds debug toggles when `debug` is set, then adds its hooks' bits and scalars.
        FrameSetup setup;
        // Host's debug toggles for this frame, or null (no folding: tests and a view that only
        // carries hook bits). Does not own the state.
        const DebugRenderState* debug = nullptr;
    };

} // namespace Dark
