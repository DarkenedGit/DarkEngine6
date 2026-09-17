#pragma once

#include "Combat/DamageEvent.h"
#include "Combat/Severity.h"
#include "Math/Vector3f.h"

namespace Dark
{
    class World;
    class Health;
    class HitReaction;
}

namespace Dark::Combat
{

    struct DefenseComponent;
    struct ArmorComponent;
    struct PoiseComponent;
    struct StatusEffectComponent;

    struct ResolveResult
    {
        bool     applied      = false;
        bool     blocked      = false;
        bool     parried      = false;
        bool     killed       = false;
        bool     iframe       = false;
        bool     filtered     = false;
        float    finalDamage  = 0.f;
        Severity severity     = Severity::Tick;
        bool     poiseBroke   = false;
        float    ccDuration   = 0.f;
    };

    class CombatSystem
    {
    public:
        CombatSystem() = default;

        ResolveResult resolve(World& world, const DamageEvent& ev);

        // Component-pointer path for unit tests (no World required).
        ResolveResult resolveDirect(const DamageEvent& ev,
                                    Health* health,
                                    HitReaction* hitReaction,
                                    DefenseComponent* defense,
                                    ArmorComponent* armor,
                                    PoiseComponent* poise,
                                    StatusEffectComponent* status,
                                    const Math::Vector3f* targetFacingFlat,
                                    float maxHpForSeverity);
    };

    Math::Vector3f facingFromYaw(float yawRad);
    bool           isInsideBlockArc(const Math::Vector3f& hitDir,
                                    const Math::Vector3f& facingFlat,
                                    float blockArcDeg);

} // namespace Dark::Combat
