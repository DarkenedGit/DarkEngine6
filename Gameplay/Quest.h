#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Dark
{

    enum class ObjectiveType : uint8_t
    {
        Reach,
        Collect,
        Event,
    };

    struct ObjectiveDef
    {
        std::string   id;
        std::string   text;
        ObjectiveType type = ObjectiveType::Event;
        std::string   target;
        int           count    = 1;
        bool          optional = false;
    };

    struct QuestReward
    {
        std::string item;
        int         count = 1;
    };

    struct QuestDef
    {
        std::string               id;
        std::string               title;
        std::string               description;
        bool                      sequential = true;
        std::vector<ObjectiveDef> objectives;
        std::vector<QuestReward>  rewards;
    };

    // Quest JSON: content/quests/<id>.quest.json. Returns false and leaves `out` unchanged on bad input.
    bool parseQuest(std::string_view jsonText, QuestDef& out, std::string* error = nullptr);
    bool loadQuestFile(const std::filesystem::path& path, QuestDef& out, std::string* error = nullptr);
    std::string questVirtualPath(std::string_view id);

    enum class QuestNote : uint8_t
    {
        Started,
        NewObjective,
        ObjectiveDone,
        QuestDone,
    };

    struct QuestNoteEvent
    {
        QuestNote   kind = QuestNote::Started;
        std::string text;
    };

    struct ObjectiveState
    {
        int  progress = 0;
        bool done     = false;
    };

    class QuestTracker
    {
    public:
        void start(QuestDef def);
        void clear();
        // Puts a saved quest back without the start and objective banners.
        void restore(QuestDef def, const std::vector<ObjectiveState>& states);

        bool                  active() const { return m_active; }
        bool                  complete() const { return m_complete; }
        const QuestDef&       def() const { return m_def; }
        const ObjectiveState& state(size_t i) const { return m_state[i]; }
        bool                  open(size_t i) const;
        int                   focus() const;

        void collect(std::string_view item, int count);
        // Absolute progress for open, not-done Collect objectives. A finished objective stays finished.
        void syncCollect(std::string_view item, int held);
        void reach(std::string_view zone);
        void event(std::string_view name, int count = 1);

        std::vector<QuestNoteEvent> takeNotes();

    private:
        void advance(ObjectiveType type, std::string_view target, int count);
        void settle();

        QuestDef                    m_def;
        std::vector<ObjectiveState> m_state;
        std::vector<bool>           m_announced;
        std::vector<QuestNoteEvent> m_notes;
        bool                        m_active   = false;
        bool                        m_complete = false;
    };

} // namespace Dark
