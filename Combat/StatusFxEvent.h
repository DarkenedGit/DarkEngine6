#pragma once

#include "ECS/Entity.h"

#include <cstdint>

namespace Dark::Combat
{

    enum class StatusFxOp : uint8_t
    {
        Applied = 0,
        Refreshed,
        Ticked,
        Expired,
        Cleared
    };

    struct StatusFxEvent
    {
        StatusFxOp op        = StatusFxOp::Applied;
        uint8_t    statusId  = 0;
        uint8_t    stacks    = 1;
        float      remaining = 0.f;
        float      magnitude = 0.f;
    };

    struct IStatusFx
    {
        void (*fn)(void* user, Entity target, const StatusFxEvent& ev) = nullptr;
        void* user                                                     = nullptr;
    };

    struct StatusEffectComponent;

    int drainStatusFx(StatusEffectComponent& st, StatusFxEvent* out, int cap);

} // namespace Dark::Combat
