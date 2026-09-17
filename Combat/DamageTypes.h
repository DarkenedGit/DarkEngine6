#pragma once

#include <cstdint>
#include <cstddef>

namespace Dark::Combat
{

    enum class DamageType : uint8_t
    {
        Slash = 0,
        Pierce,
        Blunt,
        Fire,
        Frost,
        Lightning,
        Poison,
        Arcane,
        Holy,
        Shadow,
        True,
        Count
    };

    inline constexpr size_t kDamageTypeCount = static_cast<size_t>(DamageType::Count);

    inline bool isPhysical(DamageType t)
    {
        return t == DamageType::Slash || t == DamageType::Pierce || t == DamageType::Blunt;
    }

    inline bool isElemental(DamageType t)
    {
        return t == DamageType::Fire || t == DamageType::Frost || t == DamageType::Lightning || t == DamageType::Poison || t == DamageType::Arcane || t == DamageType::Holy || t == DamageType::Shadow;
    }

    inline bool isTrue(DamageType t)
    {
        return t == DamageType::True;
    }

} // namespace Dark::Combat
