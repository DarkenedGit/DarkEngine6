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

namespace Dark::Math
{
    class Quaternion;
}

namespace Dark::Combat
{

    struct DefenseComponent;
    struct ArmorComponent;
    struct ArmorPiecesComponent;
    struct ArmorBreak;
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

    // A blocked, parried, iframe, or filtered result did not land on the body.
    inline bool partHitLanded(const ResolveResult& r)
    {
        return r.applied && !r.blocked && !r.parried && !r.iframe && !r.filtered;
    }

    class CombatSystem
    {
    public:
        CombatSystem() = default;

        ResolveResult resolve(World& world, const DamageEvent& ev, ArmorBreak* armorBreak = nullptr);

        // Component-pointer path for unit tests (no World required).
        // knockDirection, when set, is the slide passed to HitReaction. hitDir stays the attack direction.
        // Armor pieces are spent here, before mitigation, and only when the hit is not blocked.
        ResolveResult resolveDirect(const DamageEvent& ev,
                                    Health* health,
                                    HitReaction* hitReaction,
                                    DefenseComponent* defense,
                                    ArmorComponent* armor,
                                    PoiseComponent* poise,
                                    StatusEffectComponent* status,
                                    const Math::Vector3f* targetFacingFlat,
                                    float maxHpForSeverity,
                                    const Math::Vector3f* knockDirection = nullptr,
                                    ArmorPiecesComponent* armorPieces = nullptr,
                                    const Math::Vector3f* armorOwnerPos = nullptr,
                                    const Math::Quaternion* armorOwnerRot = nullptr,
                                    ArmorBreak* armorBreak = nullptr);
    };

    Math::Vector3f facingFromYaw(float yawRad);
    bool           isInsideBlockArc(const Math::Vector3f& hitDir,
                                    const Math::Vector3f& facingFlat,
                                    float blockArcDeg);

} // namespace Dark::Combat
