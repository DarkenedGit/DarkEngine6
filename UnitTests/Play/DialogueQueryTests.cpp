#include "AI/AiComponents.h"
#include "Character/HealthComponent.h"
#include "ECS/World.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/Item.h"
#include "Gameplay/Quest.h"
#include "Gameplay/QuestRuntime.h"
#include "Play/DialogueFacts.h"
#include "Play/DialogueQuery.h"

#include <gtest/gtest.h>

using namespace Dark;

namespace
{
    DialogueCondition cond(const char* op, const char* key, const char* cmp, int value)
    {
        DialogueCondition out;
        out.op = op;
        out.key = key;
        out.cmp = cmp;
        out.value = value;
        out.hasValue = true;
        return out;
    }
}

TEST(DialogueQuery, BagCountIgnoresEquippedHelmet)
{
    InventoryComponent inv;
    const ItemDef* medkit = findItemDef("medkit");
    const ItemDef* helmet = findItemDef("helmet");
    ASSERT_NE(medkit, nullptr);
    ASSERT_NE(helmet, nullptr);
    ASSERT_EQ(addItem(inv, *medkit, 2), 2);
    ASSERT_EQ(addItem(inv, *helmet, 1), 1);
    int helmetSlot = -1;
    for (int i = 0; i < InventoryComponent::kBagSize; ++i)
    {
        if (!inv.bag[i].empty() && inv.bag[i].def == helmet)
            helmetSlot = i;
    }
    ASSERT_TRUE(equipItem(inv, helmetSlot));
    EXPECT_EQ(countItem(inv, "helmet"), 1);
    EXPECT_EQ(countBagItem(inv, "helmet"), 0);

    DialogueGraphDef graph;
    DialogueQueryView view;
    view.inventory = &inv;
    EXPECT_TRUE(dialogueConditionPasses(graph, cond("item", "medkit", ">=", 1), view));
    EXPECT_FALSE(dialogueConditionPasses(graph, cond("item", "helmet", ">=", 1), view));
}

TEST(DialogueQuery, QuestCompleteUsesTheSettledFlag)
{
    QuestDef def;
    def.id = "gate";
    def.objectives.push_back(ObjectiveDef{ "spoke", "Speak", ObjectiveType::Event, "spoke", 1, false });
    QuestRuntime quest;
    quest.start(def);
    DialogueGraphDef graph;
    DialogueQueryView view;
    view.quest = &quest;
    DialogueCondition active;
    active.op = "questActive";
    active.key = "gate";
    DialogueCondition done;
    done.op = "questComplete";
    done.key = "gate";
    EXPECT_TRUE(dialogueConditionPasses(graph, active, view));
    EXPECT_FALSE(dialogueConditionPasses(graph, done, view));
    quest.event("spoke");
    EXPECT_TRUE(quest.complete());
    EXPECT_TRUE(quest.active());
    EXPECT_TRUE(dialogueConditionPasses(graph, done, view));
}

TEST(DialogueQuery, UnknownOpAndMissingHealthWhoHideTheChoice)
{
    DialogueGraphDef graph;
    DialogueQueryView view;
    DialogueCondition unknown;
    unknown.op = "dice";
    EXPECT_FALSE(dialogueConditionPasses(graph, unknown, view));
    EXPECT_FALSE(graph.loggedOps.empty());

    DialogueCondition health;
    health.op = "health";
    health.cmp = ">";
    health.value = 0;
    health.hasValue = true;
    EXPECT_FALSE(dialogueConditionPasses(graph, health, view));

    World world;
    Entity npc = world.createEntity();
    world.emplace<HealthComponent>(npc);
    view.npcHealth = world.get<HealthComponent>(npc);
    health.who = "npc";
    EXPECT_TRUE(dialogueConditionPasses(graph, health, view));
}

TEST(DialogueQuery, TakeIsBagOnlyAndUnknownItemsDoNotThrow)
{
    InventoryComponent inv;
    const ItemDef* medkit = findItemDef("medkit");
    const ItemDef* helmet = findItemDef("helmet");
    ASSERT_NE(medkit, nullptr);
    ASSERT_NE(helmet, nullptr);
    addItem(inv, *medkit, 2);
    addItem(inv, *helmet, 1);
    int helmetSlot = -1;
    for (int i = 0; i < InventoryComponent::kBagSize; ++i)
    {
        if (!inv.bag[i].empty() && inv.bag[i].def == helmet)
            helmetSlot = i;
    }
    ASSERT_TRUE(equipItem(inv, helmetSlot));
    EXPECT_FALSE(tryRemoveItem(inv, "helmet", 1));
    EXPECT_EQ(countItem(inv, "helmet"), 1);
    EXPECT_TRUE(tryRemoveItem(inv, "medkit", 2));
    EXPECT_EQ(countBagItem(inv, "medkit"), 0);
    EXPECT_EQ(countBagItem(inv, "medkit"), countItem(inv, "medkit"));

    DialogueGraphDef graph;
    DialogueFactsComponent facts;
    DialogueEffect effect;
    effect.op = "giveItem";
    effect.name = "not_a_real_item";
    effect.count = 1;
    applyDialogueEffects(graph, { effect }, &facts, &inv, nullptr);
    EXPECT_EQ(countBagItem(inv, "medkit"), 0);

    effect.op = "setFact";
    effect.key = "met";
    effect.value = 3;
    applyDialogueEffects(graph, { effect }, &facts, &inv, nullptr);
    EXPECT_EQ(facts.get("met"), 3);
}
