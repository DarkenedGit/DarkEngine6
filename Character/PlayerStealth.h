#pragma once

namespace Dark
{
    // How loud and visible the player is. Crouch walk is quieter than a normal walk.
    // Holding still while crouched is quieter and harder to see than crouch walking.
    struct PlayerStealthSettings
    {
        float stillSpeed            = 0.45f; // planar speed below this counts as not moving
        float sightCrouchWalk       = 0.55f; // multiplies hunter sight range
        float sightCrouchStill      = 0.28f;
        float hearStand             = 4.0f;
        float hearWalk              = 14.0f;
        float hearSprint            = 24.0f;
        float hearCrouchWalk        = 5.5f;
        float hearCrouchStill       = 1.25f;
        float standoff              = 2.25f; // distance at which a hunter notices you even outside the cone
        float standoffCrouchWalk    = 1.15f;
        float standoffCrouchStill   = 0.55f;
        float standTargetHeight     = 0.50f;
        float crouchTargetHeight    = 0.22f;
    };

    struct PreySense
    {
        float sightRangeScale = 1.0f;
        float standoff        = 2.25f;
        float hearRange       = 0.0f;
        float targetHeight    = 0.50f;
    };

    inline PreySense preySenseFor(const PlayerStealthSettings& s, bool crouched, bool moving, bool sprinting)
    {
        PreySense out{};
        out.standoff        = s.standoff;
        out.sightRangeScale = 1.0f;
        out.targetHeight    = s.standTargetHeight;
        if (sprinting && moving)
            out.hearRange = s.hearSprint;
        else if (moving)
            out.hearRange = s.hearWalk;
        else
            out.hearRange = s.hearStand;

        if (!crouched)
            return out;

        out.targetHeight    = s.crouchTargetHeight;
        out.sightRangeScale = moving ? s.sightCrouchWalk : s.sightCrouchStill;
        out.hearRange       = moving ? s.hearCrouchWalk : s.hearCrouchStill;
        out.standoff        = moving ? s.standoffCrouchWalk : s.standoffCrouchStill;
        return out;
    }
} // namespace Dark
