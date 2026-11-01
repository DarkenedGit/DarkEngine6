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
        bool compareInt(DialogueCmp cmp, int left, int right)
        {
            switch (cmp)
            {
            case DialogueCmp::Equal:        return left == right;
            case DialogueCmp::NotEqual:     return left != right;
            case DialogueCmp::Less:         return left < right;
            case DialogueCmp::LessEqual:    return left <= right;
            case DialogueCmp::Greater:      return left > right;
            case DialogueCmp::GreaterEqual: return left >= right;
            case DialogueCmp::None:         return false;
            }
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
        switch (condition.op)
        {
        case DialogueConditionOp::Fact:
        {
            const int value = view.facts ? view.facts->get(condition.key) : 0;
            return compareInt(condition.cmp, value, condition.value);
        }
        case DialogueConditionOp::QuestActive:
            if (!view.quest || !view.quest->active())
                return false;
            return condition.key.empty() || view.quest->def().id == condition.key;
        case DialogueConditionOp::QuestComplete:
            if (!view.quest || !view.quest->complete())
                return false;
            return condition.key.empty() || view.quest->def().id == condition.key;
        case DialogueConditionOp::Objective:
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
        case DialogueConditionOp::Item:
        {
            const int count = view.inventory ? countBagItem(*view.inventory, condition.key) : 0;
            return compareInt(condition.cmp, count, condition.value);
        }
        case DialogueConditionOp::Alertness:
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
        case DialogueConditionOp::Health:
        {
            bool whoOk = false;
            const HealthComponent* hp = healthFor(condition, view, whoOk);
            if (!whoOk)
            {
                graph.noteUnknown("health who");
                return false;
            }
            if (!condition.hasAlive && condition.cmp == DialogueCmp::None)
                return false;
            if (condition.hasAlive)
            {
                const bool alive = hp && hp->health.alive();
                if (alive != condition.alive)
                    return false;
            }
            if (condition.cmp != DialogueCmp::None)
            {
                const int value = hp ? static_cast<int>(std::lround(static_cast<double>(hp->health.hp()))) : 0;
                if (!compareInt(condition.cmp, value, condition.value))
                    return false;
            }
            return true;
        }
        }
        return false;
    }

    void applyDialogueEffects(const DialogueGraphDef& graph, const std::vector<DialogueEffect>& effects, DialogueFactsComponent* facts,
                              InventoryComponent* inventory, QuestRuntime* quest)
    {
        (void)graph;
        for (const DialogueEffect& effect : effects)
        {
            switch (effect.op)
            {
            case DialogueEffectOp::SetFact:
            case DialogueEffectOp::AddFact:
                if (!facts)
                {
                    DE_LOG_WARN(LogCategory::AI, "Dialogue: {} \"{}\" has no fact store", dialogueEffectOpName(effect.op), effect.key);
                    break;
                }
                if (effect.op == DialogueEffectOp::SetFact)
                    facts->set(effect.key, effect.value);
                else
                    facts->add(effect.key, effect.value);
                break;
            case DialogueEffectOp::QuestEvent:
                if (!quest || !quest->active() || quest->complete())
                {
                    DE_LOG_INFO(LogCategory::AI, "Dialogue: quest event \"{}\" ignored", effect.name);
                    break;
                }
                quest->event(effect.name, effect.count > 0 ? effect.count : 1);
                break;
            case DialogueEffectOp::GiveItem:
            case DialogueEffectOp::TakeItem:
                if (effect.count <= 0)
                {
                    DE_LOG_WARN(LogCategory::AI, "Dialogue: {} \"{}\" count {} changes nothing", dialogueEffectOpName(effect.op), effect.key, effect.count);
                    break;
                }
                if (!inventory)
                {
                    DE_LOG_WARN(LogCategory::AI, "Dialogue: {} \"{}\" has no inventory", dialogueEffectOpName(effect.op), effect.key);
                    break;
                }
                if (effect.op == DialogueEffectOp::GiveItem)
                {
                    const ItemDef* def = findItemDef(effect.key);
                    if (!def)
                    {
                        DE_LOG_ERROR(LogCategory::AI, "Dialogue: unknown item \"{}\"", effect.key);
                        break;
                    }
                    const int added = addItem(*inventory, *def, effect.count);
                    if (added < effect.count)
                        DE_LOG_WARN(LogCategory::AI, "Dialogue: giveItem \"{}\" added {} of {}", effect.key, added, effect.count);
                    break;
                }
                if (!tryRemoveItem(*inventory, effect.key, effect.count))
                    DE_LOG_WARN(LogCategory::AI, "Dialogue: takeItem \"{}\" x{} was not in the bag", effect.key, effect.count);
                break;
            }
        }
    }

} // namespace Dark
