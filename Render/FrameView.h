#pragma once

namespace Dark
{

    // Per-frame view state the host hands FrameComposer::begin (render-graph plan Phase 5).
    struct FrameView
    {
        // A strobe-like frame (echolocation pulse) whose image must not feed temporal filters:
        // every graph history reads as invalid this frame and the next, and motion blur is off.
        bool holdTemporal = false;
    };

} // namespace Dark
