#include "Character/SkillCatalog.h"

#include "Core/ContentRoots.h"
#include "Core/Log.h"

#include "third_party/nlohmann/json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace Dark
{
    namespace
    {
        using json   = nlohmann::json;
        namespace fs = std::filesystem;

        constexpr std::uintmax_t kMaxJsonBytes     = 64u * 1024u;
        constexpr int            kSkillFileVersion = 1;
        constexpr int            kProfileSlots     = 3;
        constexpr float          kXpBaseMin        = 25.0f;
        constexpr float          kXpBaseMax        = 500.0f;
        constexpr float          kXpDtCapMax       = 0.25f;

        constexpr std::string_view kProfileNames[kProfileSlots] = { "player", "hunter", "wolf" };

        const char* scalarName(SkillScalar id)
        {
            switch (id)
            {
            case SkillScalar::RecoilScale:
                return "recoilScale";
            case SkillScalar::CooldownScale:
                return "cooldownScale";
            case SkillScalar::SwimScale:
                return "swimScale";
            case SkillScalar::RunScale:
                return "runScale";
            case SkillScalar::JumpScale:
                return "jumpScale";
            case SkillScalar::HearScale:
                return "hearScale";
            case SkillScalar::SeeRangeScale:
                return "seeRangeScale";
            case SkillScalar::SeeConeScale:
                return "seeConeScale";
            default:
                return nullptr;
            }
        }

        bool skillFromName(std::string_view text, SkillId& out)
        {
            for (int i = 0; i < kSkillCount; ++i)
            {
                const SkillId id   = static_cast<SkillId>(i);
                const char*   name = skillIdName(id);
                if (name && text == name)
                {
                    out = id;
                    return true;
                }
            }
            return false;
        }

        bool scalarFromName(std::string_view text, SkillScalar& out)
        {
            for (int i = 0; i < static_cast<int>(SkillScalar::Count); ++i)
            {
                const SkillScalar id   = static_cast<SkillScalar>(i);
                const char*       name = scalarName(id);
                if (name && text == name)
                {
                    out = id;
                    return true;
                }
            }
            return false;
        }

        void hardRange(SkillScalar id, float& lo, float& hi)
        {
            switch (id)
            {
            case SkillScalar::RecoilScale:
                lo = kRecoilScaleMin;
                hi = kRecoilScaleMax;
                break;
            case SkillScalar::CooldownScale:
                lo = kCooldownScaleMin;
                hi = kCooldownScaleMax;
                break;
            case SkillScalar::SwimScale:
                lo = kSwimScaleMin;
                hi = kSwimScaleMax;
                break;
            case SkillScalar::RunScale:
                lo = kRunScaleMin;
                hi = kRunScaleMax;
                break;
            case SkillScalar::JumpScale:
                lo = kJumpScaleMin;
                hi = kJumpScaleMax;
                break;
            case SkillScalar::HearScale:
                lo = kHearScaleMin;
                hi = kHearScaleMax;
                break;
            case SkillScalar::SeeRangeScale:
                lo = kSeeRangeScaleMin;
                hi = kSeeRangeScaleMax;
                break;
            case SkillScalar::SeeConeScale:
                lo = kSeeConeScaleMin;
                hi = kSeeConeScaleMax;
                break;
            default:
                lo = kSkillIdentity;
                hi = kSkillIdentity;
                break;
            }
        }

        int requiredScalars(SkillId id, SkillScalar* out)
        {
            switch (id)
            {
            case SkillId::Shoot:
                out[0] = SkillScalar::RecoilScale;
                out[1] = SkillScalar::CooldownScale;
                return 2;
            case SkillId::Swim:
                out[0] = SkillScalar::SwimScale;
                return 1;
            case SkillId::Run:
                out[0] = SkillScalar::RunScale;
                return 1;
            case SkillId::Jump:
                out[0] = SkillScalar::JumpScale;
                return 1;
            case SkillId::Hear:
                out[0] = SkillScalar::HearScale;
                return 1;
            case SkillId::See:
                out[0] = SkillScalar::SeeRangeScale;
                out[1] = SkillScalar::SeeConeScale;
                return 2;
            default:
                return 0;
            }
        }

        bool jsonToDouble(const json& v, double& out)
        {
            if (const auto* f = v.get_ptr<const json::number_float_t*>())
            {
                out = *f;
                return std::isfinite(out);
            }
            if (const auto* i = v.get_ptr<const json::number_integer_t*>())
            {
                out = static_cast<double>(*i);
                return true;
            }
            if (const auto* u = v.get_ptr<const json::number_unsigned_t*>())
            {
                out = static_cast<double>(*u);
                return true;
            }
            return false;
        }

        bool readString(const json& obj, const char* key, std::string& out)
        {
            const auto it = obj.find(key);
            if (it == obj.end() || !it->is_string())
                return false;
            if (const auto* s = it->get_ptr<const json::string_t*>())
            {
                out = *s;
                return true;
            }
            return false;
        }

        bool isExactInt(double d, int& out)
        {
            if (!std::isfinite(d) || d < -2147483648.0 || d > 2147483647.0)
                return false;
            const int i = static_cast<int>(d);
            if (static_cast<double>(i) != d)
                return false;
            out = i;
            return true;
        }

        bool readNonNegative(const json& obj, const char* key, float& out)
        {
            const auto it = obj.find(key);
            if (it == obj.end())
                return false;
            double d = 0.0;
            if (!jsonToDouble(*it, d))
                return false;
            const float f = static_cast<float>(d);
            if (!std::isfinite(f) || f < 0.0f)
                return false;
            out = f;
            return true;
        }

        bool readInsideHard(const json& obj, const char* key, SkillScalar id, float& out)
        {
            const auto it = obj.find(key);
            if (it == obj.end())
                return false;
            double d = 0.0;
            if (!jsonToDouble(*it, d))
                return false;
            const float f = static_cast<float>(d);
            if (!std::isfinite(f))
                return false;
            float lo = 0.0f;
            float hi = 0.0f;
            hardRange(id, lo, hi);
            if (f < lo || f > hi)
                return false;
            out = f;
            return true;
        }

        void setScalar(SkillDef& def, SkillScalar id, float atMax, float lo, float hi)
        {
            SkillScalarDef& slot = def.scalars[def.scalarCount];
            slot.id              = id;
            slot.atLevel1        = kSkillIdentity;
            slot.atMaxLevel      = atMax;
            slot.minV            = lo;
            slot.maxV            = hi;
            def.scalarCount += 1;
        }

        uint8_t npcGrantMask()
        {
            const unsigned mask = (1u << static_cast<unsigned>(SkillId::Run)) | (1u << static_cast<unsigned>(SkillId::Hear)) | (1u << static_cast<unsigned>(SkillId::See));
            return static_cast<uint8_t>(mask);
        }

        bool fillSkill(const json& item, SkillDef& def)
        {
            const char* name = skillIdName(def.id);
            if (!readNonNegative(item, "xpPerEvent", def.xpPerEvent) || !readNonNegative(item, "xpPerMetre", def.xpPerMetre) || !readNonNegative(item, "xpPerSecond", def.xpPerSecond) ||
                !readNonNegative(item, "xpPerSecondPlayer", def.xpPerSecondPlayer))
            {
                DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: skill '{}' has a bad xp rate", name);
                return false;
            }

            const auto scalars = item.find("scalars");
            if (scalars == item.end() || !scalars->is_array())
            {
                DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: skill '{}' missing scalars", name);
                return false;
            }
            if (scalars->size() > 4)
            {
                DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: skill '{}' has more than 4 scalars", name);
                return false;
            }

            SkillScalar required[4]{};
            const int   requiredCount = requiredScalars(def.id, required);
            bool        seen[static_cast<int>(SkillScalar::Count)]{};
            for (const json& scalar : *scalars)
            {
                if (!scalar.is_object())
                {
                    DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: skill '{}' scalar is not an object", name);
                    return false;
                }
                std::string idText;
                if (!readString(scalar, "id", idText))
                {
                    DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: skill '{}' scalar missing id", name);
                    return false;
                }
                SkillScalar sid = SkillScalar::Count;
                if (!scalarFromName(idText, sid))
                {
                    DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: skill '{}' scalar '{}' is not valid for that skill", name, idText);
                    return false;
                }
                const int sidx = static_cast<int>(sid);
                if (seen[sidx])
                {
                    DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: skill '{}' duplicate scalar '{}'", name, idText);
                    return false;
                }
                bool allowed = false;
                for (int r = 0; r < requiredCount; ++r)
                {
                    if (required[r] == sid)
                    {
                        allowed = true;
                        break;
                    }
                }
                if (!allowed)
                {
                    DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: skill '{}' scalar '{}' is not valid for that skill", name, idText);
                    return false;
                }

                float atLevel1   = 0.0f;
                float atMaxLevel = 0.0f;
                float minV       = 0.0f;
                float maxV       = 0.0f;
                if (!readInsideHard(scalar, "atLevel1", sid, atLevel1) || !readInsideHard(scalar, "atMaxLevel", sid, atMaxLevel) || !readInsideHard(scalar, "min", sid, minV) ||
                    !readInsideHard(scalar, "max", sid, maxV) || minV > maxV)
                {
                    DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: skill '{}' scalar '{}' endpoint outside clamp", name, idText);
                    return false;
                }

                seen[sidx]           = true;
                SkillScalarDef& slot = def.scalars[def.scalarCount];
                slot.id              = sid;
                slot.atLevel1        = atLevel1;
                slot.atMaxLevel      = atMaxLevel;
                slot.minV            = minV;
                slot.maxV            = maxV;
                def.scalarCount += 1;
            }

            for (int r = 0; r < requiredCount; ++r)
            {
                if (!seen[static_cast<int>(required[r])])
                {
                    DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: skill '{}' missing scalar '{}'", name, scalarName(required[r]));
                    return false;
                }
            }
            return true;
        }

        bool xpAllowed(const SkillCatalog& curve, int level, float xp)
        {
            if (!std::isfinite(xp) || xp < 0.0f)
                return false;
            if (level >= curve.maxLevel())
                return xp == 0.0f;
            return xp < curve.xpToNext(level);
        }

        bool readGrant(const json& obj, const char* profileId, uint8_t& mask)
        {
            const auto it = obj.find("grant");
            if (it == obj.end())
            {
                mask = 0x3Fu;
                return true;
            }
            if (!it->is_array())
            {
                DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: profile '{}' grant is not an array", profileId);
                return false;
            }
            mask = 0;
            bool seen[kSkillCount]{};
            for (const json& item : *it)
            {
                if (!item.is_string())
                {
                    DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: profile '{}' grant token is not a string", profileId);
                    return false;
                }
                const auto* text = item.get_ptr<const json::string_t*>();
                SkillId     id   = SkillId::Count;
                if (!text || !skillFromName(*text, id))
                {
                    DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: profile '{}' unknown grant '{}'", profileId, text ? text->c_str() : "");
                    return false;
                }
                const int idx = static_cast<int>(id);
                if (seen[idx])
                {
                    DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: profile '{}' duplicate grant '{}'", profileId, text->c_str());
                    return false;
                }
                seen[idx] = true;
                mask      = static_cast<uint8_t>(mask | static_cast<uint8_t>(1u << static_cast<unsigned>(idx)));
            }
            return true;
        }

        enum class ContentFile
        {
            Missing,
            Ok,
            Invalid,
        };

        struct LoadedText
        {
            ContentFile state = ContentFile::Missing;
            fs::path    path;
            std::string text;
        };

        LoadedText readFirst(const char* fileName)
        {
            LoadedText out;
            for (const fs::path& root : contentRootCandidates())
            {
                if (root.empty())
                    continue;
                const fs::path  path = root / "skills" / fileName;
                std::error_code ec;
                if (!fs::is_regular_file(path, ec) || ec)
                    continue;
                const auto sz = fs::file_size(path, ec);
                if (ec || sz > kMaxJsonBytes)
                {
                    DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: unreadable or oversized '{}'", path.string());
                    out.state = ContentFile::Invalid;
                    out.path  = path;
                    return out;
                }
                std::ifstream in(path, std::ios::binary);
                if (!in)
                {
                    DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: failed to open '{}'", path.string());
                    out.state = ContentFile::Invalid;
                    out.path  = path;
                    return out;
                }
                out.text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                out.state = ContentFile::Ok;
                out.path  = path;
                return out;
            }
            return out;
        }

        const json* parseRoot(std::string_view jsonText, json& root)
        {
            if (jsonText.empty() || jsonText.size() > kMaxJsonBytes)
            {
                DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: empty or oversized json");
                return nullptr;
            }
            root = json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
            if (root.is_discarded() || !root.is_object())
            {
                DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: discarded or non-object json");
                return nullptr;
            }
            return &root;
        }

        bool readFileVersion(const json& doc)
        {
            const auto verIt  = doc.find("version");
            double     ver    = 0.0;
            int        verInt = 0;
            if (verIt == doc.end() || !jsonToDouble(*verIt, ver) || !isExactInt(ver, verInt) || verInt != kSkillFileVersion)
            {
                DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: unsupported version");
                return false;
            }
            return true;
        }

        // True the first time. False, after logging, when idText repeats.
        bool noteUnknownId(std::vector<std::string>& seen, const char* kind, const std::string& idText)
        {
            if (std::find(seen.begin(), seen.end(), idText) != seen.end())
            {
                DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: duplicate {} id '{}'", kind, idText);
                return false;
            }
            seen.push_back(idText);
            DE_LOG_WARN(LogCategory::Core, "SkillCatalog: skipping unknown {} id '{}'", kind, idText);
            return true;
        }
    } // namespace

    SkillCatalog::SkillCatalog()
    {
        SkillDef& shoot  = m_skills[static_cast<int>(SkillId::Shoot)];
        shoot.id         = SkillId::Shoot;
        shoot.xpPerEvent = 5.0f;
        setScalar(shoot, SkillScalar::RecoilScale, kRecoilScaleMin, kRecoilScaleMin, kRecoilScaleMax);
        setScalar(shoot, SkillScalar::CooldownScale, kCooldownScaleMin, kCooldownScaleMin, kCooldownScaleMax);

        SkillDef& swim  = m_skills[static_cast<int>(SkillId::Swim)];
        swim.id         = SkillId::Swim;
        swim.xpPerMetre = 0.50f;
        setScalar(swim, SkillScalar::SwimScale, kSwimScaleMax, kSwimScaleMin, kSwimScaleMax);

        SkillDef& run  = m_skills[static_cast<int>(SkillId::Run)];
        run.id         = SkillId::Run;
        run.xpPerMetre = 0.25f;
        setScalar(run, SkillScalar::RunScale, kRunScaleMax, kRunScaleMin, kRunScaleMax);

        SkillDef& jump  = m_skills[static_cast<int>(SkillId::Jump)];
        jump.id         = SkillId::Jump;
        jump.xpPerEvent = 6.0f;
        setScalar(jump, SkillScalar::JumpScale, kJumpScaleMax, kJumpScaleMin, kJumpScaleMax);

        SkillDef& hear         = m_skills[static_cast<int>(SkillId::Hear)];
        hear.id                = SkillId::Hear;
        hear.xpPerSecond       = 2.0f;
        hear.xpPerSecondPlayer = 1.5f;
        setScalar(hear, SkillScalar::HearScale, kHearScaleMax, kHearScaleMin, kHearScaleMax);

        SkillDef& see         = m_skills[static_cast<int>(SkillId::See)];
        see.id                = SkillId::See;
        see.xpPerSecond       = 2.0f;
        see.xpPerSecondPlayer = 1.5f;
        setScalar(see, SkillScalar::SeeRangeScale, kSeeRangeScaleMax, kSeeRangeScaleMin, kSeeRangeScaleMax);
        setScalar(see, SkillScalar::SeeConeScale, kSeeConeScaleMax, kSeeConeScaleMin, kSeeConeScaleMax);

        const uint8_t npc       = npcGrantMask();
        m_profiles[1].grantMask = npc;
        m_profiles[2].grantMask = npc;
    }

    bool SkillCatalog::parseSkills(std::string_view jsonText)
    {
        json        root;
        const json* doc = parseRoot(jsonText, root);
        if (!doc)
            return false;

        if (!readFileVersion(*doc))
            return false;

        bool       enabled   = true;
        const auto enabledIt = doc->find("enabled");
        if (enabledIt != doc->end())
        {
            if (!enabledIt->is_boolean())
            {
                DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: enabled is not a bool");
                return false;
            }
            if (const auto* b = enabledIt->get_ptr<const json::boolean_t*>())
                enabled = *b;
        }

        const auto levelIt  = doc->find("maxLevel");
        double     levelD   = 0.0;
        int        maxLevel = 0;
        if (levelIt == doc->end() || !jsonToDouble(*levelIt, levelD) || !isExactInt(levelD, maxLevel) || maxLevel != kSkillMaxLevel)
        {
            DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: maxLevel must be {}", kSkillMaxLevel);
            return false;
        }

        const auto baseIt = doc->find("xpBase");
        double     baseD  = 0.0;
        if (baseIt == doc->end() || !jsonToDouble(*baseIt, baseD) || baseD < static_cast<double>(kXpBaseMin) || baseD > static_cast<double>(kXpBaseMax))
        {
            DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: xpBase out of range");
            return false;
        }
        const float xpBase = static_cast<float>(baseD);

        const auto capIt = doc->find("xpDtCap");
        double     capD  = 0.0;
        if (capIt == doc->end() || !jsonToDouble(*capIt, capD) || !(capD > 0.0) || capD > static_cast<double>(kXpDtCapMax))
        {
            DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: xpDtCap out of range");
            return false;
        }
        const float xpDtCap = static_cast<float>(capD);
        // A positive double below the smallest float becomes 0.
        if (xpDtCap <= 0.0f)
        {
            DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: xpDtCap out of range");
            return false;
        }

        const auto skillsIt = doc->find("skills");
        if (skillsIt == doc->end() || !skillsIt->is_array())
        {
            DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: skills must be an array");
            return false;
        }

        SkillDef                 parsed[kSkillCount]{};
        bool                     seen[kSkillCount]{};
        std::vector<std::string> unknown;
        for (const json& item : *skillsIt)
        {
            if (!item.is_object())
            {
                DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: skill entry is not an object");
                return false;
            }
            std::string idText;
            if (!readString(item, "id", idText) || idText.empty())
            {
                DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: skill entry missing id");
                return false;
            }
            SkillId id = SkillId::Count;
            if (!skillFromName(idText, id))
            {
                if (!noteUnknownId(unknown, "skill", idText))
                    return false;
                continue;
            }
            const int idx = static_cast<int>(id);
            if (seen[idx])
            {
                DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: duplicate skill id '{}'", idText);
                return false;
            }
            SkillDef def;
            def.id = id;
            if (!fillSkill(item, def))
                return false;
            seen[idx]   = true;
            parsed[idx] = def;
        }

        for (int i = 0; i < kSkillCount; ++i)
        {
            if (!seen[i])
            {
                DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: missing required skill '{}'", skillIdName(static_cast<SkillId>(i)));
                return false;
            }
        }

        m_enabled  = enabled;
        m_maxLevel = maxLevel;
        m_xpBase   = xpBase;
        m_xpDtCap  = xpDtCap;
        for (int i = 0; i < kSkillCount; ++i)
            m_skills[i] = parsed[i];
        return true;
    }

    bool SkillCatalog::parseProfiles(std::string_view jsonText)
    {
        json        root;
        const json* doc = parseRoot(jsonText, root);
        if (!doc)
            return false;

        if (!readFileVersion(*doc))
            return false;

        const auto profilesIt = doc->find("profiles");
        if (profilesIt == doc->end() || !profilesIt->is_array())
        {
            DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: profiles must be an array");
            return false;
        }

        SkillProfile             parsed[kProfileSlots]{};
        bool                     filled[kProfileSlots]{};
        std::vector<std::string> unknownProfiles;
        for (const json& item : *profilesIt)
        {
            if (!item.is_object())
            {
                DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: profile entry is not an object");
                return false;
            }
            std::string idText;
            if (!readString(item, "id", idText) || idText.empty())
            {
                DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: profile missing id");
                return false;
            }
            int slot = -1;
            for (int i = 0; i < kProfileSlots; ++i)
            {
                if (idText == kProfileNames[i])
                {
                    slot = i;
                    break;
                }
            }
            if (slot < 0)
            {
                if (!noteUnknownId(unknownProfiles, "profile", idText))
                    return false;
                continue;
            }
            if (filled[slot])
            {
                DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: duplicate profile id '{}'", idText);
                return false;
            }

            SkillProfile prof;
            if (!readGrant(item, idText.c_str(), prof.grantMask))
                return false;

            const auto skillsIt = item.find("skills");
            if (skillsIt != item.end())
            {
                if (!skillsIt->is_object())
                {
                    DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: profile '{}' skills is not an object", idText);
                    return false;
                }
                for (auto it = skillsIt->begin(); it != skillsIt->end(); ++it)
                {
                    const std::string& key = it.key();
                    SkillId            id  = SkillId::Count;
                    if (!skillFromName(key, id))
                    {
                        DE_LOG_WARN(LogCategory::Core, "SkillCatalog: profile '{}' skipping unknown skill id '{}'", idText, key);
                        continue;
                    }
                    if (!it.value().is_object())
                    {
                        DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: profile '{}' skill '{}' is not an object", idText, key);
                        return false;
                    }
                    const json& body    = it.value();
                    const auto  levelIt = body.find("level");
                    const auto  xpIt    = body.find("xp");
                    double      levelD  = 0.0;
                    double      xpD     = 0.0;
                    int         level   = 0;
                    if (levelIt == body.end() || xpIt == body.end() || !jsonToDouble(*levelIt, levelD) || !isExactInt(levelD, level) || !jsonToDouble(*xpIt, xpD))
                    {
                        DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: profile '{}' skill '{}' bad level", idText, key);
                        return false;
                    }
                    if (level < 1 || level > m_maxLevel)
                    {
                        DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: profile '{}' skill '{}' bad level", idText, key);
                        return false;
                    }
                    const float xp = static_cast<float>(xpD);
                    if (!xpAllowed(*this, level, xp))
                    {
                        DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: profile '{}' skill '{}' bad xp", idText, key);
                        return false;
                    }
                    const int idx   = static_cast<int>(id);
                    prof.level[idx] = level;
                    prof.xp[idx]    = xp;
                }
            }

            filled[slot] = true;
            parsed[slot] = prof;
        }

        for (int i = 0; i < kProfileSlots; ++i)
        {
            if (!filled[i])
            {
                DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: missing profile '{}'", std::string(kProfileNames[i]));
                return false;
            }
        }

        for (int i = 0; i < kProfileSlots; ++i)
            m_profiles[i] = parsed[i];
        return true;
    }

    bool SkillCatalog::loadFromContent()
    {
        if (m_loaded)
            return m_loadOk;

        const LoadedText skills   = readFirst("skills.json");
        const LoadedText profiles = readFirst("profiles.json");

        bool ok = false;
        if (skills.state == ContentFile::Missing && profiles.state == ContentFile::Missing)
        {
            DE_LOG_WARN(LogCategory::Core, "SkillCatalog: skills.json and profiles.json missing; keeping built-in defaults");
            ok = true;
        }
        else if (skills.state != ContentFile::Ok || profiles.state != ContentFile::Ok)
        {
            if (skills.state == ContentFile::Missing)
                DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: skills/skills.json missing; keeping the previous catalog");
            else if (profiles.state == ContentFile::Missing)
                DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: skills/profiles.json missing; keeping the previous catalog");
            else
                DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: load failed; keeping the previous catalog");
            ok = false;
        }
        else
        {
            SkillCatalog temp;
            if (!temp.parseSkills(skills.text) || !temp.parseProfiles(profiles.text))
            {
                DE_LOG_ERROR(LogCategory::Core, "SkillCatalog: load failed; keeping the previous catalog");
                ok = false;
            }
            else
            {
                m_enabled  = temp.m_enabled;
                m_maxLevel = temp.m_maxLevel;
                m_xpBase   = temp.m_xpBase;
                m_xpDtCap  = temp.m_xpDtCap;
                for (int i = 0; i < kSkillCount; ++i)
                    m_skills[i] = temp.m_skills[i];
                for (int i = 0; i < kProfileSlots; ++i)
                    m_profiles[i] = temp.m_profiles[i];
                DE_LOG_INFO(LogCategory::Core, "SkillCatalog: loaded '{}' and '{}'", skills.path.string(), profiles.path.string());
                ok = true;
            }
        }

        m_loaded = true;
        m_loadOk = ok;
        return ok;
    }

    float SkillCatalog::xpToNext(int level) const
    {
        // No next rank at the cap.
        if (level < 1 || level >= m_maxLevel)
            return 0.0f;
        return m_xpBase * static_cast<float>(level);
    }

    const SkillDef* SkillCatalog::find(SkillId id) const
    {
        const int index = static_cast<int>(id);
        if (index < 0 || index >= kSkillCount)
            return nullptr;
        return &m_skills[index];
    }

    const SkillProfile* SkillCatalog::profile(std::string_view id) const
    {
        for (int i = 0; i < kProfileSlots; ++i)
        {
            if (id == kProfileNames[i])
                return &m_profiles[i];
        }
        return nullptr;
    }

    SkillCatalog& skillCatalog()
    {
        static SkillCatalog catalog;
        return catalog;
    }

    float skillScalar(SkillId id, SkillScalar scalar, int level)
    {
        const SkillCatalog& cat = skillCatalog();
        if (!cat.enabled())
            return kSkillIdentity;
        const SkillDef* def = cat.find(id);
        if (!def)
            return kSkillIdentity;

        const SkillScalarDef* found = nullptr;
        for (int i = 0; i < def->scalarCount; ++i)
        {
            if (def->scalars[i].id == scalar)
            {
                found = &def->scalars[i];
                break;
            }
        }
        if (!found)
            return kSkillIdentity;

        const int maxLevel = cat.maxLevel();
        if (level < 1)
            level = 1;
        if (level > maxLevel)
            level = maxLevel;
        const float t = static_cast<float>(level - 1) / static_cast<float>(maxLevel - 1);
        const float v = found->atLevel1 + (found->atMaxLevel - found->atLevel1) * t;
        return clampSkill(v, found->minV, found->maxV);
    }

} // namespace Dark
