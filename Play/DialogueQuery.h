#pragma once

#include "Play/DialogueGraph.h"

namespace Dark
{

    class QuestRuntime;
    struct AiAgentComponent;
    struct DialogueFactsComponent;
    struct HealthComponent;
    struct InventoryComponent;

    struct DialogueQueryView
    {
        const DialogueFactsComponent* facts        = nullptr;
        const InventoryComponent*     inventory    = nullptr;
        const HealthComponent*        playerHealth = nullptr;
        const HealthComponent*        npcHealth    = nullptr;
        const AiAgentComponent*       npc          = nullptr;
        const QuestRuntime*           quest        = nullptr;
    };

    // False hides the choice. Unknown ops are logged once on the graph.
    bool dialogueConditionPasses(const DialogueGraphDef& graph, const DialogueCondition& condition, const DialogueQueryView& view);

    // Unknown effects are skipped. A failed take or a short give does not stop the rest.
    void applyDialogueEffects(const DialogueGraphDef& graph, const std::vector<DialogueEffect>& effects, DialogueFactsComponent* facts,
                              InventoryComponent* inventory, QuestRuntime* quest);

} // namespace Dark
