#include "Scene/EntityCombat.h"

#include "Core/Log.h"
#include "Scene/EntityMaster.h"

#include "third_party/nlohmann/json.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

namespace Dark
{
    namespace
    {
        using json = nlohmann::json;

        constexpr std::uintmax_t kMaxJsonBytes = 64u * 1024u;

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

        bool takeFloat(const json& obj, const char* key, float& dest, bool allowNegative)
        {
            const auto it = obj.find(key);
            if (it == obj.end())
                return false;
            float v = 0.0f;
            if (!jsonToFloat(*it, v) || !std::isfinite(v) || (!allowNegative && v < 0.0f))
            {
                DE_LOG_WARN(LogCategory::Core, "EntityCombat: ignoring '{}'", key);
                return false;
            }
            dest = v;
            return true;
        }

        bool takeInt(const json& obj, const char* key, int& dest, int lo, int hi)
        {
            const auto it = obj.find(key);
            if (it == obj.end())
                return false;
            float v = 0.0f;
            if (!jsonToFloat(*it, v) || !std::isfinite(v))
            {
                DE_LOG_WARN(LogCategory::Core, "EntityCombat: ignoring '{}'", key);
                return false;
            }
            const int n = static_cast<int>(v);
            if (n < lo || n > hi)
            {
                DE_LOG_WARN(LogCategory::Core, "EntityCombat: ignoring '{}'", key);
                return false;
            }
            dest = n;
            return true;
        }

        void overlay(const json& root, EntityCombat& parsed)
        {
            takeFloat(root, "healthMax", parsed.healthMax, false);
            takeFloat(root, "healthRegen", parsed.healthRegen, false);
            takeFloat(root, "healthRegenDelay", parsed.healthRegenDelay, false);
            takeFloat(root, "poiseMax", parsed.poiseMax, false);
            takeFloat(root, "poiseRegen", parsed.poiseRegen, false);
            takeFloat(root, "poiseRegenDelay", parsed.poiseRegenDelay, false);
            takeFloat(root, "duelStandoff", parsed.duelStandoff, false);
            takeFloat(root, "duelEngage", parsed.duelEngage, false);
            takeFloat(root, "duelRing", parsed.duelRing, false);
            takeFloat(root, "duelPauseMin", parsed.duelPauseMin, false);
            takeFloat(root, "duelPauseMax", parsed.duelPauseMax, false);
            takeFloat(root, "bodyAlert", parsed.bodyAlert, false);
            takeFloat(root, "missingSeconds", parsed.missingSeconds, false);
            takeFloat(root, "missingRange", parsed.missingRange, false);
            takeFloat(root, "patrolWait", parsed.patrolWait, false);
            takeFloat(root, "meleeReachPad", parsed.meleeReachPad, false);
            takeFloat(root, "eyeHeight", parsed.eyeHeight, false);
            takeFloat(root, "groundOffset", parsed.groundOffset, false);
            takeFloat(root, "blockChance", parsed.blockChance, false);
            takeFloat(root, "guardMin", parsed.guardMin, false);
            takeFloat(root, "guardMax", parsed.guardMax, false);
            takeInt(root, "poiseHits", parsed.poiseHits, 1, 32);
            takeFloat(root, "guardWindow", parsed.guardWindow, false);
            takeFloat(root, "guardRecover", parsed.guardRecover, false);
            takeFloat(root, "wanderRadius", parsed.wanderRadius, false);
            takeFloat(root, "wanderMin", parsed.wanderMin, false);
            takeFloat(root, "fleeMin", parsed.fleeMin, false);
            takeFloat(root, "fleeMax", parsed.fleeMax, false);
            takeFloat(root, "fleeLateral", parsed.fleeLateral, false);
            takeFloat(root, "jumpCooldown", parsed.jumpCooldown, false);
            takeFloat(root, "jumpGroundOffset", parsed.jumpGroundOffset, false);
            takeFloat(root, "jumpConnectDamage", parsed.jumpConnectDamage, false);
            takeFloat(root, "jumpConnectPoise", parsed.jumpConnectPoise, false);
            takeFloat(root, "jumpPoundDamage", parsed.jumpPoundDamage, false);
            takeFloat(root, "jumpPoundPoise", parsed.jumpPoundPoise, false);
            takeFloat(root, "jumpLeapVertical", parsed.jumpLeapVertical, false);
            takeFloat(root, "jumpLeapForwardMax", parsed.jumpLeapForwardMax, false);
            takeFloat(root, "jumpGravity", parsed.jumpGravity, false);
            takeFloat(root, "jumpHoming", parsed.jumpHoming, false);
            takeFloat(root, "jumpKnockdown", parsed.jumpKnockdown, false);
            takeFloat(root, "jumpKnockdownForce", parsed.jumpKnockdownForce, false);
            takeFloat(root, "contactStandoff", parsed.contactStandoff, false);
            takeFloat(root, "contactDps", parsed.contactDps, false);
            takeFloat(root, "respawnSeconds", parsed.respawnSeconds, false);
            takeInt(root, "team", parsed.team, 0, 255);
            takeFloat(root, "parryStun", parsed.parryStun, false);
            takeFloat(root, "chargedParryStun", parsed.chargedParryStun, false);
            takeFloat(root, "parryKnockback", parsed.parryKnockback, false);
            takeFloat(root, "chargedParryKnockback", parsed.chargedParryKnockback, false);
            takeFloat(root, "parryKnockSeconds", parsed.parryKnockSeconds, false);
            takeFloat(root, "chargedParryKnockSeconds", parsed.chargedParryKnockSeconds, false);
            takeFloat(root, "ccRoot", parsed.ccRoot, false);
            takeFloat(root, "ccStun", parsed.ccStun, false);
            takeFloat(root, "ccKnockdown", parsed.ccKnockdown, false);
            takeFloat(root, "shockHitch", parsed.shockHitch, false);
            takeFloat(root, "hitStopLight", parsed.hitStopLight, false);
            takeFloat(root, "hitStopHeavy", parsed.hitStopHeavy, false);
            takeFloat(root, "hitStopBlock", parsed.hitStopBlock, false);
            takeFloat(root, "hitStopParry", parsed.hitStopParry, false);
            takeFloat(root, "shakeBlock", parsed.shakeBlock, false);
            takeFloat(root, "shakeParry", parsed.shakeParry, false);
            takeFloat(root, "shakeHit", parsed.shakeHit, false);
            takeFloat(root, "blockRecoil", parsed.blockRecoil, false);
            takeFloat(root, "faceSeconds", parsed.faceSeconds, false);
            takeFloat(root, "parryInterrupt", parsed.parryInterrupt, false);
            takeFloat(root, "comboWindow", parsed.comboWindow, false);
            takeFloat(root, "swingLock", parsed.swingLock, false);
            takeFloat(root, "swingSlowRecoil", parsed.swingSlowRecoil, false);
            takeFloat(root, "swingSlowCombo", parsed.swingSlowCombo, false);
            takeFloat(root, "swingSlowSingle", parsed.swingSlowSingle, false);
            takeFloat(root, "carrySlow", parsed.carrySlow, false);
            takeFloat(root, "footstepWalk", parsed.footstepWalk, false);
            takeFloat(root, "footstepSwim", parsed.footstepSwim, false);
            takeFloat(root, "drownDps", parsed.drownDps, false);
            takeFloat(root, "takedownRange", parsed.takedownRange, false);
            takeFloat(root, "takedownHeight", parsed.takedownHeight, false);
            takeFloat(root, "takedownBackDot", parsed.takedownBackDot, true);
            takeFloat(root, "meleeVertical", parsed.meleeVertical, false);
            takeFloat(root, "swordCooldown", parsed.swordCooldown, false);
            takeFloat(root, "swordHitDelay", parsed.swordHitDelay, false);
            takeFloat(root, "shieldRaise", parsed.shieldRaise, false);
            takeFloat(root, "shieldMoveScale", parsed.shieldMoveScale, false);
            takeFloat(root, "shieldArc", parsed.shieldArc, false);
            takeFloat(root, "weaponRange", parsed.weaponRange, false);
            takeFloat(root, "muzzleOffset", parsed.muzzleOffset, false);
            takeFloat(root, "repathBackoff", parsed.repathBackoff, false);
        }
    } // namespace

    EntityCombat entityCombatDefault(std::string_view typeName)
    {
        EntityCombat combat;
        combat.type = std::string(typeName);
        if (typeName == "player")
        {
            combat.healthMax         = 100.0f;
            combat.healthRegen       = 0.0f;
            combat.healthRegenDelay  = 0.0f;
            combat.team              = 1;
            combat.jumpCooldown      = 1.25f;
            combat.jumpConnectDamage = 32.0f;
            combat.jumpPoundDamage   = 20.0f;
            combat.jumpKnockdown     = 1.4f;
            combat.jumpKnockdownForce = 2.4f;
            combat.jumpLeapVertical  = 10.0f;
            combat.jumpHoming        = 6.0f;
        }
        return combat;
    }

    std::string entityCombatVirtualPath(std::string_view typeName)
    {
        return std::string("entities/") + std::string(typeName) + ".entity.combat.json";
    }

    bool parseEntityCombat(std::string_view jsonText, EntityCombat& inOut)
    {
        if (jsonText.empty() || jsonText.size() > kMaxJsonBytes)
        {
            DE_LOG_ERROR(LogCategory::Core, "EntityCombat: empty or oversized json");
            return false;
        }
        const json root = json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            DE_LOG_ERROR(LogCategory::Core, "EntityCombat: discarded or non-object json");
            return false;
        }

        const auto verIt = root.find("version");
        if (verIt != root.end())
        {
            float ver = 0.0f;
            if (!jsonToFloat(*verIt, ver) || static_cast<int>(ver) != 1)
            {
                DE_LOG_ERROR(LogCategory::Core, "EntityCombat: unsupported version");
                return false;
            }
        }

        EntityCombat parsed = inOut;
        if (!readString(root, "type", parsed.type) || parsed.type.empty())
        {
            DE_LOG_ERROR(LogCategory::Core, "EntityCombat: missing type");
            return false;
        }
        overlay(root, parsed);
        inOut = std::move(parsed);
        return true;
    }

    bool loadEntityCombatFile(const std::filesystem::path& path, EntityCombat& inOut)
    {
        std::error_code ec;
        if (path.empty() || !std::filesystem::is_regular_file(path, ec) || ec)
            return false;
        const auto sz = std::filesystem::file_size(path, ec);
        if (ec || sz > kMaxJsonBytes)
        {
            DE_LOG_ERROR(LogCategory::Core, "EntityCombat: unreadable or too large '{}'", path.string());
            return false;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        return parseEntityCombat(ss.str(), inOut);
    }

    bool loadEntityCombatType(std::string_view typeName, EntityCombat& inOut)
    {
        if (typeName.empty())
            return false;
        const std::filesystem::path path = resolveContentFile(entityCombatVirtualPath(typeName));
        if (path.empty())
            return false;
        EntityCombat parsed = inOut;
        if (!loadEntityCombatFile(path, parsed))
            return false;
        if (parsed.type != typeName)
        {
            DE_LOG_ERROR(LogCategory::Core, "EntityCombat: '{}' type is '{}'", path.string(), parsed.type);
            return false;
        }
        DE_LOG_INFO(LogCategory::Core, "EntityCombat: {} hp {} team {}", parsed.type, parsed.healthMax, parsed.team);
        inOut = std::move(parsed);
        return true;
    }

    const EntityCombat& entityCombatFor(std::string_view typeName)
    {
        static EntityCombat hunter = entityCombatDefault("hunter");
        static EntityCombat wolf   = entityCombatDefault("wolf");
        static EntityCombat player = entityCombatDefault("player");
        static bool         loaded = false;
        if (!loaded)
        {
            loaded = true;
            loadEntityCombatType("hunter", hunter);
            loadEntityCombatType("wolf", wolf);
            loadEntityCombatType("player", player);
        }
        if (typeName == "wolf")
            return wolf;
        if (typeName == "player")
            return player;
        return hunter;
    }

} // namespace Dark
