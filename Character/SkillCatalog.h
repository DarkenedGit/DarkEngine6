#pragma once

#include "Character/SkillId.h"
#include "Character/SkillLimits.h"

#include <string_view>

namespace Dark
{

    struct SkillScalarDef
    {
        SkillScalar id         = SkillScalar::RunScale;
        float       atLevel1   = 1.0f;
        float       atMaxLevel = 1.0f;
        float       minV       = 1.0f;
        float       maxV       = 1.0f;
    };

    struct SkillDef
    {
        SkillId        id                = SkillId::Run;
        float          xpPerEvent        = 0.0f;
        float          xpPerMetre        = 0.0f;
        float          xpPerSecond       = 0.0f;
        float          xpPerSecondPlayer = 0.0f;
        SkillScalarDef scalars[4]{};
        int            scalarCount = 0;
    };

    struct SkillProfile
    {
        // Bit i grants SkillId i.
        uint8_t grantMask = 0x3Fu;
        int     level[kSkillCount];
        float   xp[kSkillCount];

        // Value-initialized ints are 0, which is not a legal level.
        SkillProfile()
        {
            grantMask = 0x3Fu;
            for (int i = 0; i < kSkillCount; ++i)
            {
                level[i] = 1;
                xp[i]    = 0.0f;
            }
        }
    };

    class SkillCatalog
    {
    public:
        SkillCatalog();

        // nlohmann::json::parse(..., nullptr, false). On failure *this is unchanged.
        bool parseSkills(std::string_view jsonText);
        bool parseProfiles(std::string_view jsonText);
        bool loadFromContent();

        bool enabled() const
        {
            return m_enabled;
        }
        int maxLevel() const
        {
            return m_maxLevel;
        }
        float xpDtCap() const
        {
            return m_xpDtCap;
        }
        float xpToNext(int level) const;

        const SkillDef*     find(SkillId id) const;
        const SkillProfile* profile(std::string_view id) const;

    private:
        bool     m_enabled  = true;
        int      m_maxLevel = kSkillMaxLevel;
        float    m_xpBase   = 100.0f;
        float    m_xpDtCap  = kSkillXpDtCap;
        SkillDef m_skills[kSkillCount]{};
        // Fixed slots: 0 player, 1 hunter, 2 wolf.
        SkillProfile m_profiles[3]{};
        bool         m_loaded = false;
        bool         m_loadOk = true;
    };

    SkillCatalog& skillCatalog();

    float skillScalar(SkillId id, SkillScalar scalar, int level);

} // namespace Dark
