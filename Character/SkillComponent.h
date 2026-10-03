#pragma once

#include "Character/SkillCatalog.h"

#include <type_traits>

namespace Dark
{

    struct SkillRank
    {
        int   level = 1;
        float xp    = 0.0f;
    };

    struct SkillComponent
    {
        static constexpr const char* kTypeName = "Skill";

        SkillRank ranks[kSkillCount]{};
        uint8_t   grantMask = 0x3Fu;
        float     shootLock = 0.0f;
        bool      seeArmed  = false; // Latched by the flashlight edge, not by lightOn.

        int   level(SkillId id) const;
        float xp(SkillId id) const;
        bool  allows(SkillId id) const;
        void  resetToProfile(const SkillProfile& profile);
    };

    static_assert(std::is_trivially_copyable_v<SkillComponent>);

} // namespace Dark
