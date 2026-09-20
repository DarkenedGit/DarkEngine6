#pragma once

#include "Combat/AttackDef.h"
#include "Combat/DamageEvent.h"
#include "Weapons/Weapon.h"

namespace Dark::Combat
{

    // Thin Sandbox / delivery adapter: WeaponHit → DamageEvent.
    inline DamageEvent damageEventFromWeaponHit(const WeaponHit& hit, Entity source, const AttackDef* attack = nullptr)
    {
        DamageEvent ev{};
        ev.source      = source;
        ev.target      = hit.targetEntity;
        ev.amount      = hit.damage;
        ev.hitPoint    = hit.point;
        ev.hitDir      = hit.direction;
        ev.flags       = DamageFlags::CanBlock | DamageFlags::CanParry;
        if (attack)
        {
            ev.type        = attack->type;
            ev.poiseDamage = attack->poiseDamage;
            ev.armorPen    = attack->armorPen;
            if (attack->damage > 0.0f)
                ev.amount = attack->damage;
            if (attack->flags != 0)
                ev.flags = attack->flags;
            ev.teamSource      = attack->teamFilter;
            ev.statusId        = attack->statusId;
            ev.statusDuration  = attack->statusDuration;
            ev.statusMagnitude = attack->statusMagnitude;
        }
        else
        {
            ev.type        = DamageType::Slash;
            ev.poiseDamage = hit.damage * 0.75f;
        }
        return ev;
    }

} // namespace Dark::Combat
