#pragma once

#include <cstdint>

namespace Dark::Audio
{
    enum class StimulusKind : uint8_t
    {
        None = 0,
        Footstep,
        Impact,
        Weapon,
        Voice
    };

    // Authored on a cue. Playing that cue is what reports it. Loudness is not a per-call argument.
    struct SoundStimulus
    {
        float        radius     = 0.0f;
        float        loudness   = 0.0f;
        StimulusKind kind       = StimulusKind::None;
        bool         positional = true;
    };

    inline bool stimulusAudible(const SoundStimulus& s)
    {
        return s.kind != StimulusKind::None && s.positional && s.radius > 0.0f && s.loudness > 0.0f;
    }

} // namespace Dark::Audio
