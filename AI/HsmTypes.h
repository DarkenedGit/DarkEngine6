#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace Dark::AI
{

    class HsmMachine;
    class HsmState;

    // Opaque event identity. Reserve 0 as "none / tick".
    using HsmEventId = uint32_t;

    inline constexpr HsmEventId kHsmEventNone = 0;
    inline constexpr HsmEventId kHsmEventTick = 1; // optional dt-driven update

    struct HsmEvent
    {
        HsmEventId id        = kHsmEventNone;
        void*      payload   = nullptr;
        float      deltaTime = 0.0f; // meaningful for kHsmEventTick
    };

    // History mode for composite states.
    enum class HsmHistory : uint8_t
    {
        None,    // always enter via initial child
        Shallow, // restore last direct child
        Deep     // restore last full leaf path under this composite
    };

    // How a transition relates to the active configuration.
    enum class HsmTransitionKind : uint8_t
    {
        External, // exit up to LCCA, run action, enter down to target (default) 
        Local,    // same as external but if target is inside source composite, do not exit source
        Internal  // no state change; run action only (target ignored)
    };

    // Mutable context passed to guards, actions, and event handlers.
    struct HsmContext
    {
        HsmMachine&     machine;
        const HsmEvent& event;
        HsmState*       source   = nullptr; // state that owned the matched transition / handler
        HsmState*       target   = nullptr;
        bool            consumed = false; // set true to stop event propagation

        void* owner() const; // machine user pointer
    };

    using HsmAction = std::function<void(HsmContext&)>;
    using HsmGuard  = std::function<bool(const HsmContext&)>; // true = allow

    // One transition edge. Source may be null for machine-level shared transitions
    // that apply from any active state listed in sharedSources (see HsmMachine).
    struct HsmTransition
    {
        HsmEventId        eventId = kHsmEventNone;
        HsmState*         source  = nullptr;
        HsmState*         target  = nullptr;
        HsmGuard          guard;
        HsmAction         action;
        HsmTransitionKind kind             = HsmTransitionKind::External;
        HsmHistory        forceHistory     = HsmHistory::None; // override target composite history
        bool              useTargetHistory = true;             // honor target's history mode when entering
    };

    inline bool alwaysTrue(const HsmContext&)
    {
        return true;
    }

} // namespace Dark::AI
