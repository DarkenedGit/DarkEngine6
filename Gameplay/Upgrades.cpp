#include "Gameplay/Upgrades.h"

#include "Core/Json.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace Dark
{
    namespace
    {
        using json = nlohmann::json;

        bool fail(std::string* error, std::string message)
        {
            if (error)
                *error = std::move(message);
            return false;
        }
    } // namespace

    bool parseUpgrades(std::string_view jsonText, std::vector<UpgradeDef>& out, std::string* error)
    {
        const json root = json::parse(jsonText, nullptr, false);
        if (root.is_discarded() || !root.is_object() || !root.contains("upgrades") || !root["upgrades"].is_array())
            return fail(error, "needs an object with an \"upgrades\" list");
        std::vector<UpgradeDef> list;
        for (const json& ju : root["upgrades"])
        {
            if (!ju.is_object())
                return fail(error, "upgrade entry is not an object");
            UpgradeDef up;
            if (!jsonStringOr(ju, "id", up.id, std::string(), "Upgrades", error) || !jsonStringOr(ju, "name", up.name, up.id, "Upgrades", error)
                || !jsonStringOr(ju, "description", up.description, std::string(), "Upgrades", error)
                || !jsonStringOr(ju, "effect", up.effect, std::string(), "Upgrades", error) || !jsonFloatOr(ju, "value", up.value, 1.0f, "Upgrades", error))
                return false;
            if (ju.contains("mods") && ju["mods"].is_array())
            {
                for (const json& jm : ju["mods"])
                {
                    if (!jm.is_object())
                        return fail(error, "upgrade mod is not an object");
                    StatMod mod;
                    if (!jsonStringOr(jm, "stat", mod.stat, std::string(), "Upgrades", error) || !jsonStringOr(jm, "op", mod.op, std::string("mul"), "Upgrades", error)
                        || !jsonFloatOr(jm, "value", mod.value, 1.0f, "Upgrades", error))
                        return false;
                    if (mod.stat.empty())
                        return fail(error, "upgrade mod needs a stat");
                    if (mod.op != "mul" && mod.op != "add")
                        return fail(error, "upgrade mod op must be mul or add");
                    up.mods.push_back(std::move(mod));
                }
            }
            if (up.id.empty() || (up.effect.empty() && up.mods.empty()))
                return fail(error, "every upgrade needs an id and an effect or mods");
            if (ju.contains("cost") && ju["cost"].is_array())
            {
                for (const json& jc : ju["cost"])
                {
                    UpgradeCost c;
                    if (!jc.is_object() || !jsonStringOr(jc, "item", c.item, std::string(), "Upgrades", error) || !jsonIntOr(jc, "count", c.count, 1, "Upgrades", error))
                        return false;
                    if (!c.item.empty() && c.count > 0)
                        up.cost.push_back(std::move(c));
                }
            }
            list.push_back(std::move(up));
        }
        out = std::move(list);
        return true;
    }

    bool loadUpgrades(const std::filesystem::path& path, std::vector<UpgradeDef>& out, std::string* error)
    {
        std::error_code ec;
        if (path.empty() || !std::filesystem::is_regular_file(path, ec) || ec)
            return fail(error, "file not found: " + path.string());
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return fail(error, "cannot open: " + path.string());
        std::ostringstream ss;
        ss << in.rdbuf();
        return parseUpgrades(ss.str(), out, error);
    }

    int heldCount(const InventoryComponent& inv, std::string_view itemId)
    {
        int n = 0;
        for (const ItemStack& s : inv.bag)
            n += (!s.empty() && itemId == s.def->id) ? s.count : 0;
        return n;
    }

    bool canAfford(const InventoryComponent& inv, const UpgradeDef& up)
    {
        return std::all_of(up.cost.begin(), up.cost.end(), [&](const UpgradeCost& c) { return heldCount(inv, c.item) >= c.count; });
    }

    bool payFor(InventoryComponent& inv, const UpgradeDef& up)
    {
        if (!canAfford(inv, up))
            return false;
        for (const UpgradeCost& c : up.cost)
        {
            int left = c.count;
            for (ItemStack& s : inv.bag)
            {
                if (left <= 0)
                    break;
                if (s.empty() || c.item != s.def->id)
                    continue;
                const int take = std::min(left, s.count);
                s.count -= take;
                left -= take;
                if (s.count <= 0)
                    s = ItemStack{};
            }
        }
        return true;
    }

    namespace
    {
        float* knownStat(UpgradeEffects& fx, std::string_view stat)
        {
            if (stat == "breath")
                return &fx.breath;
            if (stat == "quiet")
                return &fx.quiet;
            if (stat == "health" || stat == "body.health")
                return &fx.health;
            if (stat == "speed" || stat == "move.speed")
                return &fx.speed;
            if (stat == "exposure" || stat == "move.exposure")
                return &fx.exposure;
            return nullptr;
        }

        void applyMod(UpgradeEffects& fx, const StatMod& mod)
        {
            if (float* slot = knownStat(fx, mod.stat))
            {
                if (mod.op == "add")
                    *slot += mod.value;
                else
                    *slot *= mod.value;
                return;
            }
            for (StatMod& extra : fx.extra)
            {
                if (extra.stat != mod.stat)
                    continue;
                if (mod.op == "add")
                    extra.value += mod.value;
                else
                    extra.value *= mod.value;
                extra.op = mod.op;
                return;
            }
            StatMod extra = mod;
            if (extra.op.empty())
                extra.op = "mul";
            fx.extra.push_back(std::move(extra));
        }

        StatMod modFromEffect(const UpgradeDef& up)
        {
            StatMod mod;
            mod.stat  = up.effect;
            mod.value = up.value;
            mod.op    = (up.effect == "health") ? "add" : "mul";
            return mod;
        }
    }

    UpgradeEffects combineUpgrades(const std::vector<UpgradeDef>& defs, const std::vector<std::string>& owned)
    {
        UpgradeEffects fx;
        for (const UpgradeDef& up : defs)
        {
            if (std::find(owned.begin(), owned.end(), up.id) == owned.end())
                continue;
            if (!up.mods.empty())
            {
                for (const StatMod& mod : up.mods)
                    applyMod(fx, mod);
            }
            else if (!up.effect.empty())
                applyMod(fx, modFromEffect(up));
        }
        return fx;
    }

    float combinedStat(const UpgradeEffects& effects, std::string_view stat)
    {
        if (stat == "breath")
            return effects.breath;
        if (stat == "quiet")
            return effects.quiet;
        if (stat == "health" || stat == "body.health")
            return effects.health;
        if (stat == "speed" || stat == "move.speed")
            return effects.speed;
        if (stat == "exposure" || stat == "move.exposure")
            return effects.exposure;
        for (const StatMod& extra : effects.extra)
        {
            if (extra.stat == stat)
                return extra.value;
        }
        return stat == "health" || stat == "body.health" ? 0.0f : 1.0f;
    }

} // namespace Dark
