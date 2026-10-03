#pragma once

#include <cstdint>

namespace Dark
{

    enum class SkillId : uint8_t
    {
        Shoot = 0,
        Swim,
        Run,
        Jump,
        Hear,
        See,
        Count
    };

    inline constexpr int kSkillCount = static_cast<int>(SkillId::Count);

    enum class SkillScalar : uint8_t
    {
        RecoilScale = 0,
        CooldownScale,
        SwimScale,
        RunScale,
        JumpScale,
        HearScale,
        SeeRangeScale,
        SeeConeScale,
        Count
    };

    static_assert(kSkillCount == 6);
    static_assert(kSkillCount <= 8);
    static_assert(static_cast<int>(SkillScalar::Count) == 8);

    inline const char* skillIdName(SkillId id)
    {
        switch (id)
        {
        case SkillId::Shoot:
            return "shoot";
        case SkillId::Swim:
            return "swim";
        case SkillId::Run:
            return "run";
        case SkillId::Jump:
            return "jump";
        case SkillId::Hear:
            return "hear";
        case SkillId::See:
            return "see";
        default:
            return nullptr;
        }
    }

} // namespace Dark
