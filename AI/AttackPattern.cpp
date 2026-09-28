#include "AI/AttackPattern.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"

#include "third_party/nlohmann/json.hpp"

#include <fstream>
#include <sstream>

namespace Dark::AI
{
    namespace
    {
        using json = nlohmann::json;

        constexpr std::uintmax_t kMaxJsonBytes = 64u * 1024u;

        bool jsonToFloat(const json& v, float& out)
        {
            if (const auto* f = v.get_ptr<const json::number_float_t*>())
            {
                out = static_cast<float>(*f);
                return true;
            }
            if (const auto* i = v.get_ptr<const json::number_integer_t*>())
            {
                out = static_cast<float>(*i);
                return true;
            }
            if (const auto* u = v.get_ptr<const json::number_unsigned_t*>())
            {
                out = static_cast<float>(*u);
                return true;
            }
            return false;
        }

        bool readString(const json& obj, const char* key, std::string& out)
        {
            const auto it = obj.find(key);
            if (it == obj.end())
                return false;
            if (const auto* s = it->get_ptr<const json::string_t*>())
            {
                out = *s;
                return true;
            }
            return false;
        }

        bool readFloat(const json& obj, const char* key, float& out)
        {
            const auto it = obj.find(key);
            if (it == obj.end())
                return false;
            return jsonToFloat(*it, out);
        }

        AttackKind kindFrom(std::string_view text)
        {
            if (text == "jump" || text == "pounce")
                return AttackKind::Jump;
            return AttackKind::Melee;
        }
    }

    bool parseAttackPattern(std::string_view jsonText, AttackPattern& out)
    {
        if (jsonText.empty() || jsonText.size() > kMaxJsonBytes)
        {
            DE_LOG_ERROR(LogCategory::AI, "AttackPattern: empty or oversized json");
            return false;
        }
        const json root = json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            DE_LOG_ERROR(LogCategory::AI, "AttackPattern: discarded or non-object json");
            return false;
        }
        const auto verIt = root.find("version");
        if (verIt != root.end())
        {
            float ver = 0.0f;
            if (!jsonToFloat(*verIt, ver) || static_cast<int>(ver) != 1)
            {
                DE_LOG_ERROR(LogCategory::AI, "AttackPattern: unsupported version");
                return false;
            }
        }

        AttackPattern parsed;
        readString(root, "id", parsed.id);
        readFloat(root, "gapSeconds", parsed.gapSeconds);
        readFloat(root, "pauseSeconds", parsed.pauseSeconds);
        readFloat(root, "repositionSeconds", parsed.repositionSeconds);
        readFloat(root, "repositionDistance", parsed.repositionDistance);
        readFloat(root, "packGapSeconds", parsed.packGapSeconds);
        float chain = static_cast<float>(parsed.chainCount);
        if (readFloat(root, "chainCount", chain))
            parsed.chainCount = static_cast<int>(chain);
        std::string after;
        if (readString(root, "afterChain", after) && after == "reposition")
            parsed.afterChain = AttackAfterChain::Reposition;

        if (parsed.gapSeconds < 0.0f || parsed.pauseSeconds < 0.0f || parsed.chainCount < 1 || parsed.repositionDistance < 0.0f)
        {
            DE_LOG_ERROR(LogCategory::AI, "AttackPattern: bad cadence for '{}'", parsed.id);
            return false;
        }

        const auto moves = root.find("attacks");
        if (moves == root.end() || !moves->is_array() || moves->empty())
        {
            DE_LOG_ERROR(LogCategory::AI, "AttackPattern: missing attacks");
            return false;
        }
        for (const json& item : *moves)
        {
            if (!item.is_object())
                continue;
            AttackMove move;
            if (!readString(item, "id", move.id) || move.id.empty())
                continue;
            std::string kind;
            if (readString(item, "kind", kind))
                move.kind = kindFrom(kind);
            readFloat(item, "minRange", move.minRange);
            readFloat(item, "maxRange", move.maxRange);
            readFloat(item, "windup", move.windup);
            readFloat(item, "damage", move.damage);
            readFloat(item, "stunSeconds", move.stunSeconds);
            readFloat(item, "poise", move.poise);
            if (move.maxRange < move.minRange || move.windup < 0.0f || move.damage < 0.0f || move.stunSeconds < 0.0f)
            {
                DE_LOG_ERROR(LogCategory::AI, "AttackPattern: bad move '{}'", move.id);
                return false;
            }
            parsed.attacks.push_back(std::move(move));
        }
        if (parsed.attacks.empty())
        {
            DE_LOG_ERROR(LogCategory::AI, "AttackPattern: no usable attacks");
            return false;
        }
        parsed.loaded = true;
        out = std::move(parsed);
        return true;
    }

    bool loadAttackPatternFile(const std::filesystem::path& path, AttackPattern& out)
    {
        std::error_code ec;
        if (path.empty() || !std::filesystem::is_regular_file(path, ec) || ec)
            return false;
        const auto sz = std::filesystem::file_size(path, ec);
        if (ec || sz > kMaxJsonBytes)
            return false;
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        return parseAttackPattern(ss.str(), out);
    }

    AttackPattern loadAttackPattern(std::string_view virtualPath)
    {
        AttackPattern pattern;
        if (virtualPath.empty())
            return pattern;
        const std::filesystem::path rel{ std::string(virtualPath) };
        for (const std::filesystem::path& root : contentRootCandidates())
        {
            if (root.empty())
                continue;
            const std::filesystem::path path = root / rel;
            if (loadAttackPatternFile(path, pattern))
            {
                DE_LOG_INFO(LogCategory::AI, "AttackPattern: loaded '{}'", path.string());
                return pattern;
            }
        }
        return {};
    }

    void tickAttackClock(AttackClock& clock, float dt)
    {
        if (dt < 0.0f)
            dt = 0.0f;
        if (clock.gap > 0.0f)
            clock.gap = clock.gap > dt ? clock.gap - dt : 0.0f;
        if (clock.pause > 0.0f)
            clock.pause = clock.pause > dt ? clock.pause - dt : 0.0f;
        if (clock.reposition > 0.0f)
            clock.reposition = clock.reposition > dt ? clock.reposition - dt : 0.0f;
    }

    bool attackClockReady(const AttackClock& clock, float packGap)
    {
        return clock.gap <= 0.0f && clock.pause <= 0.0f && clock.reposition <= 0.0f && packGap <= 0.0f;
    }

    void noteAttackStarted(const AttackPattern& pattern, AttackClock& clock, float& packGap)
    {
        clock.chain += 1;
        clock.gap = pattern.gapSeconds;
        if (pattern.packGapSeconds > packGap)
            packGap = pattern.packGapSeconds;
        clock.preferOther = false;
        if (clock.chain >= pattern.chainCount)
        {
            clock.chain = 0;
            if (pattern.afterChain == AttackAfterChain::Reposition)
            {
                clock.reposition  = pattern.repositionSeconds;
                clock.preferOther = true;
            }
            else
                clock.pause = pattern.pauseSeconds;
        }
    }

    int pickAttack(const AttackPattern& pattern, float distance, bool preferOther, int lastAttack)
    {
        int fallback = -1;
        int other    = -1;
        for (int i = 0; i < static_cast<int>(pattern.attacks.size()); ++i)
        {
            const AttackMove& move = pattern.attacks[static_cast<size_t>(i)];
            if (distance < move.minRange || distance > move.maxRange)
                continue;
            if (fallback < 0)
                fallback = i;
            if (i != lastAttack && other < 0)
                other = i;
        }
        if (preferOther && other >= 0)
            return other;
        return fallback;
    }
}
