#include "Play/DialogueGraphJson.h"

#include "Core/Json.h"
#include "Core/Log.h"

#include <fstream>
#include <unordered_set>

namespace Dark
{
    namespace
    {
        using json = nlohmann::json;

        constexpr uintmax_t kMaxGraphBytes = 256 * 1024;
        constexpr size_t    kMaxIdBytes    = 64;
        constexpr size_t    kMaxTextBytes  = 240;

        bool fail(std::string* error, const std::string& message)
        {
            if (error)
                *error = message;
            DE_LOG_ERROR(LogCategory::AI, "Dialogue: {}", message);
            return false;
        }

        bool takeId(const json& object, const char* key, std::string& out, std::unordered_set<std::string>& ids, std::string* error)
        {
            std::string detail;
            if (!jsonStringOr(object, key, out, {}, "Dialogue", &detail) || out.empty() || out.size() > kMaxIdBytes)
                return fail(error, detail.empty() ? std::string(key) + " is missing or too long" : detail);
            if (!ids.insert(out).second)
                return fail(error, "duplicate id \"" + out + "\"");
            return true;
        }

        bool readLine(const json& object, DialogueLine& line, std::unordered_set<std::string>& ids, std::string* error)
        {
            if (!object.is_object())
                return fail(error, "line is not an object");
            std::string detail;
            if (!takeId(object, "id", line.id, ids, error))
                return false;
            if (!jsonStringOr(object, "text", line.text, {}, "Dialogue", &detail) || line.text.empty() || line.text.size() > kMaxTextBytes)
                return fail(error, detail.empty() ? "line text is missing or too long" : detail);
            if (!jsonBoolOr(object, "once", line.once, false, "Dialogue", &detail))
                return fail(error, detail);
            if (!jsonStringOr(object, "clip", line.clip, {}, "Dialogue", &detail))
                return fail(error, detail);
            return true;
        }

        bool readBank(const json& list, std::vector<DialogueLine>& out, std::unordered_set<std::string>& ids, std::string* error)
        {
            if (!list.is_array())
                return fail(error, "line bank is not a list");
            if (list.size() > 12)
                return fail(error, "line bank is longer than 12");
            for (const json& item : list)
            {
                DialogueLine line;
                if (!readLine(item, line, ids, error))
                    return false;
                out.push_back(std::move(line));
            }
            return true;
        }

        bool readCondition(const json& object, DialogueCondition& cond, std::string* error)
        {
            if (!object.is_object())
                return fail(error, "condition is not an object");
            std::string detail;
            if (!jsonStringOr(object, "op", cond.op, {}, "Dialogue", &detail) || cond.op.empty())
                return fail(error, detail.empty() ? "condition op is missing" : detail);
            if (!jsonStringOr(object, "key", cond.key, {}, "Dialogue", &detail))
                return fail(error, detail);
            if (!jsonStringOr(object, "cmp", cond.cmp, {}, "Dialogue", &detail))
                return fail(error, detail);
            if (!jsonStringOr(object, "id", cond.key, cond.key, "Dialogue", &detail))
                return fail(error, detail);
            if (!jsonStringOr(object, "name", cond.name, {}, "Dialogue", &detail))
                return fail(error, detail);
            if (!jsonStringOr(object, "who", cond.who, {}, "Dialogue", &detail))
                return fail(error, detail);
            if (cond.key.size() > kMaxIdBytes)
                return fail(error, "condition key is too long");
            if (object.contains("value"))
            {
                if (object["value"].is_string())
                {
                    if (!jsonStringOr(object, "value", cond.name, {}, "Dialogue", &detail))
                        return fail(error, detail);
                    cond.hasValue = true;
                }
                else
                {
                    if (!jsonIntOr(object, "value", cond.value, 0, "Dialogue", &detail))
                        return fail(error, detail);
                    cond.hasValue = true;
                }
            }
            if (object.contains("alive"))
            {
                if (!jsonBoolOr(object, "alive", cond.alive, true, "Dialogue", &detail))
                    return fail(error, detail);
                cond.hasAlive = true;
            }
            if (object.contains("done"))
            {
                if (!jsonBoolOr(object, "done", cond.done, false, "Dialogue", &detail))
                    return fail(error, detail);
                cond.hasDone = true;
            }
            return true;
        }

        bool readEffect(const json& object, DialogueEffect& effect, std::string* error)
        {
            if (!object.is_object())
                return fail(error, "effect is not an object");
            std::string detail;
            if (!jsonStringOr(object, "op", effect.op, {}, "Dialogue", &detail) || effect.op.empty())
                return fail(error, detail.empty() ? "effect op is missing" : detail);
            if (!jsonStringOr(object, "key", effect.key, {}, "Dialogue", &detail))
                return fail(error, detail);
            if (!jsonStringOr(object, "name", effect.name, {}, "Dialogue", &detail))
                return fail(error, detail);
            if (!jsonStringOr(object, "id", effect.name, effect.name, "Dialogue", &detail))
                return fail(error, detail);
            if (effect.key.size() > kMaxIdBytes || effect.name.size() > kMaxIdBytes)
                return fail(error, "effect key is too long");
            if (object.contains("value"))
            {
                if (!jsonIntOr(object, "value", effect.value, 0, "Dialogue", &detail))
                    return fail(error, detail);
            }
            if (object.contains("count"))
            {
                if (!jsonIntOr(object, "count", effect.count, 1, "Dialogue", &detail))
                    return fail(error, detail);
                effect.hasCount = true;
            }
            return true;
        }

        json lineJson(const DialogueLine& line)
        {
            json object = json::object();
            object["id"]   = line.id;
            object["text"] = line.text;
            object["once"] = line.once;
            if (!line.clip.empty())
                object["clip"] = line.clip;
            return object;
        }

        json conditionJson(const DialogueCondition& cond)
        {
            json object = json::object();
            object["op"] = cond.op;
            if (!cond.key.empty())
                object["key"] = cond.key;
            if (!cond.cmp.empty())
                object["cmp"] = cond.cmp;
            if (!cond.name.empty())
                object["name"] = cond.name;
            if (!cond.who.empty())
                object["who"] = cond.who;
            if (cond.hasValue && cond.name.empty())
                object["value"] = cond.value;
            else if (cond.hasValue)
                object["value"] = cond.name;
            if (cond.hasAlive)
                object["alive"] = cond.alive;
            if (cond.hasDone)
                object["done"] = cond.done;
            return object;
        }

        json effectJson(const DialogueEffect& effect)
        {
            json object = json::object();
            object["op"] = effect.op;
            if (!effect.key.empty())
                object["key"] = effect.key;
            if (!effect.name.empty())
                object["name"] = effect.name;
            if (effect.op == "setFact" || effect.op == "addFact")
                object["value"] = effect.value;
            if (effect.hasCount || effect.op == "questEvent" || effect.op == "giveItem" || effect.op == "takeItem")
                object["count"] = effect.count;
            return object;
        }
    } // namespace

    bool parseDialogueGraph(std::string_view jsonText, DialogueGraphDef& out, std::string* error)
    {
        if (jsonText.size() > kMaxGraphBytes)
            return fail(error, "graph is larger than 256 KiB");
        const json root = json::parse(jsonText, nullptr, false);
        if (root.is_discarded() || !root.is_object())
            return fail(error, "not a JSON object");
        std::string detail;
        int         version = 0;
        if (!root.contains("version") || !jsonIntOr(root, "version", version, 0, "Dialogue", &detail) || version != 1)
            return fail(error, detail.empty() ? "version must be 1" : detail);

        DialogueGraphDef                graph;
        std::unordered_set<std::string> ids;
        if (!takeId(root, "id", graph.id, ids, error))
            return false;
        ids.erase(graph.id);
        if (!jsonBoolOr(root, "pauseWorld", graph.pauseWorld, false, "Dialogue", &detail))
            return fail(error, detail);
        if (!jsonFloatOr(root, "talkRange", graph.talkRange, 2.5f, "Dialogue", &detail))
            return fail(error, detail);
        if (graph.talkRange < 0.5f)
            graph.talkRange = 0.5f;
        if (graph.talkRange > 8.0f)
            graph.talkRange = 8.0f;
        if (!jsonFloatOr(root, "leash", graph.leash, 4.0f, "Dialogue", &detail))
            return fail(error, detail);
        if (!(graph.leash > graph.talkRange))
            return fail(error, "leash must be greater than talkRange");
        if (!jsonStringOr(root, "speaker", graph.speaker, "NPC", "Dialogue", &detail))
            return fail(error, detail);
        if (graph.speaker.empty())
            graph.speaker = "NPC";
        if (!jsonStringOr(root, "entry", graph.entry, {}, "Dialogue", &detail) || graph.entry.empty())
            return fail(error, detail.empty() ? "entry is missing" : detail);

        if (root.contains("refusal"))
        {
            if (!readBank(root["refusal"], graph.refusal, ids, error))
                return false;
        }
        if (root.contains("resume"))
        {
            if (!readBank(root["resume"], graph.resume, ids, error))
                return false;
        }
        if (!root.contains("nodes") || !root["nodes"].is_array())
            return fail(error, "nodes must be a list");
        if (root["nodes"].size() > 128)
            return fail(error, "more than 128 nodes");
        for (const json& nodeJson : root["nodes"])
        {
            if (!nodeJson.is_object())
                return fail(error, "node is not an object");
            DialogueNode node;
            if (!takeId(nodeJson, "id", node.id, ids, error))
                return false;
            if (!jsonFloatOr(nodeJson, "x", node.x, 0.0f, "Dialogue", &detail))
                return fail(error, detail);
            if (!jsonFloatOr(nodeJson, "y", node.y, 0.0f, "Dialogue", &detail))
                return fail(error, detail);
            if (!nodeJson.contains("lines") || !readBank(nodeJson["lines"], node.lines, ids, error))
                return fail(error, error && !error->empty() ? *error : "node needs a line bank");
            if (node.lines.empty())
                return fail(error, "node \"" + node.id + "\" has no lines");
            if (nodeJson.contains("choices"))
            {
                const json& choices = nodeJson["choices"];
                if (!choices.is_array() || choices.size() > 8)
                    return fail(error, "choices must be a list of at most 8");
                for (const json& choiceJson : choices)
                {
                    if (!choiceJson.is_object())
                        return fail(error, "choice is not an object");
                    DialogueChoice choice;
                    if (!takeId(choiceJson, "id", choice.id, ids, error))
                        return false;
                    if (!jsonStringOr(choiceJson, "text", choice.text, {}, "Dialogue", &detail) || choice.text.empty() || choice.text.size() > kMaxTextBytes)
                        return fail(error, detail.empty() ? "choice text is missing or too long" : detail);
                    if (!jsonStringOr(choiceJson, "next", choice.next, {}, "Dialogue", &detail))
                        return fail(error, detail);
                    if (!jsonBoolOr(choiceJson, "once", choice.once, false, "Dialogue", &detail))
                        return fail(error, detail);
                    if (choiceJson.contains("conditions"))
                    {
                        const json& list = choiceJson["conditions"];
                        if (!list.is_array() || list.size() > 8)
                            return fail(error, "conditions must be a list of at most 8");
                        for (const json& item : list)
                        {
                            DialogueCondition cond;
                            if (!readCondition(item, cond, error))
                                return false;
                            choice.conditions.push_back(std::move(cond));
                        }
                    }
                    if (choiceJson.contains("effects"))
                    {
                        const json& list = choiceJson["effects"];
                        if (!list.is_array() || list.size() > 8)
                            return fail(error, "effects must be a list of at most 8");
                        for (const json& item : list)
                        {
                            DialogueEffect effect;
                            if (!readEffect(item, effect, error))
                                return false;
                            choice.effects.push_back(std::move(effect));
                        }
                    }
                    node.choices.push_back(std::move(choice));
                }
            }
            graph.nodes.push_back(std::move(node));
        }
        if (!graph.findNode(graph.entry))
            return fail(error, "entry node is missing");
        out = std::move(graph);
        return true;
    }

    bool writeDialogueGraphJson(const DialogueGraphDef& graph, std::string& out)
    {
        json root = json::object();
        root["version"]    = 1;
        root["id"]         = graph.id;
        root["pauseWorld"] = graph.pauseWorld;
        root["talkRange"]  = graph.talkRange;
        root["leash"]      = graph.leash;
        root["speaker"]    = graph.speaker;
        root["entry"]      = graph.entry;
        root["refusal"]    = json::array();
        root["resume"]     = json::array();
        for (const DialogueLine& line : graph.refusal)
            root["refusal"].push_back(lineJson(line));
        for (const DialogueLine& line : graph.resume)
            root["resume"].push_back(lineJson(line));
        root["nodes"] = json::array();
        for (const DialogueNode& node : graph.nodes)
        {
            json nodeJson = json::object();
            nodeJson["id"]    = node.id;
            nodeJson["x"]     = node.x;
            nodeJson["y"]     = node.y;
            nodeJson["lines"] = json::array();
            nodeJson["choices"] = json::array();
            for (const DialogueLine& line : node.lines)
                nodeJson["lines"].push_back(lineJson(line));
            for (const DialogueChoice& choice : node.choices)
            {
                json choiceJson = json::object();
                choiceJson["id"]   = choice.id;
                choiceJson["text"] = choice.text;
                choiceJson["next"] = choice.next;
                choiceJson["once"] = choice.once;
                choiceJson["conditions"] = json::array();
                choiceJson["effects"]    = json::array();
                for (const DialogueCondition& cond : choice.conditions)
                    choiceJson["conditions"].push_back(conditionJson(cond));
                for (const DialogueEffect& effect : choice.effects)
                    choiceJson["effects"].push_back(effectJson(effect));
                nodeJson["choices"].push_back(std::move(choiceJson));
            }
            root["nodes"].push_back(std::move(nodeJson));
        }
        out = root.dump(2);
        return true;
    }

    bool saveDialogueGraphJsonFile(const DialogueGraphDef& graph, const std::filesystem::path& path)
    {
        std::string text;
        if (path.empty() || !writeDialogueGraphJson(graph, text))
            return false;
        const std::filesystem::path temp = path.native() + L".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                DE_LOG_ERROR(LogCategory::AI, "Dialogue: cannot write {}", temp.string());
                return false;
            }
            out << text;
            if (!out.good())
            {
                std::error_code ec;
                std::filesystem::remove(temp, ec);
                DE_LOG_ERROR(LogCategory::AI, "Dialogue: write failed {}", temp.string());
                return false;
            }
        }
        std::error_code ec;
        std::filesystem::rename(temp, path, ec);
        if (!ec)
            return true;
        const std::filesystem::path backup = path.native() + L".bak";
        std::filesystem::rename(path, backup, ec);
        std::filesystem::rename(temp, path, ec);
        if (ec)
        {
            std::error_code back;
            std::filesystem::rename(backup, path, back);
            std::filesystem::remove(temp, back);
            DE_LOG_ERROR(LogCategory::AI, "Dialogue: cannot replace {}", path.string());
            return false;
        }
        std::filesystem::remove(backup, ec);
        return true;
    }

} // namespace Dark
