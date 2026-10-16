#pragma once

#include "Animation/AnimGraphComponent.h"
#include "Character/PlayerMotorComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Save/PersistentId.h"
#include "Save/ProgressComponents.h"

namespace Dark::Save
{

    struct SessionClockState
    {
        float cloudTime    = 0.0f;
        float envTimeOfDay = 0.0f;
        float waterTime    = 0.0f;
    };

    struct BodyLookState
    {
        float yaw          = 0.0f;
        float pitch        = 0.0f;
        bool  lightOn      = false;
        float lowerBodyYaw = 0.0f;
        bool  crouch       = false;
    };

    // simTimeSec follows cloud time. playTimeSec stays an app accumulator.
    inline void writeSessionClock(WorldClockComponent& clock, const SessionClockState& state)
    {
        clock.cloudTime    = state.cloudTime;
        clock.envTimeOfDay = state.envTimeOfDay;
        clock.waterTime    = state.waterTime;
        clock.simTimeSec   = state.cloudTime;
    }

    inline void readSessionClock(const WorldClockComponent& clock, SessionClockState& state)
    {
        state.cloudTime    = clock.cloudTime;
        state.envTimeOfDay = clock.envTimeOfDay;
        state.waterTime    = clock.waterTime;
    }

    // Copies the possessed look into the body. A missing component is left alone.
    inline void captureBodyLook(World& world, Entity body, const BodyLookState& state)
    {
        if (!body.valid())
            return;
        if (LookComponent* look = world.get<LookComponent>(body))
        {
            look->yaw     = state.yaw;
            look->pitch   = state.pitch;
            look->lightOn = state.lightOn;
        }
        if (AnimGraphComponent* ag = world.get<AnimGraphComponent>(body))
            ag->graph.player().setLowerBodyYaw(state.lowerBodyYaw);
    }

    // Crouch is the motor state, not a saved look field. No motor means not crouched.
    // Fields with no component keep the values the caller seeded.
    inline void restoreBodyLook(World& world, Entity body, BodyLookState& state)
    {
        state.crouch = false;
        if (!body.valid())
            return;
        if (const LookComponent* look = world.get<LookComponent>(body))
        {
            state.yaw     = look->yaw;
            state.pitch   = look->pitch;
            state.lightOn = look->lightOn;
        }
        if (const PlayerMotorComponent* motor = world.get<PlayerMotorComponent>(body))
            state.crouch = motor->motor.state() == PlayerMoveState::Crouch;
        if (const AnimGraphComponent* ag = world.get<AnimGraphComponent>(body))
            state.lowerBodyYaw = ag->graph.player().lowerBodyYaw();
    }

    // A live session is returned as-is. A missing or dead one is stamped "session".
    inline Entity ensureSessionEntity(World& world, Entity current)
    {
        if (current.valid() && world.alive(current))
            return current;
        Entity session = world.createEntity();
        world.emplace<TagComponent>(session, "Session");
        stampProceduralId(world, session, "session");
        world.emplace<WorldClockComponent>(session);
        world.emplace<AiClockComponent>(session);
        return session;
    }

} // namespace Dark::Save
