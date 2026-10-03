#include <gtest/gtest.h>

#include "Character/SkillLimits.h"
#include "Character/SkillXp.h"
#include "Core/Log.h"
#include "ECS/World.h"

#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using namespace Dark;

namespace
{

    struct CapturedLog
    {
        LogLevel    level    = LogLevel::Info;
        LogCategory category = LogCategory::Core;
        std::string message;
    };

    std::vector<CapturedLog>* g_logs = nullptr;

    void captureLog(LogLevel level, LogCategory category, const char* message)
    {
        if (!g_logs)
            return;
        g_logs->push_back({ level, category, message ? message : "" });
    }

    class LogCapture
    {
    public:
        explicit LogCapture(std::vector<CapturedLog>& out) :
            m_out(out)
        {
            m_out.clear();
            g_logs = &m_out;
            Log::setCapture(&captureLog);
        }

        ~LogCapture()
        {
            Log::setCapture(nullptr);
            g_logs = nullptr;
        }

        LogCapture(const LogCapture&)            = delete;
        LogCapture& operator=(const LogCapture&) = delete;

    private:
        std::vector<CapturedLog>& m_out;
    };

    class CatalogGuard
    {
    public:
        CatalogGuard() :
            m_saved(skillCatalog())
        {
        }

        ~CatalogGuard()
        {
            skillCatalog() = m_saved;
        }

        CatalogGuard(const CatalogGuard&)            = delete;
        CatalogGuard& operator=(const CatalogGuard&) = delete;

    private:
        SkillCatalog m_saved;
    };

    int skillSlot(SkillId id)
    {
        return static_cast<int>(id);
    }

    std::string skillsJson(const char* enabled, const char* xpDtCap)
    {
        std::string text = R"({
  "version": 1,
  "enabled": )";
        text += enabled;
        text += R"(,
  "maxLevel": 10,
  "xpBase": 100,
  "xpDtCap": )";
        text += xpDtCap;
        text += R"(,
  "skills": [
    { "id": "shoot", "xpPerEvent": 5, "xpPerMetre": 0, "xpPerSecond": 0, "xpPerSecondPlayer": 0,
      "scalars": [
        { "id": "recoilScale", "atLevel1": 1.0, "atMaxLevel": 0.70, "min": 0.70, "max": 1.0 },
        { "id": "cooldownScale", "atLevel1": 1.0, "atMaxLevel": 0.85, "min": 0.85, "max": 1.0 }
      ] },
    { "id": "swim", "xpPerEvent": 0, "xpPerMetre": 0.50, "xpPerSecond": 0, "xpPerSecondPlayer": 0,
      "scalars": [ { "id": "swimScale", "atLevel1": 1.0, "atMaxLevel": 1.25, "min": 1.0, "max": 1.25 } ] },
    { "id": "run", "xpPerEvent": 0, "xpPerMetre": 0.25, "xpPerSecond": 0, "xpPerSecondPlayer": 0,
      "scalars": [ { "id": "runScale", "atLevel1": 1.0, "atMaxLevel": 1.20, "min": 1.0, "max": 1.20 } ] },
    { "id": "jump", "xpPerEvent": 6, "xpPerMetre": 0, "xpPerSecond": 0, "xpPerSecondPlayer": 0,
      "scalars": [ { "id": "jumpScale", "atLevel1": 1.0, "atMaxLevel": 1.15, "min": 1.0, "max": 1.15 } ] },
    { "id": "hear", "xpPerEvent": 0, "xpPerMetre": 0, "xpPerSecond": 2.0, "xpPerSecondPlayer": 1.5,
      "scalars": [ { "id": "hearScale", "atLevel1": 1.0, "atMaxLevel": 1.30, "min": 1.0, "max": 1.30 } ] },
    { "id": "see", "xpPerEvent": 0, "xpPerMetre": 0, "xpPerSecond": 2.0, "xpPerSecondPlayer": 1.5,
      "scalars": [
        { "id": "seeRangeScale", "atLevel1": 1.0, "atMaxLevel": 1.25, "min": 1.0, "max": 1.25 },
        { "id": "seeConeScale", "atLevel1": 1.0, "atMaxLevel": 1.15, "min": 1.0, "max": 1.15 }
      ] }
  ]
})";
        return text;
    }

    int countMessages(const std::vector<CapturedLog>& logs, LogLevel level, std::string_view snippet)
    {
        int count = 0;
        for (const CapturedLog& line : logs)
        {
            if (line.level == level && line.message.find(snippet) != std::string::npos)
                count += 1;
        }
        return count;
    }

    bool sawMessage(const std::vector<CapturedLog>& logs, LogLevel level, std::string_view snippet)
    {
        return countMessages(logs, level, snippet) > 0;
    }

    float curveTotal()
    {
        float total = 0.0f;
        for (int level = 1; level < skillCatalog().maxLevel(); ++level)
            total += skillCatalog().xpToNext(level);
        return total;
    }

} // namespace

TEST(SkillXp, ProfileAndComponentStartAtLevelOne)
{
    const SkillProfile profile;
    EXPECT_EQ(profile.grantMask, 0x3Fu);
    for (int i = 0; i < kSkillCount; ++i)
    {
        EXPECT_EQ(profile.level[i], 1);
        EXPECT_FLOAT_EQ(profile.xp[i], 0.0f);
    }

    const SkillComponent comp;
    EXPECT_FALSE(comp.seeArmed);
    EXPECT_FLOAT_EQ(comp.shootLock, 0.0f);
    EXPECT_EQ(comp.grantMask, profile.grantMask);
    for (int i = 0; i < kSkillCount; ++i)
    {
        const SkillId id = static_cast<SkillId>(i);
        EXPECT_EQ(comp.level(id), 1);
        EXPECT_FLOAT_EQ(comp.xp(id), 0.0f);
        EXPECT_TRUE(comp.allows(id));
    }
    EXPECT_EQ(comp.level(SkillId::Count), 1);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Count), 0.0f);
    EXPECT_FALSE(comp.allows(SkillId::Count));
    EXPECT_FALSE(comp.allows(static_cast<SkillId>(255)));
}

TEST(SkillXp, GrantCarriesLevelsAndCapsAtMax)
{
    const SkillDef* run = skillCatalog().find(SkillId::Run);
    ASSERT_NE(run, nullptr);
    const float first  = skillCatalog().xpToNext(1);
    const float second = skillCatalog().xpToNext(2);
    ASSERT_GT(first, 0.0f);
    ASSERT_GT(second, first);

    SkillComponent comp;
    EXPECT_TRUE(grantSkillXp(comp, SkillId::Run, first - 0.5f));
    EXPECT_EQ(comp.level(SkillId::Run), 1);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Run), first - 0.5f);

    EXPECT_TRUE(grantSkillXp(comp, SkillId::Run, 0.5f));
    EXPECT_EQ(comp.level(SkillId::Run), 2);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Run), 0.0f);

    EXPECT_TRUE(grantSkillXp(comp, SkillId::Run, second - 1.0f));
    EXPECT_EQ(comp.level(SkillId::Run), 2);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Run), second - 1.0f);

    EXPECT_TRUE(grantSkillXp(comp, SkillId::Run, 1.0f));
    EXPECT_EQ(comp.level(SkillId::Run), 3);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Run), 0.0f);

    SkillComponent multi;
    EXPECT_TRUE(grantSkillXp(multi, SkillId::Run, first + second + 10.0f));
    EXPECT_EQ(multi.level(SkillId::Run), 3);
    EXPECT_FLOAT_EQ(multi.xp(SkillId::Run), 10.0f);

    SkillComponent capped;
    EXPECT_TRUE(grantSkillXp(capped, SkillId::Swim, curveTotal()));
    EXPECT_EQ(capped.level(SkillId::Swim), skillCatalog().maxLevel());
    EXPECT_FLOAT_EQ(capped.xp(SkillId::Swim), 0.0f);
    EXPECT_FALSE(grantSkillXp(capped, SkillId::Swim, 25.0f));
    EXPECT_EQ(capped.level(SkillId::Swim), skillCatalog().maxLevel());
    EXPECT_FLOAT_EQ(capped.xp(SkillId::Swim), 0.0f);

    SkillComponent overshoot;
    overshoot.ranks[skillSlot(SkillId::Jump)].level = 8;
    const float fromEight                           = skillCatalog().xpToNext(8) + skillCatalog().xpToNext(9) + 40.0f;
    EXPECT_TRUE(grantSkillXp(overshoot, SkillId::Jump, fromEight));
    EXPECT_EQ(overshoot.level(SkillId::Jump), skillCatalog().maxLevel());
    EXPECT_FLOAT_EQ(overshoot.xp(SkillId::Jump), 0.0f);
}

TEST(SkillXp, GrantRejectsBadAmountsMasksAndBrokenLevels)
{
    SkillComponent comp;
    EXPECT_TRUE(grantSkillXp(comp, SkillId::Hear, 12.0f));
    EXPECT_FALSE(grantSkillXp(comp, SkillId::Hear, 0.0f));
    EXPECT_FALSE(grantSkillXp(comp, SkillId::Hear, -4.0f));
    EXPECT_FALSE(grantSkillXp(comp, SkillId::Hear, std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(grantSkillXp(comp, SkillId::Hear, std::numeric_limits<float>::infinity()));
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Hear), 12.0f);
    EXPECT_EQ(comp.level(SkillId::Hear), 1);

    EXPECT_FALSE(grantSkillXp(comp, SkillId::Count, 10.0f));
    EXPECT_FALSE(grantSkillXp(comp, static_cast<SkillId>(255), 10.0f));

    comp.grantMask = static_cast<uint8_t>(comp.grantMask & ~static_cast<uint8_t>(1u << skillSlot(SkillId::Run)));
    EXPECT_FALSE(comp.allows(SkillId::Run));
    EXPECT_FALSE(grantSkillXp(comp, SkillId::Run, 40.0f));
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Run), 0.0f);
    EXPECT_TRUE(grantSkillXp(comp, SkillId::Swim, 5.0f));
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Swim), 5.0f);

    SkillComponent empty;
    empty.grantMask = 0;
    for (int i = 0; i < kSkillCount; ++i)
        EXPECT_FALSE(grantSkillXp(empty, static_cast<SkillId>(i), 30.0f));
    EXPECT_FLOAT_EQ(empty.xp(SkillId::Shoot), 0.0f);
    EXPECT_FLOAT_EQ(empty.xp(SkillId::Run), 0.0f);

    MotorXpSample sprint;
    sprint.state        = PlayerMoveState::Grounded;
    sprint.sprint       = true;
    sprint.planarMetres = 20.0f;
    noteMotorXp(empty, sprint);
    noteShotXp(empty, true);
    notePlayerSenseXp(empty, true, true, true, 0.05f);
    noteNpcSenseXp(empty, true, true, 0.05f);
    noteNpcRunXp(empty, true, 20.0f);
    for (int i = 0; i < kSkillCount; ++i)
        EXPECT_FLOAT_EQ(empty.xp(static_cast<SkillId>(i)), 0.0f);

    SkillComponent broken;
    broken.ranks[skillSlot(SkillId::See)].level = 0;
    broken.ranks[skillSlot(SkillId::See)].xp    = 4.0f;
    EXPECT_FALSE(grantSkillXp(broken, SkillId::See, 100.0f));
    EXPECT_EQ(broken.level(SkillId::See), 0);
    EXPECT_FLOAT_EQ(broken.xp(SkillId::See), 4.0f);

    broken.ranks[skillSlot(SkillId::See)].level = 11;
    broken.ranks[skillSlot(SkillId::See)].xp    = 7.0f;
    EXPECT_FALSE(grantSkillXp(broken, SkillId::See, 100.0f));
    EXPECT_EQ(broken.level(SkillId::See), 11);
    EXPECT_FLOAT_EQ(broken.xp(SkillId::See), 7.0f);

    broken.ranks[skillSlot(SkillId::See)].level = 3;
    broken.ranks[skillSlot(SkillId::See)].xp    = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(grantSkillXp(broken, SkillId::See, 2.0f));
    EXPECT_EQ(broken.level(SkillId::See), 3);
    EXPECT_FLOAT_EQ(broken.xp(SkillId::See), 2.0f);
}

TEST(SkillXp, DisabledCatalogGrantsNothing)
{
    CatalogGuard guard;
    ASSERT_TRUE(skillCatalog().parseSkills(skillsJson("false", "0.1")));
    EXPECT_FALSE(skillCatalog().enabled());

    SkillComponent comp;
    EXPECT_FALSE(grantSkillXp(comp, SkillId::Run, 50.0f));
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Run), 0.0f);

    noteShotXp(comp, true);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Shoot), 0.0f);
    EXPECT_FLOAT_EQ(comp.shootLock, kShootGrantInterval);

    MotorXpSample swim;
    swim.state        = PlayerMoveState::Swimming;
    swim.planarMetres = 8.0f;
    swim.jumped       = true;
    swim.doubleJump   = true;
    noteMotorXp(comp, swim);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Swim), 0.0f);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Jump), 0.0f);

    notePlayerSenseXp(comp, true, true, true, 0.05f);
    noteNpcSenseXp(comp, true, true, 0.05f);
    noteNpcRunXp(comp, true, 8.0f);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::See), 0.0f);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Hear), 0.0f);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Run), 0.0f);

    comp.shootLock = 1.0f;
    tickSkill(comp, 1.0f);
    EXPECT_NEAR(comp.shootLock, 1.0f - skillCatalog().xpDtCap(), 1.0e-5f);
}

TEST(SkillXp, GrantLogsOncePerLevelGained)
{
    std::vector<CapturedLog> logs;
    LogCapture               capture(logs);

    SkillComponent comp;
    const float    partial = skillCatalog().xpToNext(1) * 0.5f;
    EXPECT_TRUE(grantSkillXp(comp, SkillId::Run, partial));
    EXPECT_EQ(countMessages(logs, LogLevel::Info, "reached level"), 0);

    const float rest = skillCatalog().xpToNext(1) + skillCatalog().xpToNext(2) - partial;
    EXPECT_TRUE(grantSkillXp(comp, SkillId::Run, rest));
    EXPECT_EQ(comp.level(SkillId::Run), 3);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Run), 0.0f);
    EXPECT_EQ(countMessages(logs, LogLevel::Info, "reached level"), 2);
    EXPECT_TRUE(sawMessage(logs, LogLevel::Info, "Skill 'run' reached level 2"));
    EXPECT_TRUE(sawMessage(logs, LogLevel::Info, "Skill 'run' reached level 3"));
    EXPECT_FALSE(sawMessage(logs, LogLevel::Info, "reached level 1"));
    EXPECT_FALSE(sawMessage(logs, LogLevel::Info, "reached level 4"));

    logs.clear();
    SkillComponent cap;
    cap.ranks[skillSlot(SkillId::Shoot)].level = 9;
    EXPECT_TRUE(grantSkillXp(cap, SkillId::Shoot, skillCatalog().xpToNext(9) + 15.0f));
    EXPECT_EQ(cap.level(SkillId::Shoot), 10);
    EXPECT_FLOAT_EQ(cap.xp(SkillId::Shoot), 0.0f);
    EXPECT_EQ(countMessages(logs, LogLevel::Info, "reached level"), 1);
    EXPECT_TRUE(sawMessage(logs, LogLevel::Info, "Skill 'shoot' reached level 10"));
    EXPECT_FALSE(sawMessage(logs, LogLevel::Info, "reached level 11"));
}

TEST(SkillXp, ResetToProfileClampsAndClearsLatch)
{
    SkillProfile profile;
    profile.grantMask                        = 0;
    profile.level[skillSlot(SkillId::Run)]   = 4;
    profile.xp[skillSlot(SkillId::Run)]      = 12.0f;
    profile.level[skillSlot(SkillId::Jump)]  = 0;
    profile.xp[skillSlot(SkillId::Jump)]     = 3.0f;
    profile.level[skillSlot(SkillId::Swim)]  = 99;
    profile.xp[skillSlot(SkillId::Swim)]     = 40.0f;
    profile.level[skillSlot(SkillId::Hear)]  = 3;
    profile.xp[skillSlot(SkillId::Hear)]     = std::numeric_limits<float>::quiet_NaN();
    profile.level[skillSlot(SkillId::See)]   = 2;
    profile.xp[skillSlot(SkillId::See)]      = -5.0f;
    profile.level[skillSlot(SkillId::Shoot)] = skillCatalog().maxLevel();
    profile.xp[skillSlot(SkillId::Shoot)]    = 25.0f;

    SkillComponent comp;
    comp.seeArmed                             = true;
    comp.shootLock                            = 0.4f;
    comp.ranks[skillSlot(SkillId::Run)].level = 7;
    comp.ranks[skillSlot(SkillId::Run)].xp    = 80.0f;
    comp.resetToProfile(profile);

    EXPECT_EQ(comp.grantMask, 0);
    EXPECT_FALSE(comp.seeArmed);
    EXPECT_FLOAT_EQ(comp.shootLock, 0.0f);
    EXPECT_EQ(comp.level(SkillId::Run), 4);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Run), 12.0f);
    EXPECT_EQ(comp.level(SkillId::Jump), 1);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Jump), 3.0f);
    EXPECT_EQ(comp.level(SkillId::Swim), skillCatalog().maxLevel());
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Swim), 0.0f);
    EXPECT_EQ(comp.level(SkillId::Hear), 3);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Hear), 0.0f);
    EXPECT_EQ(comp.level(SkillId::See), 2);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::See), 0.0f);
    EXPECT_EQ(comp.level(SkillId::Shoot), skillCatalog().maxLevel());
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Shoot), 0.0f);
    EXPECT_FALSE(comp.allows(SkillId::Run));
}

TEST(SkillXp, ApplySkillProfileCopiesAndLeavesUnknownUntouched)
{
    std::vector<CapturedLog> logs;
    LogCapture               capture(logs);

    World  world;
    Entity player = world.createEntity();
    ASSERT_TRUE(applySkillProfile(world, player, "player"));
    SkillComponent* sk = world.get<SkillComponent>(player);
    ASSERT_NE(sk, nullptr);
    EXPECT_EQ(sk->grantMask, skillCatalog().profile("player")->grantMask);
    EXPECT_EQ(sk->level(SkillId::Shoot), 1);
    EXPECT_FALSE(sk->seeArmed);

    EXPECT_TRUE(grantSkillXp(*sk, SkillId::Run, 15.0f));
    sk->seeArmed  = true;
    sk->shootLock = 0.2f;
    ASSERT_TRUE(applySkillProfile(world, player, "player"));
    EXPECT_FLOAT_EQ(sk->xp(SkillId::Run), 0.0f);
    EXPECT_EQ(sk->level(SkillId::Run), 1);
    EXPECT_FALSE(sk->seeArmed);
    EXPECT_FLOAT_EQ(sk->shootLock, 0.0f);

    Entity hunter = world.createEntity();
    ASSERT_TRUE(applySkillProfile(world, hunter, "hunter"));
    SkillComponent* hunterSk = world.get<SkillComponent>(hunter);
    ASSERT_NE(hunterSk, nullptr);
    const SkillProfile* hunterProfile = skillCatalog().profile("hunter");
    ASSERT_NE(hunterProfile, nullptr);
    EXPECT_EQ(hunterSk->grantMask, hunterProfile->grantMask);
    EXPECT_FALSE(hunterSk->allows(SkillId::Shoot));
    EXPECT_FALSE(hunterSk->allows(SkillId::Swim));
    EXPECT_FALSE(hunterSk->allows(SkillId::Jump));
    EXPECT_TRUE(hunterSk->allows(SkillId::Run));
    EXPECT_TRUE(hunterSk->allows(SkillId::Hear));
    EXPECT_TRUE(hunterSk->allows(SkillId::See));
    noteShotXp(*hunterSk, true);
    EXPECT_FLOAT_EQ(hunterSk->xp(SkillId::Shoot), 0.0f);
    EXPECT_FLOAT_EQ(hunterSk->shootLock, kShootGrantInterval);

    MotorXpSample jump;
    jump.jumped     = true;
    jump.doubleJump = true;
    noteMotorXp(*hunterSk, jump);
    EXPECT_FLOAT_EQ(hunterSk->xp(SkillId::Jump), 0.0f);

    Entity wolf = world.createEntity();
    ASSERT_TRUE(applySkillProfile(world, wolf, "wolf"));
    SkillComponent* wolfSk = world.get<SkillComponent>(wolf);
    ASSERT_NE(wolfSk, nullptr);
    EXPECT_EQ(wolfSk->grantMask, skillCatalog().profile("wolf")->grantMask);
    EXPECT_FALSE(grantSkillXp(*wolfSk, SkillId::Shoot, 5.0f));
    EXPECT_TRUE(wolfSk->allows(SkillId::Run));

    Entity kept = world.createEntity();
    world.emplace<SkillComponent>(kept);
    SkillComponent* keptSk = world.get<SkillComponent>(kept);
    ASSERT_NE(keptSk, nullptr);
    keptSk->ranks[skillSlot(SkillId::Run)].xp = 9.0f;
    logs.clear();
    EXPECT_FALSE(applySkillProfile(world, kept, "dragon"));
    EXPECT_FLOAT_EQ(keptSk->xp(SkillId::Run), 9.0f);
    EXPECT_TRUE(sawMessage(logs, LogLevel::Error, "unknown profile"));
    EXPECT_FALSE(applySkillProfile(world, kept, nullptr));
    EXPECT_FALSE(applySkillProfile(world, kept, ""));
    EXPECT_FLOAT_EQ(keptSk->xp(SkillId::Run), 9.0f);
    EXPECT_TRUE(sawMessage(logs, LogLevel::Error, "unknown profile"));

    Entity missing = world.createEntity();
    EXPECT_FALSE(applySkillProfile(world, missing, "nope"));
    EXPECT_FALSE(world.has<SkillComponent>(missing));

    Entity dead = world.createEntity();
    world.destroyEntity(dead);
    EXPECT_FALSE(applySkillProfile(world, dead, "player"));
    EXPECT_FALSE(world.has<SkillComponent>(dead));
    EXPECT_FALSE(applySkillProfile(world, Entity{}, "player"));
}

TEST(SkillXp, ShotLockGatesGrantsAndIsNotDecayedByTheNote)
{
    const SkillDef* shoot = skillCatalog().find(SkillId::Shoot);
    ASSERT_NE(shoot, nullptr);
    EXPECT_FLOAT_EQ(shoot->xpPerEvent, 5.0f);
    EXPECT_FLOAT_EQ(kShootGrantInterval, 0.125f);

    SkillComponent comp;
    noteShotXp(comp, false);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Shoot), 0.0f);
    EXPECT_FLOAT_EQ(comp.shootLock, 0.0f);

    noteShotXp(comp, true);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Shoot), shoot->xpPerEvent);
    EXPECT_FLOAT_EQ(comp.shootLock, kShootGrantInterval);

    tickSkill(comp, 0.05f);
    EXPECT_NEAR(comp.shootLock, 0.075f, 1.0e-5f);
    EXPECT_FLOAT_EQ(comp.shootLock, kShootGrantInterval - 0.05f);

    noteShotXp(comp, true);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Shoot), shoot->xpPerEvent);
    EXPECT_FLOAT_EQ(comp.shootLock, kShootGrantInterval - 0.05f);
    noteShotXp(comp, false);
    EXPECT_FLOAT_EQ(comp.shootLock, kShootGrantInterval - 0.05f);

    tickSkill(comp, 0.075f);
    EXPECT_FLOAT_EQ(comp.shootLock, 0.0f);
    noteShotXp(comp, true);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Shoot), shoot->xpPerEvent * 2.0f);
    EXPECT_FLOAT_EQ(comp.shootLock, kShootGrantInterval);

    SkillComponent nanLock;
    nanLock.shootLock = std::numeric_limits<float>::quiet_NaN();
    noteShotXp(nanLock, true);
    EXPECT_FLOAT_EQ(nanLock.xp(SkillId::Shoot), shoot->xpPerEvent);
    EXPECT_FLOAT_EQ(nanLock.shootLock, kShootGrantInterval);
}

TEST(SkillXp, TickSkillClampsDtAndDoesNotGrant)
{
    SkillComponent comp;
    comp.shootLock = 0.2f;
    tickSkill(comp, 0.0f);
    EXPECT_FLOAT_EQ(comp.shootLock, 0.2f);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Shoot), 0.0f);

    tickSkill(comp, -2.0f);
    EXPECT_FLOAT_EQ(comp.shootLock, 0.2f);
    tickSkill(comp, std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(comp.shootLock, 0.2f);
    tickSkill(comp, std::numeric_limits<float>::infinity());
    EXPECT_FLOAT_EQ(comp.shootLock, 0.2f);

    const float cap = skillCatalog().xpDtCap();
    EXPECT_FLOAT_EQ(cap, kSkillXpDtCap);
    tickSkill(comp, 1.0f);
    EXPECT_FLOAT_EQ(comp.shootLock, 0.2f - cap);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Shoot), 0.0f);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::See), 0.0f);

    comp.shootLock = -1.0f;
    tickSkill(comp, 0.0f);
    EXPECT_FLOAT_EQ(comp.shootLock, 0.0f);

    comp.shootLock = std::numeric_limits<float>::infinity();
    tickSkill(comp, 0.0f);
    EXPECT_FLOAT_EQ(comp.shootLock, 0.0f);
}

TEST(SkillXp, TimeGrantsUseCatalogCapWithoutASecondCeiling)
{
    CatalogGuard guard;
    ASSERT_TRUE(skillCatalog().parseSkills(skillsJson("true", "0.2")));
    const float cap = skillCatalog().xpDtCap();
    ASSERT_GT(cap, kSkillXpDtCap);
    ASSERT_LE(cap, 0.25f);

    SkillComponent comp;
    comp.shootLock = 1.0f;
    tickSkill(comp, cap);
    EXPECT_NEAR(comp.shootLock, 1.0f - cap, 1.0e-5f);
    tickSkill(comp, 5.0f);
    EXPECT_NEAR(comp.shootLock, 1.0f - cap - cap, 1.0e-5f);

    const SkillDef* see  = skillCatalog().find(SkillId::See);
    const SkillDef* hear = skillCatalog().find(SkillId::Hear);
    ASSERT_NE(see, nullptr);
    ASSERT_NE(hear, nullptr);
    notePlayerSenseXp(comp, true, true, true, 1.0f);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::See), see->xpPerSecondPlayer * cap);
    EXPECT_FLOAT_EQ(comp.xp(SkillId::Hear), hear->xpPerSecondPlayer * cap);
    EXPECT_NEAR(comp.xp(SkillId::See), 1.5f * cap, 1.0e-5f);

    SkillComponent npc;
    noteNpcSenseXp(npc, true, true, 0.5f);
    EXPECT_FLOAT_EQ(npc.xp(SkillId::See), see->xpPerSecond * cap);
    EXPECT_FLOAT_EQ(npc.xp(SkillId::Hear), hear->xpPerSecond * cap);
    EXPECT_NEAR(npc.xp(SkillId::See), 2.0f * cap, 1.0e-5f);
    EXPECT_GT(npc.xp(SkillId::See), see->xpPerSecond * kSkillXpDtCap);
}

TEST(SkillXp, MotorXpUsesStateMetresAndJumpPackets)
{
    const SkillDef* run  = skillCatalog().find(SkillId::Run);
    const SkillDef* swim = skillCatalog().find(SkillId::Swim);
    const SkillDef* jump = skillCatalog().find(SkillId::Jump);
    ASSERT_NE(run, nullptr);
    ASSERT_NE(swim, nullptr);
    ASSERT_NE(jump, nullptr);
    EXPECT_FLOAT_EQ(run->xpPerMetre, 0.25f);
    EXPECT_FLOAT_EQ(swim->xpPerMetre, 0.50f);
    EXPECT_FLOAT_EQ(jump->xpPerEvent, 6.0f);

    SkillComponent sprinting;
    MotorXpSample  sprint;
    sprint.state        = PlayerMoveState::Grounded;
    sprint.sprint       = true;
    sprint.planarMetres = 8.0f;
    noteMotorXp(sprinting, sprint);
    EXPECT_FLOAT_EQ(sprinting.xp(SkillId::Run), run->xpPerMetre * 8.0f);
    EXPECT_FLOAT_EQ(sprinting.xp(SkillId::Swim), 0.0f);

    SkillComponent walking;
    MotorXpSample  walk;
    walk.state        = PlayerMoveState::Grounded;
    walk.sprint       = false;
    walk.planarMetres = 10.0f;
    noteMotorXp(walking, walk);
    EXPECT_FLOAT_EQ(walking.xp(SkillId::Run), 0.0f);

    SkillComponent crouchSprint;
    MotorXpSample  crouch;
    crouch.state        = PlayerMoveState::Grounded;
    crouch.sprint       = true;
    crouch.crouch       = true;
    crouch.planarMetres = 8.0f;
    noteMotorXp(crouchSprint, crouch);
    EXPECT_FLOAT_EQ(crouchSprint.xp(SkillId::Run), 0.0f);

    SkillComponent crouchState;
    MotorXpSample  crouched;
    crouched.state        = PlayerMoveState::Crouch;
    crouched.sprint       = true;
    crouched.planarMetres = 8.0f;
    noteMotorXp(crouchState, crouched);
    EXPECT_FLOAT_EQ(crouchState.xp(SkillId::Run), 0.0f);

    SkillComponent dodging;
    MotorXpSample  dodge;
    dodge.state        = PlayerMoveState::Grounded;
    dodge.sprint       = true;
    dodge.dodged       = true;
    dodge.planarMetres = 8.0f;
    noteMotorXp(dodging, dodge);
    EXPECT_FLOAT_EQ(dodging.xp(SkillId::Run), 0.0f);

    SkillComponent dodgeState;
    MotorXpSample  dodgeMove;
    dodgeMove.state        = PlayerMoveState::Dodge;
    dodgeMove.sprint       = true;
    dodgeMove.planarMetres = 8.0f;
    noteMotorXp(dodgeState, dodgeMove);
    EXPECT_FLOAT_EQ(dodgeState.xp(SkillId::Run), 0.0f);

    SkillComponent busy;
    MotorXpSample  pounce;
    pounce.state        = PlayerMoveState::Grounded;
    pounce.sprint       = true;
    pounce.jumpBusy     = true;
    pounce.planarMetres = 8.0f;
    noteMotorXp(busy, pounce);
    EXPECT_FLOAT_EQ(busy.xp(SkillId::Run), 0.0f);

    SkillComponent air;
    MotorXpSample  falling;
    falling.state        = PlayerMoveState::Falling;
    falling.sprint       = true;
    falling.planarMetres = 8.0f;
    noteMotorXp(air, falling);
    EXPECT_FLOAT_EQ(air.xp(SkillId::Run), 0.0f);
    EXPECT_FLOAT_EQ(air.xp(SkillId::Swim), 0.0f);

    SkillComponent swimming;
    MotorXpSample  swimMove;
    swimMove.state        = PlayerMoveState::Swimming;
    swimMove.sprint       = true;
    swimMove.jumped       = true;
    swimMove.planarMetres = 4.0f;
    noteMotorXp(swimming, swimMove);
    EXPECT_FLOAT_EQ(swimming.xp(SkillId::Swim), swim->xpPerMetre * 4.0f);
    EXPECT_FLOAT_EQ(swimming.xp(SkillId::Run), 0.0f);
    EXPECT_FLOAT_EQ(swimming.xp(SkillId::Jump), jump->xpPerEvent);

    SkillComponent oneJump;
    MotorXpSample  hopped;
    hopped.state  = PlayerMoveState::Jumping;
    hopped.jumped = true;
    noteMotorXp(oneJump, hopped);
    EXPECT_FLOAT_EQ(oneJump.xp(SkillId::Jump), jump->xpPerEvent);
    EXPECT_FLOAT_EQ(oneJump.xp(SkillId::Run), 0.0f);

    SkillComponent doubleJump;
    MotorXpSample  doubled;
    doubled.state        = PlayerMoveState::Jumping;
    doubled.jumped       = true;
    doubled.doubleJump   = true;
    doubled.planarMetres = 6.0f;
    noteMotorXp(doubleJump, doubled);
    EXPECT_FLOAT_EQ(doubleJump.xp(SkillId::Jump), 12.0f);
    EXPECT_FLOAT_EQ(doubleJump.xp(SkillId::Jump), jump->xpPerEvent * 2.0f);
    EXPECT_FLOAT_EQ(doubleJump.xp(SkillId::Run), 0.0f);

    SkillComponent secondOnly;
    MotorXpSample  loneDouble;
    loneDouble.doubleJump = true;
    noteMotorXp(secondOnly, loneDouble);
    EXPECT_FLOAT_EQ(secondOnly.xp(SkillId::Jump), jump->xpPerEvent);

    SkillComponent far;
    MotorXpSample  longSprint;
    longSprint.state        = PlayerMoveState::Grounded;
    longSprint.sprint       = true;
    longSprint.planarMetres = 1000.0f;
    noteMotorXp(far, longSprint);
    const float gain = run->xpPerMetre * 1000.0f;
    EXPECT_EQ(far.level(SkillId::Run), 2);
    EXPECT_FLOAT_EQ(far.xp(SkillId::Run), gain - skillCatalog().xpToNext(1));

    SkillComponent badMetres;
    MotorXpSample  negative;
    negative.state        = PlayerMoveState::Grounded;
    negative.sprint       = true;
    negative.planarMetres = -5.0f;
    noteMotorXp(badMetres, negative);
    EXPECT_FLOAT_EQ(badMetres.xp(SkillId::Run), 0.0f);
    negative.planarMetres = std::numeric_limits<float>::quiet_NaN();
    noteMotorXp(badMetres, negative);
    EXPECT_FLOAT_EQ(badMetres.xp(SkillId::Run), 0.0f);
}

TEST(SkillXp, PlayerSenseUsesArmedLightAndForeignVoice)
{
    const SkillDef* see  = skillCatalog().find(SkillId::See);
    const SkillDef* hear = skillCatalog().find(SkillId::Hear);
    ASSERT_NE(see, nullptr);
    ASSERT_NE(hear, nullptr);
    EXPECT_FLOAT_EQ(see->xpPerSecondPlayer, 1.5f);
    EXPECT_FLOAT_EQ(hear->xpPerSecondPlayer, 1.5f);

    const float    dt = 0.05f;
    SkillComponent dark;
    dark.seeArmed = true;
    notePlayerSenseXp(dark, false, true, false, dt);
    EXPECT_FLOAT_EQ(dark.xp(SkillId::See), 0.0f);
    EXPECT_FLOAT_EQ(dark.xp(SkillId::Hear), 0.0f);

    SkillComponent unlatched;
    notePlayerSenseXp(unlatched, true, true, false, dt);
    EXPECT_FLOAT_EQ(unlatched.xp(SkillId::See), see->xpPerSecondPlayer * dt);
    EXPECT_TRUE(unlatched.seeArmed == false);

    SkillComponent lightOff;
    notePlayerSenseXp(lightOff, true, false, true, dt);
    EXPECT_FLOAT_EQ(lightOff.xp(SkillId::See), 0.0f);
    EXPECT_FLOAT_EQ(lightOff.xp(SkillId::Hear), hear->xpPerSecondPlayer * dt);

    SkillComponent both;
    notePlayerSenseXp(both, true, true, true, dt);
    EXPECT_FLOAT_EQ(both.xp(SkillId::See), see->xpPerSecondPlayer * dt);
    EXPECT_FLOAT_EQ(both.xp(SkillId::Hear), hear->xpPerSecondPlayer * dt);

    SkillComponent quiet;
    notePlayerSenseXp(quiet, true, true, false, dt);
    EXPECT_FLOAT_EQ(quiet.xp(SkillId::Hear), 0.0f);
    EXPECT_FLOAT_EQ(quiet.xp(SkillId::See), see->xpPerSecondPlayer * dt);

    SkillComponent negative;
    negative.ranks[skillSlot(SkillId::See)].xp = 4.0f;
    notePlayerSenseXp(negative, true, true, true, -1.0f);
    notePlayerSenseXp(negative, true, true, true, std::numeric_limits<float>::infinity());
    EXPECT_FLOAT_EQ(negative.xp(SkillId::See), 4.0f);
    EXPECT_FLOAT_EQ(negative.xp(SkillId::Hear), 0.0f);

    SkillComponent longDt;
    notePlayerSenseXp(longDt, true, true, true, 1.0f);
    const float cap = skillCatalog().xpDtCap();
    EXPECT_FLOAT_EQ(longDt.xp(SkillId::See), see->xpPerSecondPlayer * cap);
    EXPECT_FLOAT_EQ(longDt.xp(SkillId::Hear), hear->xpPerSecondPlayer * cap);
    EXPECT_LT(longDt.xp(SkillId::See), see->xpPerSecondPlayer);
}

TEST(SkillXp, NpcSenseAndRunUseGeometricHearAndQualifyingMove)
{
    const SkillDef* see  = skillCatalog().find(SkillId::See);
    const SkillDef* hear = skillCatalog().find(SkillId::Hear);
    const SkillDef* run  = skillCatalog().find(SkillId::Run);
    ASSERT_NE(see, nullptr);
    ASSERT_NE(hear, nullptr);
    ASSERT_NE(run, nullptr);
    EXPECT_FLOAT_EQ(see->xpPerSecond, 2.0f);
    EXPECT_FLOAT_EQ(hear->xpPerSecond, 2.0f);

    const float    dt = 0.05f;
    SkillComponent heardOnly;
    noteNpcSenseXp(heardOnly, false, true, dt);
    EXPECT_FLOAT_EQ(heardOnly.xp(SkillId::See), 0.0f);
    EXPECT_FLOAT_EQ(heardOnly.xp(SkillId::Hear), hear->xpPerSecond * dt);

    SkillComponent geometric;
    noteNpcSenseXp(geometric, true, false, dt);
    EXPECT_FLOAT_EQ(geometric.xp(SkillId::See), see->xpPerSecond * dt);
    EXPECT_FLOAT_EQ(geometric.xp(SkillId::Hear), 0.0f);
    EXPECT_NE(geometric.xp(SkillId::See), see->xpPerSecondPlayer * dt);

    SkillComponent neither;
    noteNpcSenseXp(neither, false, false, dt);
    EXPECT_FLOAT_EQ(neither.xp(SkillId::See), 0.0f);
    EXPECT_FLOAT_EQ(neither.xp(SkillId::Hear), 0.0f);

    SkillComponent capped;
    noteNpcSenseXp(capped, true, true, 10.0f);
    EXPECT_FLOAT_EQ(capped.xp(SkillId::See), see->xpPerSecond * skillCatalog().xpDtCap());
    EXPECT_FLOAT_EQ(capped.xp(SkillId::Hear), hear->xpPerSecond * skillCatalog().xpDtCap());

    SkillComponent negative;
    noteNpcSenseXp(negative, true, true, -0.25f);
    EXPECT_FLOAT_EQ(negative.xp(SkillId::See), 0.0f);

    SkillComponent wander;
    noteNpcRunXp(wander, false, 10.0f);
    EXPECT_FLOAT_EQ(wander.xp(SkillId::Run), 0.0f);

    SkillComponent chase;
    noteNpcRunXp(chase, true, 10.0f);
    EXPECT_FLOAT_EQ(chase.xp(SkillId::Run), run->xpPerMetre * 10.0f);

    SkillComponent stuck;
    noteNpcRunXp(stuck, true, 0.0f);
    noteNpcRunXp(stuck, true, -3.0f);
    noteNpcRunXp(stuck, true, std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(stuck.xp(SkillId::Run), 0.0f);

    SkillComponent masked;
    masked.grantMask = static_cast<uint8_t>(masked.grantMask & ~static_cast<uint8_t>(1u << skillSlot(SkillId::Run)));
    noteNpcRunXp(masked, true, 10.0f);
    EXPECT_FLOAT_EQ(masked.xp(SkillId::Run), 0.0f);
    EXPECT_TRUE(masked.allows(SkillId::Hear));
}

TEST(SkillXp, LocomotionMetresDropsSlideAndTinySteps)
{
    EXPECT_FLOAT_EQ(locomotionMetres(Math::Vector3f{ 1.0e-4f, 25.0f, 0.0f }, Math::Vector3f{ 3.0f, 0.0f, 4.0f }), 0.0f);
    EXPECT_FLOAT_EQ(locomotionMetres(Math::Vector3f{ 0.0f, 12.0f, 0.0f }, Math::Vector3f{ 0.0f, 0.0f, 0.0f }), 0.0f);
    EXPECT_FLOAT_EQ(locomotionMetres(Math::Vector3f{ 0.0f, 0.0f, 0.0f }, Math::Vector3f{ 4.0f, 0.0f, 1.0f }), 0.0f);

    const Math::Vector3f step{ 2.0f, 8.0f, -4.0f };
    EXPECT_FLOAT_EQ(locomotionMetres(step, step), 0.0f);
    EXPECT_FLOAT_EQ(locomotionMetres(Math::Vector3f{ 2.0f, 0.0f, 0.0f }, Math::Vector3f{ 2.0f, 5.0f, 0.5f }), 0.0f);

    const Math::Vector3f sprint{ 4.0f, 0.0f, 0.0f };
    const Math::Vector3f slide{ 1.0f, 2.0f, 3.0f };
    EXPECT_NEAR(locomotionMetres(sprint + slide, slide), 4.0f, 1.0e-5f);

    const float above = 1.0e-4f + 1.0e-4f;
    EXPECT_NEAR(locomotionMetres(Math::Vector3f{ above, 9.0f, 0.0f }, Math::Vector3f{ 0.0f, 3.0f, 0.0f }), above, 1.0e-6f);
    EXPECT_NEAR(locomotionMetres(Math::Vector3f{ 5.0f, 1.0f, 0.0f }, Math::Vector3f{ -2.0f, 4.0f, 0.0f }), 7.0f, 1.0e-5f);
    EXPECT_NEAR(locomotionMetres(Math::Vector3f{ 6.0f, 4.0f, 8.0f }, Math::Vector3f{ 3.0f, 1.0f, 4.0f }), 5.0f, 1.0e-5f);
}
