#pragma once

#include "ECS/Entity.h"
#include "Play/DialogueGraph.h"
#include "Play/DialogueMemory.h"

#include <string>
#include <vector>

namespace Dark
{

    class QuestRuntime;
    class World;
    struct DialogueFactsComponent;

    enum class DialogueInterrupt : uint8_t
    {
        Distance,
        Alert,
        Combat,
        Death,
        Menu,
        Pause,
        Cancel,
        Ui
    };

    enum class DialoguePhase : uint8_t
    {
        Idle,
        Resume,
        NodeLine,
        Choice,
        Refusal
    };

    // Null acquire shows text on the next tick for 2.5 seconds. stop may be null.
    struct DialogueLineAudio
    {
        void* user = nullptr;
        bool (*acquire)(void* user, const DialogueLine& line, Entity speaker, float waited, float& shown) = nullptr;
        void (*stop)(void* user)                                                                           = nullptr;
    };

    // One open conversation. The graph pointer must outlive the session.
    class DialogueRuntime
    {
    public:
        void setGraph(const DialogueGraphDef* graph);
        void setAudio(DialogueLineAudio audio);
        void setFacts(DialogueFactsComponent* facts);
        void setUiBlocked(bool blocked);

        bool tryStart(World& world, Entity player, Entity npc, QuestRuntime* quest);
        void interrupt(DialogueInterrupt reason);
        void requestSkip();
        void moveChoice(int delta);
        void commitChoice();
        void tick(World& world, float dt);

        bool                       locksWorld() const;
        bool                       choiceOpen() const;
        bool                       active() const;
        DialoguePhase              phase() const { return m_phase; }
        const std::string&         subtitle() const { return m_subtitle; }
        const std::string&         speakerName() const { return m_speakerName; }
        const std::vector<int>&    choiceIndices() const { return m_choiceIndex; }
        int                        selected() const { return m_selected; }
        Entity                     speaker() const { return m_npc; }
        const std::string&         nodeId() const { return m_nodeId; }
        const DialogueGraphDef*    graph() const { return m_graph; }

    private:
        void beginNodeLine(World& world);
        void beginChoice(World& world);
        void beginResume(World& world);
        void beginRefusal(World& world);
        void endConversation(World& world);
        void writeBookmark(World& world, DialoguePending pending);
        void advance(World& world);
        void commitLine(World& world, float dt);
        bool blockedByWorld(World& world, DialogueInterrupt& reason) const;

        const DialogueGraphDef* m_graph = nullptr;
        World*                   m_world = nullptr;
        DialogueFactsComponent* m_facts = nullptr;
        QuestRuntime*           m_quest = nullptr;
        DialogueLineAudio       m_audio{};
        DialoguePhase           m_phase = DialoguePhase::Idle;
        Entity                  m_player{};
        Entity                  m_npc{};
        std::string             m_nodeId;
        DialogueLine            m_line;
        bool                    m_hasLine     = false;
        bool                    m_committed   = false;
        bool                    m_pauseWorld  = false;
        bool                    m_uiBlocked   = false;
        bool                    m_skip        = false;
        bool                    m_commit      = false;
        int                     m_move        = 0;
        int                     m_selected    = 0;
        float                   m_waited      = 0.0f;
        float                   m_shownLeft   = 0.0f;
        std::string             m_subtitle;
        std::string             m_speakerName;
        std::vector<int>        m_choiceIndex;
    };

} // namespace Dark
