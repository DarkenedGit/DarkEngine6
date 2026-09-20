#pragma once

#include <cstdint>

namespace Dark::Combat
{

    enum class CcCategory : uint8_t
    {
        Stun = 0,
        Root,
        Fear,
        Knockdown,
        Count
    };

    inline constexpr int   kCcCategoryCount   = static_cast<int>(CcCategory::Count);
    inline constexpr float kCcDrWindowSeconds = 18.0f;
    inline constexpr float kHardCcMaxSeconds  = 3.0f;

    enum class StatusId : uint8_t
    {
        None      = 0,
        Stun      = 1,
        Root      = 2,
        Fear      = 3,
        Knockdown = 4,
        Poison    = 5,
        Bleed     = 6,
        Ignite    = 7,
        Chill     = 8,
        Shock     = 9,
        Count
    };

    inline constexpr int kStatusIdCount = static_cast<int>(StatusId::Count);

    inline bool statusIdIsCc(StatusId id)
    {
        const int v = static_cast<int>(id);
        return v >= static_cast<int>(StatusId::Stun) && v <= static_cast<int>(StatusId::Knockdown);
    }

    inline CcCategory ccCategoryForStatus(StatusId id)
    {
        if (!statusIdIsCc(id))
            return CcCategory::Count;
        return static_cast<CcCategory>(static_cast<int>(id) - static_cast<int>(StatusId::Stun));
    }

    inline StatusId statusIdForCc(CcCategory cat)
    {
        const int i = static_cast<int>(cat);
        if (i < 0 || i >= kCcCategoryCount)
            return StatusId::None;
        return static_cast<StatusId>(static_cast<int>(StatusId::Stun) + i);
    }

} // namespace Dark::Combat
