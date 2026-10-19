#include "AI/Awareness.h"

#include "Math/MathHelper.h"

#include <cmath>

namespace Dark::AI
{
    bool stepAwareness(AwarenessState& state, const AwarenessSettings& settings, const AwarenessInput& in, float dt)
    {
        const bool noticed = in.sees && in.exposure >= settings.noticeExposure;
        const bool feels   = in.nearby && in.motion > settings.noticeMotion;
        state.seesNow      = noticed || in.touching;
        if (state.seesNow || feels)
        {
            state.noticePos = in.playerPos;
            state.heardOnly = false;
        }
        else if (in.noise > 0.0f && state.alertness != Alertness::Alert)
        {
            state.noticePos = in.noisePos;
            state.heardOnly = state.alertness == Alertness::Calm || state.heardOnly;
        }
        const bool cue = state.seesNow || feels || in.noise > 0.0f;
        state.sinceCue = cue ? 0.0f : state.sinceCue + dt;

        float sight = 0.0f;
        if (noticed)
        {
            const float angleScale = in.primary ? 1.0f : settings.sideStill + settings.sideMoving * in.motion;
            sight                  = settings.fillNear * std::exp(-in.distance / settings.falloff) * angleScale * in.exposure;
            if (in.primary && in.distance < settings.nearRange && in.exposure >= settings.clearExposure && in.bodyShown >= settings.clearBody)
                sight = Math::Max(sight, settings.clearRate);
            if (in.looking)
                sight *= settings.lookBoost;
        }
        float felt = feels ? settings.proximityRate * in.motion : 0.0f;
        if (state.wary)
        {
            sight *= settings.waryFillScale;
            felt *= settings.waryFillScale;
        }
        const float drainScale = state.wary ? settings.waryDrainScale : 1.0f;
        const float timeScale  = state.wary ? settings.waryTimeScale : 1.0f;

        state.stateTime += dt;
        auto enter = [&](Alertness next) {
            state.alertness = next;
            state.stateTime = 0.0f;
            state.meter     = 0.0f;
        };

        const bool  tense      = state.alertness != Alertness::Calm || state.wary;
        const bool  pointBlank = noticed && in.primary && in.distance < settings.instantRange
                              && in.exposure >= (tense ? settings.instantExposureWary : settings.instantExposure);
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
                state.meter -= settings.drainCalm * drainScale * dt;
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
                state.meter -= settings.drainCurious * drainScale * dt;
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
            else if (state.alertness == Alertness::Investigating && state.stateTime >= settings.investigateSeconds * timeScale)
                enter(Alertness::Calm);
            else if (state.alertness == Alertness::Searching && state.stateTime >= settings.searchSeconds * timeScale)
            {
                state.alertness = Alertness::Investigating;
                state.stateTime = 0.0f;
            }
            break;
        }

        case Alertness::Alert:
            if (state.seesNow || feels)
                state.stateTime = 0.0f;
            else if (state.stateTime >= settings.alertLoseSeconds)
            {
                enter(Alertness::Searching);
                state.wary = true;
            }
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
