#pragma once

#include "Combat/DamageEvent.h"
#include "Combat/StatusDef.h"
#include "Combat/StatusFxEvent.h"
#include "Combat/StatusId.h"
#include "Core/Log.h"
#include "ECS/Entity.h"
#include "Math/MathHelper.h"

#include <cstdint>

namespace Dark::Combat
{

    struct StatusInstance
    {
        uint8_t    id        = 0;
        float      magnitude = 0.f;
        float      remaining = 0.f;
        bool       hard      = false;
        CcCategory category  = CcCategory::Stun; // Count = ailment
        uint8_t    stacks    = 1;
        float      tickAcc   = 0.f;
        Entity     source{};
    };

    struct CcDrState
    {
        int   applications = 0;
        float resetAt      = 0.f;
    };

    struct StatusEffectComponent
    {
        static constexpr const char* kTypeName    = "StatusEffect";
        static constexpr int         kMaxStatus   = 12;
        static constexpr int         kMaxFxEvents = 16;

        StatusInstance slots[kMaxStatus]{};
        int            count = 0;
        CcDrState      dr[kCcCategoryCount]{};
        float          now = 0.f;
        StatusFxEvent  fx[kMaxFxEvents]{};
        int            fxCount = 0;
        int            fxRead  = 0;

        void reset()
        {
            const bool active = count > 0 || fxCount > 0;
            count             = 0;
            now               = 0.f;
            fxCount           = 0;
            fxRead            = 0;
            for (int i = 0; i < kMaxStatus; ++i)
                slots[i] = StatusInstance{};
            for (int i = 0; i < kCcCategoryCount; ++i)
                dr[i] = CcDrState{};
            for (int i = 0; i < kMaxFxEvents; ++i)
                fx[i] = StatusFxEvent{};
            if (active)
                pushFx(StatusFxOp::Cleared, 0, 1, 0.f, 0.f);
        }

        void tick(float dt)
        {
            if (dt < 0.0f)
                dt = 0.0f;
            now += dt;
            for (int i = 0; i < kCcCategoryCount; ++i)
            {
                if (dr[i].applications > 0 && now >= dr[i].resetAt)
                {
                    dr[i].applications = 0;
                    dr[i].resetAt      = 0.f;
                }
            }
            for (int i = 0; i < count; ++i)
            {
                slots[i].remaining -= dt;
                const StatusDef* def = statusDef(static_cast<StatusId>(slots[i].id));
                if (def && def->tickDamage > 0.0f && def->tickInterval > 0.0f)
                    slots[i].tickAcc += dt;
            }
        }

        void compactExpired()
        {
            for (int i = 0; i < count;)
            {
                if (slots[i].remaining > 0.0f)
                {
                    ++i;
                    continue;
                }
                pushFx(StatusFxOp::Expired, slots[i].id, slots[i].stacks, 0.f, slots[i].magnitude);
                slots[i] = slots[count - 1];
                --count;
            }
        }

        // 1st = 100%, 2nd = 50%, 3rd+ = immune within the DR window.
        float applyCc(CcCategory cat, float duration, bool hard, uint8_t statusId, float magnitude, Entity source = {})
        {
            if (duration <= 0.0f)
                return 0.0f;
            const int idx = static_cast<int>(cat);
            if (idx < 0 || idx >= kCcCategoryCount)
                return 0.0f;

            if (dr[idx].applications == 0)
                dr[idx].resetAt = now + kCcDrWindowSeconds;

            const int n   = dr[idx].applications;
            float     mul = 1.0f;
            if (n == 1)
                mul = 0.5f;
            else if (n >= 2)
                mul = 0.0f;

            ++dr[idx].applications;
            dr[idx].resetAt = now + kCcDrWindowSeconds;

            float eff = duration * mul;
            if (hard)
                eff = Math::Min(eff, kHardCcMaxSeconds);
            if (eff <= 0.0f)
                return 0.0f;

            const uint8_t storedId = statusId != 0 ? statusId : static_cast<uint8_t>(statusIdForCc(cat));

            for (int i = 0; i < count; ++i)
            {
                if (slots[i].category == cat)
                {
                    if (eff > slots[i].remaining)
                    {
                        slots[i].remaining = eff;
                        slots[i].magnitude = magnitude;
                        slots[i].hard      = hard;
                        slots[i].id        = storedId;
                        slots[i].source    = source;
                        pushFx(StatusFxOp::Refreshed, storedId, slots[i].stacks, eff, magnitude);
                    }
                    return eff;
                }
            }

            if (count >= kMaxStatus)
                return eff;
            slots[count++]          = StatusInstance{ storedId, magnitude, eff, hard, cat };
            slots[count - 1].source = source;
            pushFx(StatusFxOp::Applied, storedId, slots[count - 1].stacks, eff, magnitude);
            return eff;
        }

        float applyStatus(StatusId id, float duration, float magnitude, Entity source = {})
        {
            const StatusDef* def = statusDef(id);
            if (!def)
            {
                if (id != StatusId::None)
                {
                    static bool s_loggedUnknown = false;
                    if (!s_loggedUnknown)
                    {
                        s_loggedUnknown = true;
                        DE_LOG_WARN("StatusEffect: unknown statusId {}", static_cast<int>(id));
                    }
                }
                return 0.0f;
            }

            const float dur = duration > 0.0f ? duration : def->defaultDuration;
            const float mag = magnitude != 0.0f ? magnitude : def->defaultMagnitude;
            if (dur <= 0.0f)
                return 0.0f;

            if (def->ccCategory != CcCategory::Count)
                return applyCc(def->ccCategory, dur, def->hardCc, static_cast<uint8_t>(id), mag, source);

            const uint8_t sid = static_cast<uint8_t>(id);
            for (int i = 0; i < count; ++i)
            {
                if (slots[i].id != sid)
                    continue;
                switch (def->stack)
                {
                case StatusStackRule::RefreshDurationMaxMag:
                    slots[i].remaining = dur;
                    slots[i].magnitude = Math::Max(slots[i].magnitude, mag);
                    slots[i].stacks    = 1;
                    slots[i].source    = source;
                    pushFx(StatusFxOp::Refreshed, sid, slots[i].stacks, dur, slots[i].magnitude);
                    return dur;
                case StatusStackRule::StackCount:
                    slots[i].stacks    = static_cast<uint8_t>(Math::Min(static_cast<int>(slots[i].stacks) + 1, static_cast<int>(def->maxStacks)));
                    slots[i].remaining = dur;
                    slots[i].magnitude = mag;
                    slots[i].source    = source;
                    pushFx(StatusFxOp::Refreshed, sid, slots[i].stacks, dur, mag);
                    return dur;
                case StatusStackRule::ExclusiveRefresh:
                    slots[i].stacks    = 1;
                    slots[i].remaining = dur;
                    slots[i].magnitude = mag;
                    slots[i].source    = source;
                    pushFx(StatusFxOp::Refreshed, sid, slots[i].stacks, dur, mag);
                    return dur;
                default:
                {
                    static bool s_loggedRule = false;
                    if (!s_loggedRule)
                    {
                        s_loggedRule = true;
                        DE_LOG_WARN("StatusEffect: ailment '{}' uses CC stack rule", def->name);
                    }
                    return 0.0f;
                }
                }
            }

            if (count >= kMaxStatus)
            {
                static bool s_loggedOverflow = false;
                if (!s_loggedOverflow)
                {
                    s_loggedOverflow = true;
                    DE_LOG_WARN("StatusEffect: status slots full, refusing ailment");
                }
                return 0.0f;
            }

            StatusInstance inst{};
            inst.id        = sid;
            inst.magnitude = mag;
            inst.remaining = dur;
            inst.hard      = false;
            inst.category  = CcCategory::Count;
            inst.stacks    = 1;
            inst.tickAcc   = 0.f;
            inst.source    = source;
            slots[count++] = inst;
            pushFx(StatusFxOp::Applied, sid, 1, dur, mag);
            return dur;
        }

        bool hasHardCc() const
        {
            for (int i = 0; i < count; ++i)
            {
                if (slots[i].hard && slots[i].remaining > 0.0f)
                    return true;
            }
            return false;
        }

        bool hasCategory(CcCategory cat) const
        {
            for (int i = 0; i < count; ++i)
            {
                if (slots[i].category == cat && slots[i].remaining > 0.0f)
                    return true;
            }
            return false;
        }

        bool knockedDown() const
        {
            return hasCategory(CcCategory::Knockdown);
        }

        bool has(StatusId id) const
        {
            const uint8_t sid = static_cast<uint8_t>(id);
            for (int i = 0; i < count; ++i)
            {
                if (slots[i].id == sid && slots[i].remaining > 0.0f)
                    return true;
            }
            return false;
        }

        int stacks(StatusId id) const
        {
            const uint8_t sid = static_cast<uint8_t>(id);
            for (int i = 0; i < count; ++i)
            {
                if (slots[i].id == sid && slots[i].remaining > 0.0f)
                    return static_cast<int>(slots[i].stacks);
            }
            return 0;
        }

        float remaining(StatusId id) const
        {
            const uint8_t sid = static_cast<uint8_t>(id);
            for (int i = 0; i < count; ++i)
            {
                if (slots[i].id == sid && slots[i].remaining > 0.0f)
                    return slots[i].remaining;
            }
            return 0.0f;
        }

        float moveSpeedScale() const
        {
            float s = 1.0f;
            for (int i = 0; i < count; ++i)
            {
                if (slots[i].remaining <= 0.0f)
                    continue;
                if (static_cast<StatusId>(slots[i].id) != StatusId::Chill)
                    continue;
                const float mag = Math::Clamp(slots[i].magnitude, 0.25f, 1.0f);
                if (mag < s)
                    s = mag;
            }
            return s;
        }

        int harvestDot(DamageEvent* out, int cap, Entity self)
        {
            int written = 0;
            if (out && cap > 0)
            {
                for (int i = 0; i < count; ++i)
                {
                    const StatusDef* def = statusDef(static_cast<StatusId>(slots[i].id));
                    if (!def || def->tickDamage <= 0.0f || def->tickInterval <= 0.0f)
                        continue;
                    int emitted = 0;
                    // Cap 4 ticks per harvest so a hitch cannot fill the 16-event ring.
                    while (slots[i].tickAcc >= def->tickInterval && written < cap && emitted < 4)
                    {
                        DamageEvent ev{};
                        ev.source      = slots[i].source;
                        ev.target      = self;
                        ev.type        = def->damageType;
                        ev.amount      = def->tickDamage * (def->tickScalesWithStacks ? static_cast<float>(slots[i].stacks) : 1.f);
                        ev.flags       = DamageFlags::DotTick;
                        ev.statusId    = 0;
                        ev.hitDir      = Math::Vector3f{ 0.f, 1.f, 0.f };
                        out[written++] = ev;
                        slots[i].tickAcc -= def->tickInterval;
                        ++emitted;
                        pushFx(StatusFxOp::Ticked, slots[i].id, slots[i].stacks, slots[i].remaining, slots[i].magnitude);
                    }
                }
            }
            compactExpired();
            return written;
        }

    private:
        void pushFx(StatusFxOp op, uint8_t statusId, uint8_t stacks, float remaining, float magnitude)
        {
            StatusFxEvent ev{};
            ev.op           = op;
            ev.statusId     = statusId;
            ev.stacks       = stacks;
            ev.remaining    = remaining;
            ev.magnitude    = magnitude;
            const int write = (fxRead + fxCount) % kMaxFxEvents;
            fx[write]       = ev;
            if (fxCount < kMaxFxEvents)
                ++fxCount;
            else
                fxRead = (fxRead + 1) % kMaxFxEvents;
        }
    };

    inline int drainStatusFx(StatusEffectComponent& st, StatusFxEvent* out, int cap)
    {
        int n = 0;
        if (out && cap > 0)
        {
            n = st.fxCount < cap ? st.fxCount : cap;
            for (int i = 0; i < n; ++i)
                out[i] = st.fx[(st.fxRead + i) % StatusEffectComponent::kMaxFxEvents];
        }
        st.fxCount = 0;
        st.fxRead  = 0;
        return n;
    }

} // namespace Dark::Combat
