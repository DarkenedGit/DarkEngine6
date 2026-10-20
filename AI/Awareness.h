#pragma once

#include "Math/Vector3f.h"

#include <cstdint>

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
        float searchSeconds       = 10.0f;
        float searchFillScale     = 2.5f;
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

} // namespace Dark::AI
