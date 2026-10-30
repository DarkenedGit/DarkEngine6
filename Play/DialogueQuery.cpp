#include "Play/DialogueQuery.h"

#include "AI/AiComponents.h"
#include "AI/Awareness.h"
#include "Character/HealthComponent.h"
#include "Core/Log.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/Item.h"
#include "Gameplay/QuestRuntime.h"
#include "Play/DialogueFacts.h"

#include <cmath>

namespace Dark
{
    namespace
    {
        bool compareInt(const DialogueGraphDef& graph, const DialogueCondition& condition, int left)
        {
            const int right = condition.value;
            if (condition.cmp == "==")
                return left == right;
            if (condition.cmp == "!=")
                return left != right;
            if (condition.cmp == "<")
                return left < right;
            if (condition.cmp == "<=")
                return left <= right;
            if (condition.cmp == ">")
                return left > right;
            if (condition.cmp == ">=")
                return left >= right;
            graph.noteUnknown(condition.cmp.empty() ? condition.op + " missing cmp" : condition.op + " cmp " + condition.cmp);
            return false;
        }

        bool parseAlertness(const std::string& name, AI::Alertness& out)
        {
            if (name == "Calm")
                out = AI::Alertness::Calm;
            else if (name == "Suspicious")
                out = AI::Alertness::Suspicious;
            else if (name == "Investigating")
                out = AI::Alertness::Investigating;
            else if (name == "Alert")
                out = AI::Alertness::Alert;
            else if (name == "Searching")
                out = AI::Alertness::Searching;
            else
                return false;
            return true;
        }

        const HealthComponent* healthFor(const DialogueCondition& condition, const DialogueQueryView& view, bool& whoOk)
        {
            whoOk = condition.who == "player" || condition.who == "npc";
            if (!whoOk)
                return nullptr;
            return condition.who == "player" ? view.playerHealth : view.npcHealth;
        }
    } // namespace

    bool dialogueConditionPasses(const DialogueGraphDef& graph, const DialogueCondition& condition, const DialogueQueryView& view)
    {
        if (condition.op == "fact")
        {
            const int value = view.facts ? view.facts->get(condition.key) : 0;
            return compareInt(graph, condition, value);
        }
        if (condition.op == "questActive")
        {
            if (!view.quest || !view.quest->active())
                return false;
            return condition.key.empty() || view.quest->def().id == condition.key;
        }
        if (condition.op == "questComplete")
        {
            if (!view.quest || !view.quest->complete())
                return false;
            return condition.key.empty() || view.quest->def().id == condition.key;
        }
        if (condition.op == "objective")
        {
            if (!view.quest || !view.quest->active())
                return false;
            const QuestDef& def = view.quest->def();
            for (size_t i = 0; i < def.objectives.size(); ++i)
            {
                if (def.objectives[i].id != condition.key)
                    continue;
                const bool done = view.quest->state(i).done;
                return condition.hasDone ? done == condition.done : done;
            }
            return false;
        }
        if (condition.op == "item")
        {
            const int count = view.inventory ? countBagItem(*view.inventory, condition.key) : 0;
            return compareInt(graph, condition, count);
        }
        if (condition.op == "alertness")
        {
            AI::Alertness want = AI::Alertness::Calm;
            if (!parseAlertness(condition.name, want))
            {
                graph.noteUnknown(condition.name.empty() ? "alertness" : "alertness " + condition.name);
                return false;
            }
            const AI::Alertness now = view.npc ? view.npc->aware.alertness : AI::Alertness::Calm;
            return now == want;
        }
        if (condition.op == "health")
        {
            bool whoOk = false;
            const HealthComponent* hp = healthFor(condition, view, whoOk);
            if (!whoOk)
            {
                graph.noteUnknown("health who");
                return false;
            }
            if (!condition.hasAlive && condition.cmp.empty())
            {
                graph.noteUnknown("health");
                return false;
            }
            if (condition.hasAlive)
            {
                const bool alive = hp && hp->health.alive();
                if (alive != condition.alive)
                    return false;
            }
            if (!condition.cmp.empty())
            {
                const int value = hp ? static_cast<int>(std::lround(static_cast<double>(hp->health.hp()))) : 0;
                if (!compareInt(graph, condition, value))
                    return false;
            }
            return true;
        }
        graph.noteUnknown(condition.op.empty() ? "condition" : condition.op);
        return false;
    }

    void applyDialogueEffects(const DialogueGraphDef& graph, const std::vector<DialogueEffect>& effects, DialogueFactsComponent* facts,
                              InventoryComponent* inventory, QuestRuntime* quest)
    {
        for (const DialogueEffect& effect : effects)
        {
            if (effect.op == "setFact" || effect.op == "addFact")
            {
                if (!facts)
                {
                    DE_LOG_WARN(LogCategory::AI, "Dialogue: {} \"{}\" has no fact store", effect.op, effect.key);
                    continue;
                }
                if (effect.op == "setFact")
                    facts->set(effect.key, effect.value);
                else
                    facts->add(effect.key, effect.value);
                continue;
            }
            if (effect.op == "questEvent")
            {
                if (!quest || !quest->active() || quest->complete())
                {
                    DE_LOG_INFO(LogCategory::AI, "Dialogue: quest event \"{}\" ignored", effect.name);
                    continue;
                }
                quest->event(effect.name, effect.count > 0 ? effect.count : 1);
                continue;
            }
            if (effect.op == "giveItem" || effect.op == "takeItem")
            {
                if (effect.count <= 0)
                {
                    DE_LOG_WARN(LogCategory::AI, "Dialogue: {} \"{}\" count {} changes nothing", effect.op, effect.name, effect.count);
                    continue;
                }
                if (!inventory)
                {
                    DE_LOG_WARN(LogCategory::AI, "Dialogue: {} \"{}\" has no inventory", effect.op, effect.name);
                    continue;
                }
                if (effect.op == "giveItem")
                {
                    const ItemDef* def = findItemDef(effect.name);
                    if (!def)
                    {
                        DE_LOG_ERROR(LogCategory::AI, "Dialogue: unknown item \"{}\"", effect.name);
                        continue;
                    }
                    const int added = addItem(*inventory, *def, effect.count);
                    if (added < effect.count)
                        DE_LOG_WARN(LogCategory::AI, "Dialogue: giveItem \"{}\" added {} of {}", effect.name, added, effect.count);
                    continue;
                }
                if (!tryRemoveItem(*inventory, effect.name, effect.count))
                    DE_LOG_WARN(LogCategory::AI, "Dialogue: takeItem \"{}\" x{} was not in the bag", effect.name, effect.count);
                continue;
            }
            graph.noteUnknown(effect.op.empty() ? "effect" : effect.op);
        }
    }

} // namespace Dark
