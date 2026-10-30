#pragma once

#include "ECS/Entity.h"
#include "Gameplay/Quest.h"
#include "Math/Vector3f.h"

namespace Dark
{

    class World;

    // Owns the quest tracker. Loads a quest file, grants its rewards once, and finds the focused objective in the world.
    // PlaySession forwards each note for banners and plays the reward click.
    class QuestRuntime
    {
    public:
        QuestTracker&       tracker() { return m_tracker; }
        const QuestTracker& tracker() const { return m_tracker; }

        // content/quests/<id>.quest.json. An empty id or a missing file leaves the current quest in place.
        bool                      start(std::string_view id);
        void                      start(QuestDef def);
        void                      clear();
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

        // Reach is the zone center. Collect is the nearest matching pickup. "power_off" is the nearest breaker that is still on.
        bool objectiveTarget(World& world, Entity player, Math::Vector3f& out) const;

        // Adds def().rewards into the player inventory once the quest is complete. Returns how many reward lines were added.
        // A full bag leaves the rewards ungranted so a later call can try again.
        int grantRewards(World& world, Entity player);

        bool rewardsGranted() const { return m_rewardsGranted; }

        // Reloads content/quests/<id>.quest.json and applies saved objective rows. No start banners.
        // An empty id clears the quest. A missing file leaves the current quest in place.
        bool importProgress(std::string_view id, bool rewardsGranted, const std::vector<ObjectiveState>& states);

    private:
        QuestTracker m_tracker;
        bool         m_rewardsGranted = false;
        bool         m_rewardBlocked  = false;
    };

    struct QuestProgressComponent;

    void writeQuestProgress(const QuestRuntime& quest, QuestProgressComponent& out);
    bool readQuestProgress(const QuestProgressComponent& in, QuestRuntime& quest);

} // namespace Dark
