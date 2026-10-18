#include "AI/Awareness.h"

#include "Math/MathHelper.h"

#include <cmath>

namespace Dark::AI
{
    bool stepAwareness(AwarenessState& state, const AwarenessSettings& settings, const AwarenessInput& in, float dt)
    {
        state.seesNow = in.sees || in.touching;
        if (state.seesNow || in.nearby)
        {
            state.noticePos = in.playerPos;
            state.heardOnly = false;
        }
        else if (in.noise > 0.0f && state.alertness != Alertness::Alert)
        {
            state.noticePos = in.noisePos;
            state.heardOnly = state.alertness == Alertness::Calm || state.heardOnly;
        }
        const bool cue = state.seesNow || in.nearby || in.noise > 0.0f;
        state.sinceCue = cue ? 0.0f : state.sinceCue + dt;

        float sight = 0.0f;
        if (in.sees)
        {
            const float angleScale = in.primary ? 1.0f : settings.sideStill + settings.sideMoving * in.motion;
            sight                  = settings.fillNear * std::exp(-in.distance / settings.falloff) * angleScale * in.exposure;
            if (in.primary && in.distance < settings.nearRange && in.exposure >= settings.clearExposure && in.bodyShown >= settings.clearBody)
                sight = Math::Max(sight, settings.clearRate);
            if (in.looking)
                sight *= settings.lookBoost;
        }
        const float felt = in.nearby ? settings.proximityRate * in.motion : 0.0f;

        state.stateTime += dt;
        auto enter = [&](Alertness next) {
            state.alertness = next;
            state.stateTime = 0.0f;
            state.meter     = 0.0f;
        };

        const bool  wary       = state.alertness != Alertness::Calm;
        const bool  pointBlank = in.sees && in.primary && in.distance < settings.instantRange
                              && in.exposure >= (wary ? settings.instantExposureWary : settings.instantExposure);
        if ((in.touching || pointBlank) && state.alertness != Alertness::Alert)
        {
            enter(Alertness::Alert);
            return true;
        }

        const bool draining = state.sinceCue > settings.evidenceGrace;
        switch (state.alertness)
        {
        case Alertness::Calm:
            state.meter += Math::Max(sight, felt) * dt + in.noise;
            if (draining)
                state.meter -= settings.drainCalm * dt;
            if (state.meter >= 1.0f)
                enter(Alertness::Suspicious);
            break;

        case Alertness::Suspicious:
        case Alertness::Investigating:
        case Alertness::Searching:
        {
            const float yellow = Math::Max(sight, felt);
            if (yellow > 0.0f)
                state.meter += yellow * (state.alertness == Alertness::Searching ? settings.searchFillScale : settings.suspiciousFillScale) * dt;
            else if (draining)
                state.meter -= settings.drainCurious * dt;
            if (cue)
                state.stateTime = Math::Min(state.stateTime, settings.investigateAfter);
            if (state.meter >= 1.0f)
            {
                enter(Alertness::Alert);
                return true;
            }
            if (state.alertness == Alertness::Suspicious && state.stateTime >= settings.investigateAfter)
            {
                state.alertness = Alertness::Investigating;
                state.stateTime = 0.0f;
            }
            else if (state.alertness == Alertness::Investigating && state.stateTime >= settings.investigateSeconds)
                enter(Alertness::Calm);
            else if (state.alertness == Alertness::Searching && state.stateTime >= settings.searchSeconds)
            {
                state.alertness = Alertness::Investigating;
                state.stateTime = 0.0f;
            }
            break;
        }

        case Alertness::Alert:
            if (state.seesNow || in.nearby)
                state.stateTime = 0.0f;
            else if (state.stateTime >= settings.alertLoseSeconds)
                enter(Alertness::Searching);
            break;
        }
        state.meter = Math::Clamp(state.meter, 0.0f, 1.0f);
        return false;
    }

    void forceAlert(AwarenessState& state, const Math::Vector3f& where)
    {
        state.noticePos = where;
        state.heardOnly = false;
        state.seesNow   = true;
        state.alertness = Alertness::Alert;
        state.stateTime = 0.0f;
        state.meter     = 0.0f;
    }

    void alertToInvestigate(AwarenessState& state, const Math::Vector3f& where)
    {
        state.noticePos = where;
        state.heardOnly = true;
        if (state.alertness == Alertness::Calm)
        {
            state.alertness = Alertness::Investigating;
            state.stateTime = 0.0f;
            state.meter     = 0.0f;
        }
    }

} // namespace Dark::AI
