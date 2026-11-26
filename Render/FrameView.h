#pragma once

#include "Render/FrameSetup.h"

namespace Dark
{

    // Per-frame view state the host hands FrameComposer::begin (render-graph plan Phase 5).
    struct FrameView
    {
        // A strobe-like frame (echolocation pulse) whose image must not feed temporal filters:
        // every graph history reads as invalid this frame and the next, and motion blur is off.
        bool holdTemporal = false;
        // The engine passes that run (Render/FrameSetup.h). FrameComposer::begin adds its hooks' bits.
        FrameSetup setup;
    };

} // namespace Dark
