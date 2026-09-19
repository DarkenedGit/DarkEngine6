#pragma once

#include "Combat/DamageTypes.h"
#include "ECS/Entity.h"
#include "Math/Vector3f.h"

#include <cstdint>

namespace Dark::Combat
{

    // Bit flags for DamageEvent::flags.
    namespace DamageFlags
    {
        constexpr uint32_t None          = 0;
        constexpr uint32_t CanBlock      = 1u << 0;
        constexpr uint32_t CanParry      = 1u << 1;
        constexpr uint32_t IgnoresArmor  = 1u << 2;
        constexpr uint32_t DotTick       = 1u << 3;
        constexpr uint32_t SoftCc        = 1u << 4;
        constexpr uint32_t HardCc        = 1u << 5;
        constexpr uint32_t SameTeamOk    = 1u << 6;
        constexpr uint32_t Knockdown     = 1u << 7;
    }

    struct DamageEvent
    {
        Entity         source{};
        Entity         target{};
        DamageType     type        = DamageType::Slash;
        float          amount      = 0.f; // pre-mitigation
        float          poiseDamage = 0.f;
        float          armorPen    = 0.f; // 0..1 -> A_eff = A*(1-p)
        Math::Vector3f hitPoint{};
        Math::Vector3f hitDir{}; // attacker -> victim
        uint32_t       flags       = DamageFlags::CanBlock | DamageFlags::CanParry;
        uint8_t        teamSource  = 0;
        uint8_t        teamTarget  = 0;
        uint8_t        statusId    = 0; // optional status payload
        float          statusMagnitude = 0.f;
        float          statusDuration  = 0.f;
    };

} // namespace Dark::Combat
