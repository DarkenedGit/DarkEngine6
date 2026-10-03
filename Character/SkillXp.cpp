#include "Character/SkillXp.h"

#include "Core/Log.h"

#include <cmath>

namespace Dark
{
    namespace
    {

        int skillIndex(SkillId id)
        {
            const int index = static_cast<int>(id);
            if (index < 0 || index >= kSkillCount)
                return -1;
            return index;
        }

        // Catalog cap only. A non-finite or negative dt grants no time.
        float skillDt(float dt)
        {
            if (!std::isfinite(dt) || !(dt > 0.0f))
                return 0.0f;
            const float cap = skillCatalog().xpDtCap();
            if (!std::isfinite(cap) || !(cap > 0.0f))
                return 0.0f;
            return dt > cap ? cap : dt;
        }

    } // namespace

    int SkillComponent::level(SkillId id) const
    {
        const int index = skillIndex(id);
        if (index < 0)
            return 1;
        return ranks[index].level;
    }

    float SkillComponent::xp(SkillId id) const
    {
        const int index = skillIndex(id);
        if (index < 0)
            return 0.0f;
        return ranks[index].xp;
    }

    bool SkillComponent::allows(SkillId id) const
    {
        const int index = skillIndex(id);
        if (index < 0)
            return false;
        return (grantMask & static_cast<uint8_t>(1u << index)) != 0;
    }

    void SkillComponent::resetToProfile(const SkillProfile& profile)
    {
        grantMask          = profile.grantMask;
        const int maxLevel = skillCatalog().maxLevel();
        for (int i = 0; i < kSkillCount; ++i)
        {
            int level = profile.level[i];
            if (level < 1)
                level = 1;
            if (level > maxLevel)
                level = maxLevel;
            ranks[i].level = level;

            float xp = profile.xp[i];
            if (level >= maxLevel || !std::isfinite(xp) || xp < 0.0f)
                xp = 0.0f;
            ranks[i].xp = xp;
        }
        shootLock = 0.0f;
        seeArmed  = false;
    }

    bool grantSkillXp(SkillComponent& comp, SkillId id, float amount)
    {
        if (!std::isfinite(amount) || !(amount > 0.0f))
            return false;
        if (!skillCatalog().enabled())
            return false;

        const int index = skillIndex(id);
        if (index < 0 || !comp.allows(id))
            return false;

        SkillRank& rank     = comp.ranks[index];
        const int  maxLevel = skillCatalog().maxLevel();
        if (rank.level >= maxLevel)
            return false;
        // xpToNext is 0 below level 1, so the loop would not terminate.
        if (rank.level < 1)
            return false;

        float xp = rank.xp;
        if (!std::isfinite(xp) || xp < 0.0f)
            xp = 0.0f;
        xp += amount;

        int level = rank.level;
        while (level < maxLevel)
        {
            const float need = skillCatalog().xpToNext(level);
            if (!(need > 0.0f) || xp < need)
                break;
            xp -= need;
            level += 1;
            if (const char* name = skillIdName(id))
                DE_LOG_INFO(LogCategory::Core, "Skill '{}' reached level {}", name, level);
            if (level >= maxLevel)
            {
                xp = 0.0f;
                break;
            }
        }

        rank.level = level;
        rank.xp    = xp;
        return true;
    }

    void tickSkill(SkillComponent& comp, float dt)
    {
        const float step = skillDt(dt);
        float       lock = comp.shootLock;
        if (!std::isfinite(lock))
            lock = 0.0f;
        lock -= step;
        if (lock < 0.0f)
            lock = 0.0f;
        comp.shootLock = lock;
    }

    void noteMotorXp(SkillComponent& comp, const MotorXpSample& sample)
    {
        const bool swimming = sample.state == PlayerMoveState::Swimming;
        const bool running  = sample.state == PlayerMoveState::Grounded && sample.sprint && !sample.crouch && !sample.dodged && !sample.jumpBusy;
        if (swimming)
        {
            if (const SkillDef* swim = skillCatalog().find(SkillId::Swim))
                grantSkillXp(comp, SkillId::Swim, swim->xpPerMetre * sample.planarMetres);
        }
        else if (running)
        {
            if (const SkillDef* run = skillCatalog().find(SkillId::Run))
                grantSkillXp(comp, SkillId::Run, run->xpPerMetre * sample.planarMetres);
        }

        if (const SkillDef* jump = skillCatalog().find(SkillId::Jump))
        {
            if (sample.jumped)
                grantSkillXp(comp, SkillId::Jump, jump->xpPerEvent);
            if (sample.doubleJump)
                grantSkillXp(comp, SkillId::Jump, jump->xpPerEvent);
        }
    }

    void noteShotXp(SkillComponent& comp, bool projectileFired)
    {
        if (!projectileFired || comp.shootLock > 0.0f)
            return;
        const SkillDef* def    = skillCatalog().find(SkillId::Shoot);
        const float     amount = def ? def->xpPerEvent : 0.0f;
        grantSkillXp(comp, SkillId::Shoot, amount);
        comp.shootLock = kShootGrantInterval;
    }

    void noteNpcSenseXp(SkillComponent& comp, bool geometricSees, bool heardPrey, float dt)
    {
        const float step = skillDt(dt);
        if (!(step > 0.0f))
            return;
        if (geometricSees)
        {
            if (const SkillDef* see = skillCatalog().find(SkillId::See))
                grantSkillXp(comp, SkillId::See, see->xpPerSecond * step);
        }
        if (heardPrey)
        {
            if (const SkillDef* hear = skillCatalog().find(SkillId::Hear))
                grantSkillXp(comp, SkillId::Hear, hear->xpPerSecond * step);
        }
    }

    void notePlayerSenseXp(SkillComponent& comp, bool seeArmed, bool lightOn, bool foreignSpatialVoice, float dt)
    {
        const float step = skillDt(dt);
        if (!(step > 0.0f))
            return;
        if (seeArmed && lightOn)
        {
            if (const SkillDef* see = skillCatalog().find(SkillId::See))
                grantSkillXp(comp, SkillId::See, see->xpPerSecondPlayer * step);
        }
        if (foreignSpatialVoice)
        {
            if (const SkillDef* hear = skillCatalog().find(SkillId::Hear))
                grantSkillXp(comp, SkillId::Hear, hear->xpPerSecondPlayer * step);
        }
    }

    void noteNpcRunXp(SkillComponent& comp, bool qualifyingMove, float planarMetres)
    {
        if (!qualifyingMove)
            return;
        if (const SkillDef* run = skillCatalog().find(SkillId::Run))
            grantSkillXp(comp, SkillId::Run, run->xpPerMetre * planarMetres);
    }

    bool applySkillProfile(World& world, Entity e, const char* profileId)
    {
        if (!profileId || profileId[0] == '\0')
        {
            DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: unknown profile");
            return false;
        }
        const SkillProfile* profile = skillCatalog().profile(profileId);
        if (!profile)
        {
            DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: unknown profile '{}'", profileId);
            return false;
        }
        if (!world.alive(e))
            return false;

        SkillComponent* comp = world.get<SkillComponent>(e);
        if (!comp)
            comp = &world.emplace<SkillComponent>(e);
        comp->resetToProfile(*profile);
        return true;
    }

} // namespace Dark
