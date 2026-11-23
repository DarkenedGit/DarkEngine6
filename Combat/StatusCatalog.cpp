#include "Combat/StatusDef.h"

#include "Core/Log.h"
#include "Scene/EntityMaster.h"

#include "third_party/nlohmann/json.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace Dark::Combat
{
    namespace
    {
        constexpr StatusDef kStatusCatalog[] = {
            {
                .id                   = StatusId::Stun,
                .name                 = "Stun",
                .damageType           = DamageType::True,
                .stack                = StatusStackRule::RefreshLonger,
                .maxStacks            = 1,
                .hardCc               = true,
                .ccCategory           = CcCategory::Stun,
                .defaultDuration      = 1.2f,
                .defaultMagnitude     = 1.f,
                .tickDamage           = 0.f,
                .tickInterval         = 1.f,
                .tickScalesWithStacks = false,
                .particle             = StatusParticlePreset::StunStars,
                .particleLoop         = true,
                .applyCue             = "status",
                .loopCue              = "",
                .expireCue            = "",
                .audioPriority        = 0,
                .animBool             = "stunned",
                .animTrigger          = "",
            },
            {
                .id                   = StatusId::Root,
                .name                 = "Root",
                .damageType           = DamageType::True,
                .stack                = StatusStackRule::RefreshLonger,
                .maxStacks            = 1,
                .hardCc               = false,
                .ccCategory           = CcCategory::Root,
                .defaultDuration      = 0.8f,
                .defaultMagnitude     = 1.f,
                .tickDamage           = 0.f,
                .tickInterval         = 1.f,
                .tickScalesWithStacks = false,
                .particle             = StatusParticlePreset::None,
                .particleLoop         = true,
                .applyCue             = "",
                .loopCue              = "",
                .expireCue            = "",
                .audioPriority        = 0,
                .animBool             = "",
                .animTrigger          = "",
            },
            {
                .id                   = StatusId::Fear,
                .name                 = "Fear",
                .damageType           = DamageType::True,
                .stack                = StatusStackRule::RefreshLonger,
                .maxStacks            = 1,
                .hardCc               = false,
                .ccCategory           = CcCategory::Fear,
                .defaultDuration      = 1.0f,
                .defaultMagnitude     = 1.f,
                .tickDamage           = 0.f,
                .tickInterval         = 1.f,
                .tickScalesWithStacks = false,
                .particle             = StatusParticlePreset::None,
                .particleLoop         = true,
                .applyCue             = "",
                .loopCue              = "",
                .expireCue            = "",
                .audioPriority        = 0,
                .animBool             = "",
                .animTrigger          = "",
            },
            {
                .id                   = StatusId::Knockdown,
                .name                 = "Knockdown",
                .damageType           = DamageType::True,
                .stack                = StatusStackRule::RefreshLonger,
                .maxStacks            = 1,
                .hardCc               = true,
                .ccCategory           = CcCategory::Knockdown,
                .defaultDuration      = 1.4f,
                .defaultMagnitude     = 1.f,
                .tickDamage           = 0.f,
                .tickInterval         = 1.f,
                .tickScalesWithStacks = false,
                .particle             = StatusParticlePreset::None,
                .particleLoop         = true,
                .applyCue             = "",
                .loopCue              = "",
                .expireCue            = "",
                .audioPriority        = 0,
                .animBool             = "",
                .animTrigger          = "",
            },
            {
                .id                   = StatusId::Poison,
                .name                 = "Poison",
                .damageType           = DamageType::Poison,
                .stack                = StatusStackRule::RefreshDurationMaxMag,
                .maxStacks            = 1,
                .hardCc               = false,
                .ccCategory           = CcCategory::Count,
                .defaultDuration      = 8.f,
                .defaultMagnitude     = 1.f,
                .tickDamage           = 4.f,
                .tickInterval         = 1.f,
                .tickScalesWithStacks = false,
                .particle             = StatusParticlePreset::PoisonCloud,
                .particleLoop         = true,
                .applyCue             = "status",
                .loopCue              = "",
                .expireCue            = "",
                .audioPriority        = 0,
                .animBool             = "poisoned",
                .animTrigger          = "",
            },
            {
                .id                   = StatusId::Bleed,
                .name                 = "Bleed",
                .damageType           = DamageType::Slash,
                .stack                = StatusStackRule::RefreshDurationMaxMag,
                .maxStacks            = 1,
                .hardCc               = false,
                .ccCategory           = CcCategory::Count,
                .defaultDuration      = 6.f,
                .defaultMagnitude     = 1.f,
                .tickDamage           = 2.f,
                .tickInterval         = 1.f,
                .tickScalesWithStacks = false,
                .particle             = StatusParticlePreset::BleedDrip,
                .particleLoop         = true,
                .applyCue             = "status",
                .loopCue              = "",
                .expireCue            = "",
                .audioPriority        = 0,
                .animBool             = "bleeding",
                .animTrigger          = "",
            },
            {
                .id                   = StatusId::Ignite,
                .name                 = "Ignite",
                .damageType           = DamageType::Fire,
                .stack                = StatusStackRule::RefreshDurationMaxMag,
                .maxStacks            = 1,
                .hardCc               = false,
                .ccCategory           = CcCategory::Count,
                .defaultDuration      = 6.f,
                .defaultMagnitude     = 1.f,
                .tickDamage           = 2.f,
                .tickInterval         = 1.f,
                .tickScalesWithStacks = false,
                .particle             = StatusParticlePreset::IgniteFlames,
                .particleLoop         = true,
                .applyCue             = "status",
                .loopCue              = "",
                .expireCue            = "",
                .audioPriority        = 0,
                .animBool             = "ignited",
                .animTrigger          = "",
            },
            {
                .id                   = StatusId::Chill,
                .name                 = "Chill",
                .damageType           = DamageType::Frost,
                .stack                = StatusStackRule::RefreshDurationMaxMag,
                .maxStacks            = 1,
                .hardCc               = false,
                .ccCategory           = CcCategory::Count,
                .defaultDuration      = 6.f,
                .defaultMagnitude     = 0.5f,
                .tickDamage           = 0.f,
                .tickInterval         = 1.f,
                .tickScalesWithStacks = false,
                .particle             = StatusParticlePreset::ChillMist,
                .particleLoop         = true,
                .applyCue             = "status",
                .loopCue              = "",
                .expireCue            = "",
                .audioPriority        = 0,
                .animBool             = "chilled",
                .animTrigger          = "",
            },
            {
                .id                   = StatusId::Shock,
                .name                 = "Shock",
                .damageType           = DamageType::Lightning,
                .stack                = StatusStackRule::RefreshDurationMaxMag,
                .maxStacks            = 1,
                .hardCc               = false,
                .ccCategory           = CcCategory::Count,
                .defaultDuration      = 3.f,
                .defaultMagnitude     = 1.f,
                .tickDamage           = 0.f,
                .tickInterval         = 1.f,
                .tickScalesWithStacks = false,
                .particle             = StatusParticlePreset::ShockSparks,
                .particleLoop         = true,
                .applyCue             = "status",
                .loopCue              = "",
                .expireCue            = "",
                .audioPriority        = 0,
                .animBool             = "shocked",
                .animTrigger          = "",
            },
        };

        static_assert(sizeof(kStatusCatalog) / sizeof(kStatusCatalog[0]) == static_cast<size_t>(kStatusIdCount) - 1u);

        struct MutableStatus
        {
            StatusDef   def{};
            std::string name;
            std::string applyCue;
            std::string loopCue;
            std::string expireCue;
            std::string animBool;
            std::string animTrigger;
        };

        MutableStatus gRuntime[kStatusIdCount - 1]{};
        bool          gReady = false;

        void bindStatusStrings(MutableStatus& row)
        {
            row.def.name        = row.name.c_str();
            row.def.applyCue    = row.applyCue.c_str();
            row.def.loopCue     = row.loopCue.c_str();
            row.def.expireCue   = row.expireCue.c_str();
            row.def.animBool    = row.animBool.c_str();
            row.def.animTrigger = row.animTrigger.c_str();
        }

        void copyCompiledStatuses()
        {
            for (size_t i = 0; i < sizeof(kStatusCatalog) / sizeof(kStatusCatalog[0]); ++i)
            {
                MutableStatus& row = gRuntime[i];
                row.def            = kStatusCatalog[i];
                row.name           = kStatusCatalog[i].name ? kStatusCatalog[i].name : "";
                row.applyCue       = kStatusCatalog[i].applyCue ? kStatusCatalog[i].applyCue : "";
                row.loopCue        = kStatusCatalog[i].loopCue ? kStatusCatalog[i].loopCue : "";
                row.expireCue      = kStatusCatalog[i].expireCue ? kStatusCatalog[i].expireCue : "";
                row.animBool       = kStatusCatalog[i].animBool ? kStatusCatalog[i].animBool : "";
                row.animTrigger    = kStatusCatalog[i].animTrigger ? kStatusCatalog[i].animTrigger : "";
                bindStatusStrings(row);
            }
        }

        StatusId statusIdFromName(std::string_view text)
        {
            for (const StatusDef& row : kStatusCatalog)
            {
                if (row.name && text == row.name)
                    return row.id;
            }
            if (text == "stun" || text == "Stun") return StatusId::Stun;
            if (text == "root" || text == "Root") return StatusId::Root;
            if (text == "fear" || text == "Fear") return StatusId::Fear;
            if (text == "knockdown" || text == "Knockdown") return StatusId::Knockdown;
            if (text == "poison" || text == "Poison") return StatusId::Poison;
            if (text == "bleed" || text == "Bleed") return StatusId::Bleed;
            if (text == "ignite" || text == "Ignite") return StatusId::Ignite;
            if (text == "chill" || text == "Chill") return StatusId::Chill;
            if (text == "shock" || text == "Shock") return StatusId::Shock;
            return StatusId::None;
        }

        void overlayStatusFile()
        {
            const std::filesystem::path path = resolveContentFile("combat/statuses.json");
            if (path.empty())
                return;
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return;
            std::ostringstream ss;
            ss << in.rdbuf();
            const nlohmann::json root = nlohmann::json::parse(ss.str(), nullptr, false);
            if (root.is_discarded() || !root.is_object() || !root.contains("statuses") || !root["statuses"].is_array())
            {
                DE_LOG_WARN(LogCategory::Core, "Status: combat/statuses.json is not a statuses list");
                return;
            }
            for (const nlohmann::json& item : root["statuses"])
            {
                if (!item.is_object())
                    continue;
                const std::string id = item.contains("id") && item["id"].is_string() ? item["id"].get<std::string>() : std::string();
                const StatusId status = statusIdFromName(id);
                if (status == StatusId::None)
                {
                    DE_LOG_WARN(LogCategory::Core, "Status: '{}' is not a registered status mechanic", id);
                    continue;
                }
                MutableStatus& row = gRuntime[static_cast<int>(status) - 1];
                if (item.contains("name") && item["name"].is_string())
                    row.name = item["name"].get<std::string>();
                if (item.contains("duration") && item["duration"].is_number())
                    row.def.defaultDuration = item["duration"].get<float>();
                if (item.contains("magnitude") && item["magnitude"].is_number())
                    row.def.defaultMagnitude = item["magnitude"].get<float>();
                if (item.contains("tickDamage") && item["tickDamage"].is_number())
                    row.def.tickDamage = item["tickDamage"].get<float>();
                if (item.contains("tickInterval") && item["tickInterval"].is_number())
                    row.def.tickInterval = item["tickInterval"].get<float>();
                if (item.contains("maxStacks") && item["maxStacks"].is_number())
                    row.def.maxStacks = static_cast<uint8_t>(item["maxStacks"].get<int>());
                bindStatusStrings(row);
            }
        }
    } // namespace

    void reloadStatusCatalog()
    {
        copyCompiledStatuses();
        overlayStatusFile();
        gReady = true;
    }

    const StatusDef* statusDef(StatusId id)
    {
        if (!gReady)
            reloadStatusCatalog();
        const int i = static_cast<int>(id);
        if (i <= 0 || i >= kStatusIdCount)
            return nullptr;
        const StatusDef& row = gRuntime[i - 1].def;
        if (row.id != id)
            return nullptr;
        return &row;
    }

} // namespace Dark::Combat
