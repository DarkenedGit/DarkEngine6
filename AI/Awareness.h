#pragma once

#include "Math/Vector3f.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Dark::AI
{
    enum class Alertness : uint8_t
    {
        Calm = 0,
        Suspicious,
        Investigating,
        Alert,
        Searching
    };

    struct AwarenessSettings
    {
        float fillNear            = 1.8f;
        float falloff             = 11.0f;
        float maxRange            = 150.0f;
        float focusHalfDeg        = 35.0f;
        float sideHalfDeg         = 90.0f;
        float sideRange           = 15.0f;
        float sideStill           = 0.05f;
        float sideMoving          = 0.70f;
        float nearRange           = 8.0f;
        float clearRate           = 2.5f;
        float clearExposure       = 0.35f;
        float clearBody           = 0.8f;
        float instantRange        = 4.0f;
        float instantExposure     = 0.5f;
        float instantExposureWary = 0.2f;
        float proximityRange      = 2.5f;
        float proximityRate       = 3.0f;
        float noticeExposure      = 0.12f;
        float noticeMotion        = 0.1f;
        float bodyRange           = 25.0f;
        float waryFillScale       = 1.6f;
        float waryDrainScale      = 0.4f;
        float waryTimeScale       = 1.5f;
        float lookBoost           = 1.5f;
        float evidenceGrace       = 0.4f;
        float drainCalm           = 0.2f;
        float drainCurious        = 0.08f;
        float suspiciousFillScale = 1.5f;
        float investigateAfter    = 1.0f;
        float investigateSeconds  = 9.0f;
        float alertLoseSeconds    = 2.0f;
        // Spotter follows the live player this long after losing sight. 0 disables.
        float alertTrackSeconds   = 1.0f;
        // Hold the lose timer until the guard reaches noticePos, for at most this long. 0 disables.
        float alertCatchSeconds   = 10.0f;
        float searchSeconds       = 10.0f;
        float searchFillScale     = 2.5f;

        // 3D radius for sharing an alert with nearby guards.
        float alertShareRadius = 30.0f;
        // Calm meter that starts the glance toward a cue.
        float reactMeter       = 0.15f;
        // Body-sample fraction that remains inside a conceal volume.
        float concealShown     = 0.15f;
        // Multiplies a guard's hear radius and noise range when a cube blocks the ray.
        float wallScale        = 0.5f;
        // Sample points above the player's feet, head to feet. Weights sum to 1.
        float bodyStandHeight[3]{ 0.75f, 0.45f, -0.2f };
        float bodyCrouchHeight[3]{ 0.3f, 0.1f, -0.25f };
        float bodyWeight[3]{ 0.4f, 0.4f, 0.2f };
        // Noise impulse is noiseBase + noiseFalloff * (1 - distance / range).
        float noiseBase    = 0.3f;
        float noiseFalloff = 0.5f;
        // Sandbox exposure multipliers for the player's stance, then footstep spacing in meters.
        float stanceCrouchWalk = 0.55f;
        float stanceCrouchStill = 0.4f;
        float stanceSprint      = 1.3f;
        float stanceWalk        = 1.0f;
        float stanceStand       = 0.8f;
        float stanceCamouflage  = 0.3f;
        float strideCrouch      = 0.65f;
        float strideSprint      = 2.2f;
        float strideWalk        = 1.4f;
    };

    struct AwarenessInput
    {
        bool           sees        = false;
        bool           primary     = false;
        float          distance    = 0.0f;
        float          exposure    = 1.0f;
        float          bodyShown   = 1.0f;
        float          motion      = 0.0f;
        bool           nearby      = false;
        bool           looking     = false;
        float          noise       = 0.0f;
        bool           touching    = false;
        Math::Vector3f playerPos{};
        Math::Vector3f noisePos{};
    };

    struct AwarenessState
    {
        float          meter     = 0.0f;
        Alertness      alertness = Alertness::Calm;
        float          stateTime = 0.0f;
        float          sinceCue  = 0.0f;
        Math::Vector3f noticePos{};
        bool           seesNow   = false;
        bool           heardOnly = false;
        bool           wary      = false;
    };

    bool stepAwareness(AwarenessState& state, const AwarenessSettings& settings, const AwarenessInput& in, float dt);
    void forceAlert(AwarenessState& state, const Math::Vector3f& where);
    void alertToInvestigate(AwarenessState& state, const Math::Vector3f& where);

    // Present numeric keys overlay inout. writtenMask bits mark the keys that applied.
    bool readAwarenessSettings(std::string_view jsonText, AwarenessSettings& inout, uint64_t* writtenMask = nullptr);
    bool writeAwarenessSettings(const AwarenessSettings& settings, uint64_t mask, std::string& jsonOut);
    void applyAwarenessMask(AwarenessSettings& dst, const AwarenessSettings& src, uint64_t mask);
    void sanitizeAwarenessSettings(AwarenessSettings& settings);
    // Overlays content/ai/awareness.json when that file is present, then sanitizes.
    bool loadAwarenessSettings(AwarenessSettings& inout);

} // namespace Dark::AI
