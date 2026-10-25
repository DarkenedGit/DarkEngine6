#include "Gameplay/Quest.h"

#include "Core/Json.h"
#include "third_party/nlohmann/json.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace Dark
{
    namespace
    {
        using json = nlohmann::json;

        constexpr uintmax_t kMaxQuestBytes = 256 * 1024;

        bool fail(std::string* error, std::string message)
        {
            if (error)
                *error = std::move(message);
            return false;
        }

        bool parseType(const std::string& name, ObjectiveType& out)
        {
            if (name == "reach")
                out = ObjectiveType::Reach;
            else if (name == "collect")
                out = ObjectiveType::Collect;
            else if (name == "event")
                out = ObjectiveType::Event;
            else
                return false;
            return true;
        }

    } // namespace

    std::string questVirtualPath(std::string_view id)
    {
        return "quests/" + std::string(id) + ".quest.json";
    }

    bool parseQuest(std::string_view jsonText, QuestDef& out, std::string* error)
    {
        const json root = json::parse(jsonText, nullptr, false);
        if (root.is_discarded() || !root.is_object())
            return fail(error, "not a JSON object");
        QuestDef def;
        if (!jsonStringOr(root, "id", def.id, std::string(), "Quest", error))
            return false;
        if (!jsonStringOr(root, "title", def.title, def.id, "Quest", error))
            return false;
        if (!jsonStringOr(root, "description", def.description, std::string(), "Quest", error))
            return false;
        if (!jsonBoolOr(root, "sequential", def.sequential, true, "Quest", error))
            return false;
        if (def.id.empty())
            return fail(error, "missing \"id\"");
        if (!root.contains("objectives") || !root["objectives"].is_array() || root["objectives"].empty())
            return fail(error, "needs a non-empty \"objectives\" array");
        for (const json& jo : root["objectives"])
        {
            if (!jo.is_object())
                return fail(error, "objective is not an object");
            ObjectiveDef o;
            if (!jsonStringOr(jo, "id", o.id, std::string(), "Quest", error))
                return false;
            if (!jsonStringOr(jo, "text", o.text, o.id, "Quest", error))
                return false;
            if (!jsonIntOr(jo, "count", o.count, 1, "Quest", error))
                return false;
            if (!jsonBoolOr(jo, "optional", o.optional, false, "Quest", error))
                return false;
            std::string typeName;
            if (!jsonStringOr(jo, "type", typeName, std::string(), "Quest", error))
                return false;
            if (!parseType(typeName, o.type))
                return fail(error, "objective '" + o.id + "' needs \"type\": reach, collect or event");
            const char* targetKey = o.type == ObjectiveType::Collect ? "item" : (o.type == ObjectiveType::Reach ? "zone" : "event");
            if (!jsonStringOr(jo, targetKey, o.target, std::string(), "Quest", error))
                return false;
            if (o.target.empty())
                return fail(error, "objective '" + o.id + "' has no target");
            if (o.count < 1)
                o.count = 1;
            def.objectives.push_back(std::move(o));
        }
        if (root.contains("rewards") && root["rewards"].is_array())
        {
            for (const json& jr : root["rewards"])
            {
                if (!jr.is_object())
                    continue;
                QuestReward r;
                if (!jsonStringOr(jr, "item", r.item, std::string(), "Quest", error))
                    return false;
                if (!jsonIntOr(jr, "count", r.count, 1, "Quest", error))
                    return false;
                if (!r.item.empty() && r.count > 0)
                    def.rewards.push_back(std::move(r));
            }
        }
        out = std::move(def);
        return true;
    }

    bool loadQuestFile(const std::filesystem::path& path, QuestDef& out, std::string* error)
    {
        std::error_code ec;
        if (path.empty() || !std::filesystem::is_regular_file(path, ec) || ec)
            return fail(error, "file not found: " + path.string());
        const uintmax_t size = std::filesystem::file_size(path, ec);
        if (ec || size > kMaxQuestBytes)
            return fail(error, "unreadable or too large: " + path.string());
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return fail(error, "cannot open: " + path.string());
        std::ostringstream ss;
        ss << in.rdbuf();
        return parseQuest(ss.str(), out, error);
    }

    void QuestTracker::start(QuestDef def)
    {
        m_def      = std::move(def);
        m_state.assign(m_def.objectives.size(), ObjectiveState{});
        m_announced.assign(m_def.objectives.size(), false);
        m_notes.clear();
        m_active   = true;
        m_complete = false;
        m_notes.push_back(QuestNoteEvent{ QuestNote::Started, m_def.title });
        settle();
    }

    void QuestTracker::clear()
    {
        m_def      = QuestDef{};
        m_state.clear();
        m_announced.clear();
        m_notes.clear();
        m_active   = false;
        m_complete = false;
    }

    bool QuestTracker::open(size_t i) const
    {
        if (!m_active || i >= m_state.size() || m_state[i].done)
            return false;
        if (!m_def.sequential || m_def.objectives[i].optional)
            return true;
        for (size_t k = 0; k < i; ++k)
        {
            if (!m_def.objectives[k].optional && !m_state[k].done)
                return false;
        }
        return true;
    }

    int QuestTracker::focus() const
    {
        for (size_t i = 0; i < m_state.size(); ++i)
        {
            if (open(i) && !m_def.objectives[i].optional)
                return static_cast<int>(i);
        }
        for (size_t i = 0; i < m_state.size(); ++i)
        {
            if (open(i))
                return static_cast<int>(i);
        }
        return -1;
    }

    void QuestTracker::collect(std::string_view item, int count)
    {
        advance(ObjectiveType::Collect, item, count);
    }

    void QuestTracker::reach(std::string_view zone)
    {
        advance(ObjectiveType::Reach, zone, 1);
    }

    void QuestTracker::event(std::string_view name, int count)
    {
        advance(ObjectiveType::Event, name, count);
    }

    void QuestTracker::advance(ObjectiveType type, std::string_view target, int count)
    {
        if (!m_active || m_complete || count <= 0)
            return;
        for (size_t i = 0; i < m_state.size(); ++i)
        {
            const ObjectiveDef& o = m_def.objectives[i];
            if (m_state[i].done || o.type != type || o.target != target)
                continue;
            if (type == ObjectiveType::Reach && !open(i))
                continue;
            m_state[i].progress = std::min(o.count, m_state[i].progress + count);
        }
        settle();
    }

    void QuestTracker::settle()
    {
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (size_t i = 0; i < m_state.size(); ++i)
            {
                if (!open(i))
                    continue;
                if (!m_announced[i])
                {
                    m_announced[i] = true;
                    m_notes.push_back(QuestNoteEvent{ QuestNote::NewObjective, m_def.objectives[i].text });
                }
                if (m_state[i].progress >= m_def.objectives[i].count)
                {
                    m_state[i].done = true;
                    m_notes.push_back(QuestNoteEvent{ QuestNote::ObjectiveDone, m_def.objectives[i].text });
                    changed = true;
                }
            }
        }
        if (m_complete)
            return;
        for (size_t i = 0; i < m_state.size(); ++i)
        {
            if (!m_def.objectives[i].optional && !m_state[i].done)
                return;
        }
        m_complete = true;
        m_notes.push_back(QuestNoteEvent{ QuestNote::QuestDone, m_def.title });
    }

    std::vector<QuestNoteEvent> QuestTracker::takeNotes()
    {
        std::vector<QuestNoteEvent> out;
        out.swap(m_notes);
        return out;
    }

} // namespace Dark
