#pragma once

#include "ECS/Entity.h"
#include "Gameplay/Quest.h"

namespace Dark
{

    class World;

    // Owns the quest tracker. PlaySession takes notes, grants rewards, and forwards each note for banners.
    class QuestRuntime
    {
    public:
        QuestTracker&       tracker() { return m_tracker; }
        const QuestTracker& tracker() const { return m_tracker; }

        void                      start(QuestDef def) { m_tracker.start(std::move(def)); }
        void                      clear() { m_tracker.clear(); }
        bool                      active() const { return m_tracker.active(); }
        bool                      complete() const { return m_tracker.complete(); }
        const QuestDef&           def() const { return m_tracker.def(); }
        const ObjectiveState&     state(size_t i) const { return m_tracker.state(i); }
        bool                      open(size_t i) const { return m_tracker.open(i); }
        int                       focus() const { return m_tracker.focus(); }
        void                      event(std::string_view name, int count = 1) { m_tracker.event(name, count); }
        std::vector<QuestNoteEvent> takeNotes() { return m_tracker.takeNotes(); }

        // Zone enter calls reach. Collect objectives follow the inventory count.
        void tick(World& world, Entity player);

    private:
        QuestTracker m_tracker;
    };

} // namespace Dark
