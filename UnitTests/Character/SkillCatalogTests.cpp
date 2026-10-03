#include <gtest/gtest.h>

#include "Character/SkillCatalog.h"
#include "Character/SkillId.h"
#include "Character/SkillLimits.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
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

    class SingletonGuard
    {
    public:
        SingletonGuard() :
            m_saved(skillCatalog())
        {
        }

        ~SingletonGuard()
        {
            skillCatalog() = m_saved;
        }

        SingletonGuard(const SingletonGuard&)            = delete;
        SingletonGuard& operator=(const SingletonGuard&) = delete;

    private:
        SkillCatalog m_saved;
    };

    struct FileSlot
    {
        std::filesystem::path original;
        std::filesystem::path backup;
        bool                  hidden   = false;
        bool                  replaced = false;
    };

    class ContentGuard
    {
    public:
        ~ContentGuard()
        {
            restoreAll();
        }

        ContentGuard(const ContentGuard&)            = delete;
        ContentGuard& operator=(const ContentGuard&) = delete;
        ContentGuard()                               = default;

        bool hideAll(const char* fileName)
        {
            bool any = false;
            for (const std::filesystem::path& root : contentRootCandidates())
            {
                if (root.empty())
                    continue;
                const std::filesystem::path path = root / "skills" / fileName;
                std::error_code             ec;
                if (!std::filesystem::is_regular_file(path, ec) || ec)
                    continue;
                FileSlot slot;
                slot.original = path;
                slot.backup   = path;
                slot.backup += ".skillcatalogbak";
                std::filesystem::remove(slot.backup, ec);
                std::filesystem::rename(slot.original, slot.backup, ec);
                if (ec)
                    return false;
                slot.hidden = true;
                m_slots.push_back(std::move(slot));
                any = true;
            }
            return any;
        }

        bool replaceFirst(const char* fileName, std::string_view text)
        {
            for (const std::filesystem::path& root : contentRootCandidates())
            {
                if (root.empty())
                    continue;
                const std::filesystem::path path = root / "skills" / fileName;
                std::error_code             ec;
                if (!std::filesystem::is_regular_file(path, ec) || ec)
                    continue;
                FileSlot slot;
                slot.original = path;
                slot.backup   = path;
                slot.backup += ".skillcatalogbak";
                std::filesystem::remove(slot.backup, ec);
                std::filesystem::rename(slot.original, slot.backup, ec);
                if (ec)
                    return false;
                slot.hidden = true;
                if (!writeFile(slot.original, text))
                {
                    std::filesystem::rename(slot.backup, slot.original, ec);
                    return false;
                }
                slot.replaced = true;
                m_slots.push_back(std::move(slot));
                return true;
            }
            return false;
        }

        bool writeOverFirstHidden(const char* fileName, std::string_view text)
        {
            for (FileSlot& slot : m_slots)
            {
                if (!slot.hidden || slot.original.filename() != fileName)
                    continue;
                if (!writeFile(slot.original, text))
                    return false;
                slot.replaced = true;
                return true;
            }
            return false;
        }

        void restoreAll()
        {
            std::error_code ec;
            for (auto it = m_slots.rbegin(); it != m_slots.rend(); ++it)
            {
                if (it->replaced || std::filesystem::exists(it->original, ec))
                    std::filesystem::remove(it->original, ec);
                if (it->hidden)
                    std::filesystem::rename(it->backup, it->original, ec);
            }
            m_slots.clear();
        }

    private:
        static bool writeFile(const std::filesystem::path& path, std::string_view text)
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out)
                return false;
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            return static_cast<bool>(out);
        }

        std::vector<FileSlot> m_slots;
    };

    struct ShippedText
    {
        std::string skills;
        std::string profiles;
    };

    std::string readShippedFile(const char* fileName)
    {
        for (const std::filesystem::path& root : contentRootCandidates())
        {
            if (root.empty())
                continue;
            const std::filesystem::path path = root / "skills" / fileName;
            std::error_code             ec;
            if (!std::filesystem::is_regular_file(path, ec) || ec)
                continue;
            std::ifstream in(path, std::ios::binary);
            if (!in)
                continue;
            const std::istreambuf_iterator<char> begin(in);
            const std::istreambuf_iterator<char> end;
            const std::string                    raw(begin, end);
            std::string                          text;
            text.reserve(raw.size());
            for (char c : raw)
            {
                if (c != '\r')
                    text.push_back(c);
            }
            return text;
        }
        return {};
    }

    ShippedText readShipped()
    {
        ShippedText out;
        out.skills   = readShippedFile("skills.json");
        out.profiles = readShippedFile("profiles.json");
        return out;
    }

    bool replaceOne(std::string& text, std::string_view from, std::string_view to)
    {
        const auto pos = text.find(from);
        if (pos == std::string::npos)
            return false;
        text.replace(pos, from.size(), to);
        return true;
    }

    void replaceAll(std::string& text, std::string_view from, std::string_view to)
    {
        std::size_t pos = 0;
        while ((pos = text.find(from, pos)) != std::string::npos)
        {
            text.replace(pos, from.size(), to);
            pos += to.size();
        }
    }

    std::string withXpBase(std::string text, int xpBase)
    {
        if (!replaceOne(text, "\"xpBase\": 100", "\"xpBase\": " + std::to_string(xpBase)))
            return {};
        return text;
    }

    bool insertBeforeLast(std::string& text, char marker, std::string_view extra)
    {
        const auto pos = text.rfind(marker);
        if (pos == std::string::npos)
            return false;
        text.insert(pos, extra);
        return true;
    }

    uint8_t grantBit(SkillId id)
    {
        return static_cast<uint8_t>(1u << static_cast<unsigned>(id));
    }

    uint8_t npcGrant()
    {
        return static_cast<uint8_t>(grantBit(SkillId::Run) | grantBit(SkillId::Hear) | grantBit(SkillId::See));
    }

    uint8_t allGrant()
    {
        uint8_t mask = 0;
        for (int i = 0; i < kSkillCount; ++i)
            mask = static_cast<uint8_t>(mask | static_cast<uint8_t>(1u << i));
        return mask;
    }

    std::string profilesWithRun(int level, double xp)
    {
        return std::format(
            R"({{
  "version": 1,
  "profiles": [
    {{ "id": "player", "skills": {{ "run": {{ "level": {}, "xp": {} }} }} }},
    {{ "id": "hunter", "grant": ["run", "hear", "see"] }},
    {{ "id": "wolf", "grant": ["run", "hear", "see"] }}
  ]
}})",
            level, xp);
    }

    const char* kEmptyGrant = R"({
  "version": 1,
  "profiles": [
    { "id": "player", "grant": [], "skills": {} },
    { "id": "hunter", "grant": ["run", "hear", "see"] },
    { "id": "wolf", "grant": ["run", "hear", "see"] }
  ]
})";

    bool sawLevel(const std::vector<CapturedLog>& logs, LogLevel level, std::string_view snippet)
    {
        for (const CapturedLog& line : logs)
        {
            if (line.level == level && line.message.find(snippet) != std::string::npos)
                return true;
        }
        return false;
    }

    int runIndex()
    {
        return static_cast<int>(SkillId::Run);
    }

} // namespace

TEST(SkillCatalog, ProfileDefaultIsLevelOne)
{
    const SkillProfile profile;
    EXPECT_EQ(profile.grantMask, allGrant());
    EXPECT_EQ(profile.grantMask, 0x3Fu);
    for (int i = 0; i < kSkillCount; ++i)
    {
        EXPECT_EQ(profile.level[i], 1);
        EXPECT_FLOAT_EQ(profile.xp[i], 0.0f);
    }
}

TEST(SkillCatalog, NamesAndBuiltinCurve)
{
    EXPECT_STREQ(skillIdName(SkillId::Shoot), "shoot");
    EXPECT_STREQ(skillIdName(SkillId::Swim), "swim");
    EXPECT_STREQ(skillIdName(SkillId::Run), "run");
    EXPECT_STREQ(skillIdName(SkillId::Jump), "jump");
    EXPECT_STREQ(skillIdName(SkillId::Hear), "hear");
    EXPECT_STREQ(skillIdName(SkillId::See), "see");
    EXPECT_EQ(skillIdName(SkillId::Count), nullptr);

    EXPECT_FLOAT_EQ(kShootGrantInterval, 0.125f);

    const SkillCatalog cat;
    EXPECT_TRUE(cat.enabled());
    EXPECT_EQ(cat.maxLevel(), kSkillMaxLevel);
    EXPECT_FLOAT_EQ(cat.xpDtCap(), kSkillXpDtCap);
    EXPECT_FLOAT_EQ(cat.xpToNext(1), 100.0f);
    EXPECT_FLOAT_EQ(cat.xpToNext(2), 200.0f);
    EXPECT_FLOAT_EQ(cat.xpToNext(9), 900.0f);
    EXPECT_FLOAT_EQ(cat.xpToNext(10), 0.0f);
    EXPECT_FLOAT_EQ(cat.xpToNext(0), 0.0f);

    const SkillDef* shoot = cat.find(SkillId::Shoot);
    const SkillDef* swim  = cat.find(SkillId::Swim);
    const SkillDef* run   = cat.find(SkillId::Run);
    const SkillDef* jump  = cat.find(SkillId::Jump);
    const SkillDef* hear  = cat.find(SkillId::Hear);
    const SkillDef* see   = cat.find(SkillId::See);
    ASSERT_NE(shoot, nullptr);
    ASSERT_NE(swim, nullptr);
    ASSERT_NE(run, nullptr);
    ASSERT_NE(jump, nullptr);
    ASSERT_NE(hear, nullptr);
    ASSERT_NE(see, nullptr);
    EXPECT_EQ(cat.find(static_cast<SkillId>(200)), nullptr);

    EXPECT_FLOAT_EQ(shoot->xpPerEvent, 5.0f);
    EXPECT_FLOAT_EQ(shoot->xpPerMetre, 0.0f);
    EXPECT_EQ(shoot->scalarCount, 2);
    EXPECT_EQ(shoot->scalars[0].id, SkillScalar::RecoilScale);
    EXPECT_EQ(shoot->scalars[1].id, SkillScalar::CooldownScale);

    EXPECT_FLOAT_EQ(swim->xpPerMetre, 0.50f);
    EXPECT_EQ(swim->scalarCount, 1);
    EXPECT_EQ(swim->scalars[0].id, SkillScalar::SwimScale);

    EXPECT_FLOAT_EQ(run->xpPerMetre, 0.25f);
    EXPECT_EQ(run->scalarCount, 1);
    EXPECT_FLOAT_EQ(run->scalars[0].atLevel1, kSkillIdentity);
    EXPECT_FLOAT_EQ(run->scalars[0].atMaxLevel, kRunScaleMax);

    EXPECT_FLOAT_EQ(jump->xpPerEvent, 6.0f);
    EXPECT_EQ(jump->scalarCount, 1);

    EXPECT_FLOAT_EQ(hear->xpPerSecond, 2.0f);
    EXPECT_FLOAT_EQ(hear->xpPerSecondPlayer, 1.5f);
    EXPECT_EQ(hear->scalarCount, 1);

    EXPECT_FLOAT_EQ(see->xpPerSecond, 2.0f);
    EXPECT_FLOAT_EQ(see->xpPerSecondPlayer, 1.5f);
    EXPECT_EQ(see->scalarCount, 2);
    EXPECT_EQ(see->scalars[0].id, SkillScalar::SeeRangeScale);
    EXPECT_EQ(see->scalars[1].id, SkillScalar::SeeConeScale);

    const SkillProfile* player = cat.profile("player");
    const SkillProfile* hunter = cat.profile("hunter");
    const SkillProfile* wolf   = cat.profile("wolf");
    ASSERT_NE(player, nullptr);
    ASSERT_NE(hunter, nullptr);
    ASSERT_NE(wolf, nullptr);
    EXPECT_EQ(cat.profile("bear"), nullptr);
    EXPECT_EQ(player->grantMask, allGrant());
    EXPECT_EQ(hunter->grantMask, npcGrant());
    EXPECT_EQ(wolf->grantMask, npcGrant());
    EXPECT_EQ(hunter->grantMask & grantBit(SkillId::Shoot), 0);
    EXPECT_EQ(hunter->grantMask & grantBit(SkillId::Swim), 0);
    EXPECT_EQ(hunter->grantMask & grantBit(SkillId::Jump), 0);
    EXPECT_EQ(player->level[runIndex()], 1);
    EXPECT_FLOAT_EQ(player->xp[runIndex()], 0.0f);
    EXPECT_EQ(hunter->level[static_cast<int>(SkillId::Shoot)], 1);
    EXPECT_FLOAT_EQ(wolf->xp[static_cast<int>(SkillId::Hear)], 0.0f);
}

TEST(SkillCatalog, LevelFiveRunScaleAndClampedEnds)
{
    const float levelFive = 1.0f + 0.20f * (4.0f / 9.0f);
    EXPECT_NEAR(skillScalar(SkillId::Run, SkillScalar::RunScale, 5), levelFive, 1e-5f);

    const float recoilFive = kRecoilScaleMax + (kRecoilScaleMin - kRecoilScaleMax) * (4.0f / 9.0f);
    EXPECT_NEAR(skillScalar(SkillId::Shoot, SkillScalar::RecoilScale, 5), recoilFive, 1e-5f);

    EXPECT_NEAR(skillScalar(SkillId::Run, SkillScalar::RunScale, 1), kSkillIdentity, 1e-5f);
    EXPECT_NEAR(skillScalar(SkillId::Run, SkillScalar::RunScale, 0), kSkillIdentity, 1e-5f);
    EXPECT_NEAR(skillScalar(SkillId::Run, SkillScalar::RunScale, -4), kSkillIdentity, 1e-5f);
    EXPECT_NEAR(skillScalar(SkillId::Run, SkillScalar::RunScale, 10), kRunScaleMax, 1e-5f);
    EXPECT_FLOAT_EQ(skillScalar(SkillId::Run, SkillScalar::RunScale, 11), skillScalar(SkillId::Run, SkillScalar::RunScale, 10));
    EXPECT_GT(skillScalar(SkillId::Run, SkillScalar::RunScale, 11), 1.05f);

    EXPECT_NEAR(skillScalar(SkillId::Swim, SkillScalar::SwimScale, 10), kSwimScaleMax, 1e-5f);
    EXPECT_NEAR(skillScalar(SkillId::Jump, SkillScalar::JumpScale, 10), kJumpScaleMax, 1e-5f);
    EXPECT_NEAR(skillScalar(SkillId::Shoot, SkillScalar::RecoilScale, 10), kRecoilScaleMin, 1e-5f);
    EXPECT_NEAR(skillScalar(SkillId::Shoot, SkillScalar::CooldownScale, 10), kCooldownScaleMin, 1e-5f);
    EXPECT_NEAR(skillScalar(SkillId::Hear, SkillScalar::HearScale, 10), kHearScaleMax, 1e-5f);
    EXPECT_NEAR(skillScalar(SkillId::See, SkillScalar::SeeRangeScale, 10), kSeeRangeScaleMax, 1e-5f);
    EXPECT_NEAR(skillScalar(SkillId::See, SkillScalar::SeeConeScale, 10), kSeeConeScaleMax, 1e-5f);

    EXPECT_FLOAT_EQ(skillScalar(SkillId::Run, SkillScalar::HearScale, 10), kSkillIdentity);
    EXPECT_FLOAT_EQ(skillScalar(static_cast<SkillId>(200), SkillScalar::RunScale, 5), kSkillIdentity);
    EXPECT_FLOAT_EQ(skillScalar(SkillId::Shoot, SkillScalar::RunScale, 10), kSkillIdentity);
}

TEST(SkillCatalog, ClampSkillNonFiniteIsIdentity)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_FLOAT_EQ(clampSkill(nan, kRecoilScaleMin, kRecoilScaleMax), kSkillIdentity);
    EXPECT_FLOAT_EQ(clampSkill(-inf, kRecoilScaleMin, kRecoilScaleMax), kSkillIdentity);
    EXPECT_FLOAT_EQ(clampSkill(inf, kRunScaleMin, kRunScaleMax), kSkillIdentity);
    EXPECT_FLOAT_EQ(clampSkill(0.5f, kRecoilScaleMin, kRecoilScaleMax), kRecoilScaleMin);
    EXPECT_FLOAT_EQ(clampSkill(1.5f, kRunScaleMin, kRunScaleMax), kRunScaleMax);
    EXPECT_FLOAT_EQ(clampSkill(1.1f, kRunScaleMin, kRunScaleMax), 1.1f);
    EXPECT_NE(clampSkill(nan, kRecoilScaleMin, kRecoilScaleMax), kRecoilScaleMin);
}

TEST(SkillCatalog, DisabledCatalogScalarIsIdentity)
{
    const SingletonGuard guard;
    const ShippedText    shipped = readShipped();
    ASSERT_FALSE(shipped.skills.empty());
    std::string text = shipped.skills;
    ASSERT_TRUE(replaceOne(text, "\"enabled\": true", "\"enabled\": false"));
    ASSERT_TRUE(skillCatalog().parseSkills(text));
    EXPECT_FALSE(skillCatalog().enabled());
    EXPECT_FLOAT_EQ(skillScalar(SkillId::Run, SkillScalar::RunScale, 10), kSkillIdentity);
    EXPECT_FLOAT_EQ(skillScalar(SkillId::Shoot, SkillScalar::RecoilScale, 10), kSkillIdentity);
}

TEST(SkillCatalog, ParseRejectsBadDocumentsWithoutTouchingCatalog)
{
    const ShippedText shipped = readShipped();
    ASSERT_FALSE(shipped.skills.empty());
    ASSERT_FALSE(shipped.profiles.empty());

    SkillCatalog      cat;
    const std::string xp200 = withXpBase(shipped.skills, 200);
    ASSERT_FALSE(xp200.empty());
    ASSERT_TRUE(cat.parseSkills(xp200));
    EXPECT_FLOAT_EQ(cat.xpToNext(1), 200.0f);
    ASSERT_TRUE(cat.parseProfiles(kEmptyGrant));
    EXPECT_EQ(cat.profile("player")->grantMask, 0);

    EXPECT_FALSE(cat.parseSkills(""));
    EXPECT_FALSE(cat.parseSkills("{"));
    EXPECT_FALSE(cat.parseSkills("[]"));
    EXPECT_FALSE(cat.parseSkills("{ \"version\": 1 }"));
    std::string oversized(64u * 1024u + 1u, ' ');
    EXPECT_FALSE(cat.parseSkills(oversized));

    std::string badVersion = shipped.skills;
    ASSERT_TRUE(replaceOne(badVersion, "\"version\": 1", "\"version\": 2"));
    EXPECT_FALSE(cat.parseSkills(badVersion));

    std::string badLevel = shipped.skills;
    ASSERT_TRUE(replaceOne(badLevel, "\"maxLevel\": 10", "\"maxLevel\": 9"));
    EXPECT_FALSE(cat.parseSkills(badLevel));

    const std::string xp24  = withXpBase(shipped.skills, 24);
    const std::string xp501 = withXpBase(shipped.skills, 501);
    const std::string xp25  = withXpBase(shipped.skills, 25);
    const std::string xp500 = withXpBase(shipped.skills, 500);
    ASSERT_FALSE(xp24.empty());
    ASSERT_FALSE(xp501.empty());
    ASSERT_FALSE(xp25.empty());
    ASSERT_FALSE(xp500.empty());
    EXPECT_FALSE(cat.parseSkills(xp24));
    EXPECT_FALSE(cat.parseSkills(xp501));
    EXPECT_TRUE(cat.parseSkills(xp25));
    EXPECT_FLOAT_EQ(cat.xpToNext(1), 25.0f);
    EXPECT_TRUE(cat.parseSkills(xp500));
    EXPECT_FLOAT_EQ(cat.xpToNext(1), 500.0f);
    ASSERT_TRUE(cat.parseSkills(xp200));

    std::string cap = shipped.skills;
    ASSERT_TRUE(replaceOne(cap, "\"xpDtCap\": 0.1", "\"xpDtCap\": 0"));
    EXPECT_FALSE(cat.parseSkills(cap));
    cap = shipped.skills;
    ASSERT_TRUE(replaceOne(cap, "\"xpDtCap\": 0.1", "\"xpDtCap\": -1"));
    EXPECT_FALSE(cat.parseSkills(cap));
    cap = shipped.skills;
    ASSERT_TRUE(replaceOne(cap, "\"xpDtCap\": 0.1", "\"xpDtCap\": 0.26"));
    EXPECT_FALSE(cat.parseSkills(cap));
    cap = shipped.skills;
    ASSERT_TRUE(replaceOne(cap, "\"xpDtCap\": 0.1", "\"xpDtCap\": 0.25"));
    EXPECT_TRUE(cat.parseSkills(cap));
    EXPECT_NEAR(cat.xpDtCap(), 0.25f, 1e-6f);
    ASSERT_TRUE(cat.parseSkills(xp200));

    std::string nanRun = xp200;
    ASSERT_TRUE(replaceOne(nanRun, "\"atMaxLevel\": 1.20", "\"atMaxLevel\": NaN"));
    EXPECT_FALSE(cat.parseSkills(nanRun));
    EXPECT_FLOAT_EQ(cat.xpToNext(1), 200.0f);

    std::string infRun = xp200;
    ASSERT_TRUE(replaceOne(infRun, "\"atMaxLevel\": 1.20", "\"atMaxLevel\": 1e308"));
    EXPECT_FALSE(cat.parseSkills(infRun));
    EXPECT_FLOAT_EQ(cat.xpToNext(1), 200.0f);

    std::string outside = xp200;
    ASSERT_TRUE(replaceOne(outside, "\"atMaxLevel\": 1.20", "\"atMaxLevel\": 1.21"));
    EXPECT_FALSE(cat.parseSkills(outside));

    std::string inverted = xp200;
    ASSERT_TRUE(replaceOne(inverted, "{ \"id\": \"runScale\", \"atLevel1\": 1.0, \"atMaxLevel\": 1.20, \"min\": 1.0, \"max\": 1.20 }",
                           "{ \"id\": \"runScale\", \"atLevel1\": 1.0, \"atMaxLevel\": 1.0, \"min\": 1.10, \"max\": 1.0 }"));
    EXPECT_FALSE(cat.parseSkills(inverted));

    std::string missingRecoil = xp200;
    ASSERT_TRUE(replaceOne(missingRecoil, "{ \"id\": \"recoilScale\", \"atLevel1\": 1.0, \"atMaxLevel\": 0.70, \"min\": 0.70, \"max\": 1.0 },\n", ""));
    EXPECT_FALSE(cat.parseSkills(missingRecoil));

    std::string duplicateScalar = xp200;
    ASSERT_TRUE(replaceOne(duplicateScalar, "{ \"id\": \"recoilScale\", \"atLevel1\": 1.0, \"atMaxLevel\": 0.70, \"min\": 0.70, \"max\": 1.0 },\n",
                           "{ \"id\": \"recoilScale\", \"atLevel1\": 1.0, \"atMaxLevel\": 0.70, \"min\": 0.70, \"max\": 1.0 },\n        { \"id\": \"recoilScale\", \"atLevel1\": 1.0, \"atMaxLevel\": "
                           "0.70, \"min\": 0.70, \"max\": 1.0 },\n"));
    EXPECT_FALSE(cat.parseSkills(duplicateScalar));

    const char* runScalar = "{ \"id\": \"runScale\", \"atLevel1\": 1.0, \"atMaxLevel\": 1.20, \"min\": 1.0, \"max\": 1.20 }";
    std::string five      = xp200;
    ASSERT_TRUE(replaceOne(five, runScalar, std::string(runScalar) + ", " + runScalar + ", " + runScalar + ", " + runScalar + ", " + runScalar));
    EXPECT_FALSE(cat.parseSkills(five));

    std::string wrongScalar = xp200;
    ASSERT_TRUE(replaceOne(wrongScalar, "\"id\": \"swimScale\"", "\"id\": \"runScale\""));
    EXPECT_FALSE(cat.parseSkills(wrongScalar));

    std::string duplicateSkill = xp200;
    ASSERT_TRUE(insertBeforeLast(duplicateSkill, ']',
                                 ", { \"id\": \"run\", \"xpPerEvent\": 0, \"xpPerMetre\": 0.25, \"xpPerSecond\": 0, \"xpPerSecondPlayer\": 0, \"scalars\": [ { \"id\": \"runScale\", \"atLevel1\": "
                                 "1.0, \"atMaxLevel\": 1.20, \"min\": 1.0, \"max\": 1.20 } ] }"));
    EXPECT_FALSE(cat.parseSkills(duplicateSkill));

    std::string negativeRate = xp200;
    ASSERT_TRUE(replaceOne(negativeRate, "\"xpPerMetre\": 0.25", "\"xpPerMetre\": -1"));
    EXPECT_FALSE(cat.parseSkills(negativeRate));

    std::string missingRate = xp200;
    ASSERT_TRUE(replaceOne(missingRate, "\"id\": \"run\",\n      \"xpPerEvent\": 0,\n", "\"id\": \"run\",\n"));
    EXPECT_FALSE(cat.parseSkills(missingRate));

    std::string unknown = shipped.skills;
    ASSERT_TRUE(insertBeforeLast(unknown, ']', ", { \"id\": \"fly\" }"));
    EXPECT_TRUE(cat.parseSkills(unknown));
    EXPECT_EQ(cat.find(SkillId::Run)->scalarCount, 1);

    std::string duplicateUnknown = shipped.skills;
    ASSERT_TRUE(insertBeforeLast(duplicateUnknown, ']', ", { \"id\": \"fly\" }, { \"id\": \"fly\" }"));
    EXPECT_FALSE(cat.parseSkills(duplicateUnknown));

    std::string omittedEnabled = shipped.skills;
    ASSERT_TRUE(replaceOne(omittedEnabled, "  \"enabled\": true,\n", ""));
    EXPECT_TRUE(cat.parseSkills(omittedEnabled));
    EXPECT_TRUE(cat.enabled());

    EXPECT_FLOAT_EQ(cat.xpToNext(1), 100.0f);
    EXPECT_EQ(cat.profile("player")->grantMask, 0);
}

TEST(SkillCatalog, ProfilesGrantMaskAndXpAgainstCurrentCurve)
{
    const ShippedText shipped = readShipped();
    ASSERT_FALSE(shipped.profiles.empty());
    ASSERT_FALSE(shipped.skills.empty());

    SkillCatalog cat;
    ASSERT_TRUE(cat.parseProfiles(shipped.profiles));
    EXPECT_EQ(cat.profile("player")->grantMask, allGrant());
    EXPECT_EQ(cat.profile("hunter")->grantMask, npcGrant());
    EXPECT_EQ(cat.profile("wolf")->grantMask, npcGrant());
    EXPECT_EQ(cat.profile("player")->level[runIndex()], 1);
    EXPECT_FLOAT_EQ(cat.profile("player")->xp[runIndex()], 0.0f);

    ASSERT_TRUE(cat.parseProfiles(kEmptyGrant));
    EXPECT_EQ(cat.profile("player")->grantMask, 0);
    EXPECT_EQ(cat.profile("player")->level[static_cast<int>(SkillId::Shoot)], 1);

    const char* unknownGrant = R"({
      "version": 1,
      "profiles": [
        { "id": "player", "grant": ["run", "fly"] },
        { "id": "hunter", "grant": ["run", "hear", "see"] },
        { "id": "wolf", "grant": ["run", "hear", "see"] }
      ]
    })";
    EXPECT_FALSE(cat.parseProfiles(unknownGrant));
    EXPECT_EQ(cat.profile("player")->grantMask, 0);

    const char* missingWolf = R"({
      "version": 1,
      "profiles": [
        { "id": "player" },
        { "id": "hunter", "grant": ["run", "hear", "see"] }
      ]
    })";
    EXPECT_FALSE(cat.parseProfiles(missingWolf));

    const char* duplicatePlayer = R"({
      "version": 1,
      "profiles": [
        { "id": "player" },
        { "id": "player" },
        { "id": "hunter", "grant": ["run", "hear", "see"] },
        { "id": "wolf", "grant": ["run", "hear", "see"] }
      ]
    })";
    EXPECT_FALSE(cat.parseProfiles(duplicatePlayer));
    EXPECT_EQ(cat.profile("player")->grantMask, 0);

    std::string extra = shipped.profiles;
    ASSERT_TRUE(insertBeforeLast(extra, ']', ", { \"id\": \"bear\" }"));
    EXPECT_TRUE(cat.parseProfiles(extra));
    EXPECT_EQ(cat.profile("bear"), nullptr);
    EXPECT_EQ(cat.profile("player")->grantMask, allGrant());

    EXPECT_FALSE(cat.parseProfiles(profilesWithRun(1, 100.0)));
    EXPECT_TRUE(cat.parseProfiles(profilesWithRun(1, 99.0)));
    EXPECT_FLOAT_EQ(cat.profile("player")->xp[runIndex()], 99.0f);
    EXPECT_FALSE(cat.parseProfiles(profilesWithRun(1, 100.0)));
    EXPECT_FLOAT_EQ(cat.profile("player")->xp[runIndex()], 99.0f);

    EXPECT_TRUE(cat.parseProfiles(profilesWithRun(9, 899.0)));
    EXPECT_FALSE(cat.parseProfiles(profilesWithRun(9, 900.0)));
    EXPECT_EQ(cat.profile("player")->level[runIndex()], 9);
    EXPECT_TRUE(cat.parseProfiles(profilesWithRun(10, 0.0)));
    EXPECT_EQ(cat.profile("player")->level[runIndex()], 10);
    EXPECT_FLOAT_EQ(cat.profile("player")->xp[runIndex()], 0.0f);
    EXPECT_FALSE(cat.parseProfiles(profilesWithRun(10, 1.0)));
    EXPECT_EQ(cat.profile("player")->level[runIndex()], 10);
    EXPECT_FALSE(cat.parseProfiles(profilesWithRun(0, 0.0)));
    EXPECT_FALSE(cat.parseProfiles(profilesWithRun(11, 0.0)));
    EXPECT_FALSE(cat.parseProfiles(profilesWithRun(1, -1.0)));

    const char* infXp = R"({
      "version": 1,
      "profiles": [
        { "id": "player", "skills": { "run": { "level": 1, "xp": 1e308 } } },
        { "id": "hunter", "grant": ["run", "hear", "see"] },
        { "id": "wolf", "grant": ["run", "hear", "see"] }
      ]
    })";
    EXPECT_FALSE(cat.parseProfiles(infXp));

    const std::string base25 = withXpBase(shipped.skills, 25);
    ASSERT_FALSE(base25.empty());
    ASSERT_TRUE(cat.parseSkills(base25));
    EXPECT_TRUE(cat.parseProfiles(profilesWithRun(1, 24.0)));
    EXPECT_FLOAT_EQ(cat.profile("player")->xp[runIndex()], 24.0f);
    EXPECT_FALSE(cat.parseProfiles(profilesWithRun(1, 25.0)));
    EXPECT_FLOAT_EQ(cat.profile("player")->xp[runIndex()], 24.0f);
    EXPECT_EQ(cat.profile("hunter")->grantMask, npcGrant());
}

TEST(SkillCatalog, LoadShippedContentLatches)
{
    SkillCatalog             cat;
    std::vector<CapturedLog> logs;
    {
        LogCapture capture(logs);
        ASSERT_TRUE(cat.loadFromContent());
    }
    EXPECT_TRUE(sawLevel(logs, LogLevel::Info, "SkillCatalog: loaded"));
    EXPECT_FLOAT_EQ(cat.xpToNext(1), 100.0f);
    EXPECT_FLOAT_EQ(cat.xpDtCap(), kSkillXpDtCap);
    EXPECT_EQ(cat.profile("player")->grantMask, allGrant());
    EXPECT_EQ(cat.profile("hunter")->grantMask, npcGrant());
    EXPECT_EQ(cat.profile("wolf")->grantMask, npcGrant());
    EXPECT_EQ(cat.find(SkillId::Shoot)->xpPerEvent, 5.0f);
    EXPECT_EQ(cat.find(SkillId::Run)->scalarCount, 1);

    const float     levelFive = 1.0f + 0.20f * (4.0f / 9.0f);
    const SkillDef* run       = cat.find(SkillId::Run);
    ASSERT_NE(run, nullptr);
    const float t        = static_cast<float>(4) / static_cast<float>(9);
    const float fromFile = run->scalars[0].atLevel1 + (run->scalars[0].atMaxLevel - run->scalars[0].atLevel1) * t;
    EXPECT_NEAR(fromFile, levelFive, 1e-5f);
    EXPECT_NEAR(skillScalar(SkillId::Run, SkillScalar::RunScale, 5), levelFive, 1e-5f);

    const ShippedText shipped = readShipped();
    ASSERT_FALSE(shipped.skills.empty());
    ContentGuard guard;
    ASSERT_TRUE(guard.replaceFirst("skills.json", withXpBase(shipped.skills, 250)));
    logs.clear();
    {
        LogCapture capture(logs);
        EXPECT_TRUE(cat.loadFromContent());
    }
    EXPECT_FALSE(sawLevel(logs, LogLevel::Info, "SkillCatalog: loaded"));
    EXPECT_FALSE(sawLevel(logs, LogLevel::Error, "SkillCatalog"));
    EXPECT_FLOAT_EQ(cat.xpToNext(1), 100.0f);
}

TEST(SkillCatalog, SkillsValidProfilesMissingLeavesCatalogAndLatches)
{
    const ShippedText shipped = readShipped();
    ASSERT_FALSE(shipped.skills.empty());
    const std::string xp200 = withXpBase(shipped.skills, 200);
    ASSERT_FALSE(xp200.empty());

    ContentGuard guard;
    ASSERT_TRUE(guard.hideAll("profiles.json"));

    SkillCatalog cat;
    ASSERT_TRUE(cat.parseSkills(xp200));
    ASSERT_TRUE(cat.parseProfiles(kEmptyGrant));

    std::vector<CapturedLog> logs;
    {
        LogCapture capture(logs);
        EXPECT_FALSE(cat.loadFromContent());
    }
    EXPECT_TRUE(sawLevel(logs, LogLevel::Error, "profiles.json missing"));
    EXPECT_FALSE(sawLevel(logs, LogLevel::Warn, "keeping built-in defaults"));
    EXPECT_FLOAT_EQ(cat.xpToNext(1), 200.0f);
    EXPECT_EQ(cat.profile("player")->grantMask, 0);

    guard.restoreAll();
    logs.clear();
    {
        LogCapture capture(logs);
        EXPECT_FALSE(cat.loadFromContent());
    }
    EXPECT_FALSE(sawLevel(logs, LogLevel::Info, "SkillCatalog"));
    EXPECT_FALSE(sawLevel(logs, LogLevel::Error, "SkillCatalog"));
    EXPECT_FLOAT_EQ(cat.xpToNext(1), 200.0f);
    EXPECT_EQ(cat.profile("player")->grantMask, 0);
}

TEST(SkillCatalog, SkillsInvalidProfilesValidLeavesCatalogAndLatches)
{
    const ShippedText shipped = readShipped();
    ASSERT_FALSE(shipped.skills.empty());
    const std::string xp200 = withXpBase(shipped.skills, 200);
    ASSERT_FALSE(xp200.empty());

    ContentGuard guard;
    ASSERT_TRUE(guard.replaceFirst("skills.json", "{ \"version\": 1 }"));

    SkillCatalog cat;
    ASSERT_TRUE(cat.parseSkills(xp200));
    ASSERT_TRUE(cat.parseProfiles(kEmptyGrant));

    std::vector<CapturedLog> logs;
    {
        LogCapture capture(logs);
        EXPECT_FALSE(cat.loadFromContent());
    }
    EXPECT_TRUE(sawLevel(logs, LogLevel::Error, "SkillCatalog"));
    EXPECT_FLOAT_EQ(cat.xpToNext(1), 200.0f);
    EXPECT_EQ(cat.profile("player")->grantMask, 0);

    guard.restoreAll();
    logs.clear();
    {
        LogCapture capture(logs);
        EXPECT_FALSE(cat.loadFromContent());
    }
    EXPECT_FALSE(sawLevel(logs, LogLevel::Error, "SkillCatalog"));
    EXPECT_FALSE(sawLevel(logs, LogLevel::Info, "SkillCatalog"));
    EXPECT_FLOAT_EQ(cat.xpToNext(1), 200.0f);
    EXPECT_EQ(cat.profile("player")->grantMask, 0);
}

TEST(SkillCatalog, ValidSkillsDoNotMergeWhenProfilesAreInvalid)
{
    const ShippedText shipped = readShipped();
    ASSERT_FALSE(shipped.skills.empty());
    ContentGuard guard;
    ASSERT_TRUE(guard.replaceFirst("skills.json", withXpBase(shipped.skills, 250)));
    ASSERT_TRUE(guard.replaceFirst("profiles.json", "{ \"version\": 2 }"));

    SkillCatalog cat;
    ASSERT_TRUE(cat.parseProfiles(kEmptyGrant));
    EXPECT_FALSE(cat.loadFromContent());
    EXPECT_FLOAT_EQ(cat.xpToNext(1), 100.0f);
    EXPECT_EQ(cat.profile("player")->grantMask, 0);
}

TEST(SkillCatalog, ProfileXpUsesSkillsTempFromTheSameLoad)
{
    const ShippedText shipped = readShipped();
    ASSERT_FALSE(shipped.skills.empty());
    ASSERT_FALSE(shipped.profiles.empty());
    std::string profiles = shipped.profiles;
    replaceAll(profiles, "\"xp\": 0", "\"xp\": 30");

    ContentGuard guard;
    ASSERT_TRUE(guard.replaceFirst("skills.json", withXpBase(shipped.skills, 25)));
    ASSERT_TRUE(guard.replaceFirst("profiles.json", profiles));

    SkillCatalog cat;
    EXPECT_FALSE(cat.loadFromContent());
    EXPECT_FLOAT_EQ(cat.xpToNext(1), 100.0f);
    EXPECT_EQ(cat.profile("player")->grantMask, allGrant());
}

TEST(SkillCatalog, LoadCommitsBothTempsTogether)
{
    const ShippedText shipped = readShipped();
    ASSERT_FALSE(shipped.skills.empty());
    ASSERT_FALSE(shipped.profiles.empty());
    ContentGuard guard;
    ASSERT_TRUE(guard.replaceFirst("skills.json", withXpBase(shipped.skills, 250)));
    ASSERT_TRUE(guard.replaceFirst("profiles.json", shipped.profiles));

    SkillCatalog cat;
    ASSERT_TRUE(cat.loadFromContent());
    EXPECT_FLOAT_EQ(cat.xpToNext(1), 250.0f);
    EXPECT_EQ(cat.profile("player")->grantMask, allGrant());
    EXPECT_EQ(cat.profile("hunter")->grantMask, npcGrant());
    EXPECT_EQ(cat.find(SkillId::Jump)->xpPerEvent, 6.0f);
}

TEST(SkillCatalog, BothMissingKeepsBuiltinsAndLatches)
{
    ContentGuard guard;
    ASSERT_TRUE(guard.hideAll("skills.json"));
    ASSERT_TRUE(guard.hideAll("profiles.json"));

    SkillCatalog             cat;
    std::vector<CapturedLog> logs;
    {
        LogCapture capture(logs);
        EXPECT_TRUE(cat.loadFromContent());
    }
    EXPECT_TRUE(sawLevel(logs, LogLevel::Warn, "keeping built-in defaults"));
    EXPECT_FALSE(sawLevel(logs, LogLevel::Error, "SkillCatalog"));
    EXPECT_FLOAT_EQ(cat.xpToNext(1), 100.0f);
    EXPECT_FLOAT_EQ(cat.xpDtCap(), kSkillXpDtCap);
    EXPECT_TRUE(cat.enabled());
    EXPECT_EQ(cat.maxLevel(), kSkillMaxLevel);
    EXPECT_EQ(cat.profile("player")->grantMask, allGrant());
    EXPECT_EQ(cat.profile("hunter")->grantMask, npcGrant());
    EXPECT_EQ(cat.profile("wolf")->grantMask, npcGrant());
    EXPECT_FLOAT_EQ(cat.find(SkillId::Run)->scalars[0].atMaxLevel, kRunScaleMax);
    EXPECT_FLOAT_EQ(cat.find(SkillId::Swim)->xpPerMetre, 0.50f);

    const std::string probeSkills = R"({
      "version": 1,
      "enabled": true,
      "maxLevel": 10,
      "xpBase": 250,
      "xpDtCap": 0.1,
      "skills": [
        {
          "id": "shoot",
          "xpPerEvent": 5, "xpPerMetre": 0, "xpPerSecond": 0, "xpPerSecondPlayer": 0,
          "scalars": [
            { "id": "recoilScale", "atLevel1": 1.0, "atMaxLevel": 0.70, "min": 0.70, "max": 1.0 },
            { "id": "cooldownScale", "atLevel1": 1.0, "atMaxLevel": 0.85, "min": 0.85, "max": 1.0 }
          ]
        },
        {
          "id": "swim",
          "xpPerEvent": 0, "xpPerMetre": 0.50, "xpPerSecond": 0, "xpPerSecondPlayer": 0,
          "scalars": [ { "id": "swimScale", "atLevel1": 1.0, "atMaxLevel": 1.25, "min": 1.0, "max": 1.25 } ]
        },
        {
          "id": "run",
          "xpPerEvent": 0, "xpPerMetre": 0.25, "xpPerSecond": 0, "xpPerSecondPlayer": 0,
          "scalars": [ { "id": "runScale", "atLevel1": 1.0, "atMaxLevel": 1.20, "min": 1.0, "max": 1.20 } ]
        },
        {
          "id": "jump",
          "xpPerEvent": 6, "xpPerMetre": 0, "xpPerSecond": 0, "xpPerSecondPlayer": 0,
          "scalars": [ { "id": "jumpScale", "atLevel1": 1.0, "atMaxLevel": 1.15, "min": 1.0, "max": 1.15 } ]
        },
        {
          "id": "hear",
          "xpPerEvent": 0, "xpPerMetre": 0, "xpPerSecond": 2.0, "xpPerSecondPlayer": 1.5,
          "scalars": [ { "id": "hearScale", "atLevel1": 1.0, "atMaxLevel": 1.30, "min": 1.0, "max": 1.30 } ]
        },
        {
          "id": "see",
          "xpPerEvent": 0, "xpPerMetre": 0, "xpPerSecond": 2.0, "xpPerSecondPlayer": 1.5,
          "scalars": [
            { "id": "seeRangeScale", "atLevel1": 1.0, "atMaxLevel": 1.25, "min": 1.0, "max": 1.25 },
            { "id": "seeConeScale", "atLevel1": 1.0, "atMaxLevel": 1.15, "min": 1.0, "max": 1.15 }
          ]
        }
      ]
    })";
    ASSERT_TRUE(guard.writeOverFirstHidden("skills.json", probeSkills));
    ASSERT_TRUE(guard.writeOverFirstHidden("profiles.json", kEmptyGrant));

    logs.clear();
    {
        LogCapture capture(logs);
        EXPECT_TRUE(cat.loadFromContent());
    }
    EXPECT_FALSE(sawLevel(logs, LogLevel::Info, "SkillCatalog"));
    EXPECT_FALSE(sawLevel(logs, LogLevel::Warn, "SkillCatalog"));
    EXPECT_FALSE(sawLevel(logs, LogLevel::Error, "SkillCatalog"));
    EXPECT_FLOAT_EQ(cat.xpToNext(1), 100.0f);
    EXPECT_EQ(cat.profile("player")->grantMask, allGrant());
}

TEST(SkillCatalog, OversizedSkillsFileLeavesCatalog)
{
    const std::string xp200 = withXpBase(readShipped().skills, 200);
    ASSERT_FALSE(xp200.empty());

    ContentGuard      guard;
    const std::string big(64u * 1024u + 1u, 'x');
    ASSERT_TRUE(guard.replaceFirst("skills.json", big));

    SkillCatalog cat;
    ASSERT_TRUE(cat.parseSkills(xp200));
    EXPECT_FALSE(cat.loadFromContent());
    EXPECT_FLOAT_EQ(cat.xpToNext(1), 200.0f);
    EXPECT_FALSE(cat.loadFromContent());
    EXPECT_FLOAT_EQ(cat.xpToNext(1), 200.0f);
}

TEST(SkillCatalog, ShippedFilesRestored)
{
    SkillCatalog cat;
    ASSERT_TRUE(cat.loadFromContent());
    EXPECT_FLOAT_EQ(cat.xpToNext(1), 100.0f);
    EXPECT_FLOAT_EQ(cat.xpDtCap(), kSkillXpDtCap);
    EXPECT_EQ(cat.profile("player")->grantMask, allGrant());
    EXPECT_EQ(cat.profile("hunter")->grantMask, npcGrant());
}
