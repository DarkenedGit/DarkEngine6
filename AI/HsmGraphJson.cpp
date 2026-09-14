#include "AI/HsmGraphJson.h"
#include "Core/Log.h"

#include "third_party/nlohmann/json.hpp"

#include <fstream>

namespace Dark
{
    using json = nlohmann::json;
    using ojson = nlohmann::ordered_json;

    namespace
    {
        const char* historyName(AI::HsmHistory h)
        {
            switch (h)
            {
            case AI::HsmHistory::Shallow: return "shallow";
            case AI::HsmHistory::Deep:    return "deep";
            case AI::HsmHistory::None:
            default:                      return "none";
            }
        }

        AI::HsmHistory parseHistory(const std::string& s)
        {
            if (s == "shallow")
                return AI::HsmHistory::Shallow;
            if (s == "deep")
                return AI::HsmHistory::Deep;
            return AI::HsmHistory::None;
        }

        const char* kindName(AI::HsmTransitionKind k)
        {
            switch (k)
            {
            case AI::HsmTransitionKind::Local:    return "local";
            case AI::HsmTransitionKind::Internal: return "internal";
            case AI::HsmTransitionKind::External:
            default:                              return "external";
            }
        }

        AI::HsmTransitionKind parseKind(const std::string& s)
        {
            if (s == "local")
                return AI::HsmTransitionKind::Local;
            if (s == "internal")
                return AI::HsmTransitionKind::Internal;
            return AI::HsmTransitionKind::External;
        }

        bool parseFrom(const json& jfrom, std::vector<std::string>& out)
        {
            out.clear();
            if (jfrom.is_string())
            {
                out.push_back(jfrom.get<std::string>());
                return !out.back().empty();
            }
            if (!jfrom.is_array() || jfrom.empty())
                return false;
            for (const json& item : jfrom)
            {
                if (!item.is_string())
                    return false;
                out.push_back(item.get<std::string>());
            }
            return !out.empty();
        }
    } // namespace

    bool parseHsmGraphJson(const char* jsonText, HsmGraphDef& out)
    {
        out.name.clear();
        out.root.clear();
        out.events.clear();
        out.params.clear();
        out.states.clear();
        out.transitions.clear();
        if (!jsonText || jsonText[0] == '\0')
        {
            DE_LOG_ERROR(LogCategory::AI, "HsmGraph: empty json");
            return false;
        }
        const json root = json::parse(jsonText, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            DE_LOG_ERROR(LogCategory::AI, "HsmGraph: discarded or non-object json");
            return false;
        }
        if (root.contains("version"))
        {
            if (!root["version"].is_number_integer() || root["version"].get<int>() != 1)
            {
                DE_LOG_ERROR(LogCategory::AI, "HsmGraph: unsupported version");
                return false;
            }
        }
        if (root.contains("name") && root["name"].is_string())
            out.name = root["name"].get<std::string>();
        if (!root.contains("root") || !root["root"].is_string())
        {
            DE_LOG_ERROR(LogCategory::AI, "HsmGraph: root required");
            return false;
        }
        out.root = root["root"].get<std::string>();

        if (root.contains("events") && root["events"].is_array())
        {
            AI::HsmEventId nextId = 10;
            for (const json& je : root["events"])
            {
                if (!je.is_object() || !je.contains("name") || !je["name"].is_string())
                {
                    DE_LOG_ERROR(LogCategory::AI, "HsmGraph: event missing name");
                    return false;
                }
                HsmEventDef e;
                e.name = je["name"].get<std::string>();
                if (je.contains("id") && je["id"].is_number_integer())
                    e.id = static_cast<AI::HsmEventId>(je["id"].get<int>());
                else
                    e.id = nextId++;
                if (e.id == AI::kHsmEventNone || e.id == AI::kHsmEventTick)
                {
                    DE_LOG_ERROR(LogCategory::AI, "HsmGraph: event '{}' uses reserved id", e.name);
                    return false;
                }
                out.events.push_back(std::move(e));
            }
        }

        if (root.contains("parameters") && root["parameters"].is_array())
        {
            for (const json& jp : root["parameters"])
            {
                if (!jp.is_object() || !jp.contains("name") || !jp["name"].is_string())
                {
                    DE_LOG_ERROR(LogCategory::AI, "HsmGraph: parameter missing name");
                    return false;
                }
                HsmParamDef p;
                p.name = jp["name"].get<std::string>();
                if (jp.contains("default") && jp["default"].is_number())
                    p.defaultFloat = jp["default"].get<float>();
                out.params.push_back(std::move(p));
            }
        }

        if (!root.contains("states") || !root["states"].is_array() || root["states"].empty())
        {
            DE_LOG_ERROR(LogCategory::AI, "HsmGraph: states array required");
            return false;
        }
        for (const json& js : root["states"])
        {
            if (!js.is_object() || !js.contains("name") || !js["name"].is_string())
            {
                DE_LOG_ERROR(LogCategory::AI, "HsmGraph: state missing name");
                return false;
            }
            HsmStateDef st;
            st.name = js["name"].get<std::string>();
            if (js.contains("parent") && js["parent"].is_string())
                st.parent = js["parent"].get<std::string>();
            if (js.contains("initial") && js["initial"].is_string())
                st.initial = js["initial"].get<std::string>();
            if (js.contains("base") && js["base"].is_string())
                st.base = js["base"].get<std::string>();
            if (js.contains("history") && js["history"].is_string())
                st.history = parseHistory(js["history"].get<std::string>());
            if (js.contains("inheritEntryExit") && js["inheritEntryExit"].is_boolean())
                st.inheritEntryExit = js["inheritEntryExit"].get<bool>();
            if (js.contains("onEnter") && js["onEnter"].is_string())
                st.onEnter = js["onEnter"].get<std::string>();
            if (js.contains("onExit") && js["onExit"].is_string())
                st.onExit = js["onExit"].get<std::string>();
            out.states.push_back(std::move(st));
        }
        if (out.findStateIndex(out.root) < 0)
        {
            DE_LOG_ERROR(LogCategory::AI, "HsmGraph: unknown root '{}'", out.root);
            return false;
        }

        if (root.contains("transitions") && root["transitions"].is_array())
        {
            for (const json& jt : root["transitions"])
            {
                if (!jt.is_object() || !jt.contains("from") || !jt.contains("to") || !jt["to"].is_string() || !jt.contains("event") || !jt["event"].is_string())
                {
                    DE_LOG_ERROR(LogCategory::AI, "HsmGraph: transition requires from, to, event");
                    return false;
                }
                HsmTransitionDef t;
                if (!parseFrom(jt["from"], t.from))
                {
                    DE_LOG_ERROR(LogCategory::AI, "HsmGraph: transition from must be a name, '*' or array");
                    return false;
                }
                t.to    = jt["to"].get<std::string>();
                t.event = jt["event"].get<std::string>();
                if (jt.contains("guard") && jt["guard"].is_string())
                    t.guard = jt["guard"].get<std::string>();
                if (jt.contains("action") && jt["action"].is_string())
                    t.action = jt["action"].get<std::string>();
                if (jt.contains("kind") && jt["kind"].is_string())
                    t.kind = parseKind(jt["kind"].get<std::string>());
                if (out.findEventIndex(t.event) < 0)
                {
                    DE_LOG_ERROR(LogCategory::AI, "HsmGraph: unknown event '{}'", t.event);
                    return false;
                }
                if (t.to != "*" && out.findStateIndex(t.to) < 0)
                {
                    DE_LOG_ERROR(LogCategory::AI, "HsmGraph: unknown transition to '{}'", t.to);
                    return false;
                }
                out.transitions.push_back(std::move(t));
            }
        }
        return !out.states.empty();
    }

    bool writeHsmGraphJson(const HsmGraphDef& def, std::string& outText)
    {
        outText.clear();
        if (def.states.empty() || def.root.empty())
        {
            DE_LOG_ERROR(LogCategory::AI, "HsmGraph: cannot write empty graph");
            return false;
        }
        ojson root = ojson::object();
        root["version"] = 1;
        if (!def.name.empty())
            root["name"] = def.name;
        root["root"] = def.root;

        if (!def.events.empty())
        {
            ojson events = ojson::array();
            for (const HsmEventDef& e : def.events)
            {
                ojson je = ojson::object();
                je["name"] = e.name;
                je["id"]   = e.id;
                events.push_back(std::move(je));
            }
            root["events"] = std::move(events);
        }
        if (!def.params.empty())
        {
            ojson params = ojson::array();
            for (const HsmParamDef& p : def.params)
            {
                ojson jp = ojson::object();
                jp["name"]    = p.name;
                jp["default"] = p.defaultFloat;
                params.push_back(std::move(jp));
            }
            root["parameters"] = std::move(params);
        }

        ojson states = ojson::array();
        for (const HsmStateDef& st : def.states)
        {
            ojson js = ojson::object();
            js["name"] = st.name;
            if (!st.parent.empty())
                js["parent"] = st.parent;
            if (!st.initial.empty())
                js["initial"] = st.initial;
            if (!st.base.empty())
                js["base"] = st.base;
            if (st.history != AI::HsmHistory::None)
                js["history"] = historyName(st.history);
            if (!st.inheritEntryExit)
                js["inheritEntryExit"] = false;
            if (!st.onEnter.empty())
                js["onEnter"] = st.onEnter;
            if (!st.onExit.empty())
                js["onExit"] = st.onExit;
            states.push_back(std::move(js));
        }
        root["states"] = std::move(states);

        if (!def.transitions.empty())
        {
            ojson transitions = ojson::array();
            for (const HsmTransitionDef& t : def.transitions)
            {
                ojson jt = ojson::object();
                if (t.from.size() == 1)
                    jt["from"] = t.from[0];
                else
                    jt["from"] = t.from;
                jt["to"]    = t.to;
                jt["event"] = t.event;
                if (!t.guard.empty())
                    jt["guard"] = t.guard;
                if (!t.action.empty())
                    jt["action"] = t.action;
                if (t.kind != AI::HsmTransitionKind::External)
                    jt["kind"] = kindName(t.kind);
                transitions.push_back(std::move(jt));
            }
            root["transitions"] = std::move(transitions);
        }

        outText = root.dump(2);
        outText += '\n';
        return true;
    }

    bool saveHsmGraphJsonFile(const HsmGraphDef& def, const std::filesystem::path& path)
    {
        std::string text;
        if (!writeHsmGraphJson(def, text))
            return false;
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            DE_LOG_ERROR(LogCategory::AI, "HsmGraph: cannot write '{}'", path.string());
            return false;
        }
        out << text;
        if (!out)
        {
            DE_LOG_ERROR(LogCategory::AI, "HsmGraph: write failed '{}'", path.string());
            return false;
        }
        DE_LOG_INFO(LogCategory::AI, "HsmGraph: saved '{}'", path.string());
        return true;
    }
} // namespace Dark
