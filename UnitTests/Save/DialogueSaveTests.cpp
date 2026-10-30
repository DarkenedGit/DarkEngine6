#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/Item.h"
#include "Play/DialogueFacts.h"
#include "Play/DialogueMemory.h"
#include "Save/PersistentId.h"
#include "SaveTestWorld.h"

#include <gtest/gtest.h>

using namespace Dark;

TEST(DialogueSave, MemoryRoundTripKeepsChoiceAndSeparateHeardLists)
{
    World world;
    Entity a = world.createEntity();
    Entity b = world.createEntity();
    DialogueMemoryComponent first;
    first.bookmark.valid = true;
    first.bookmark.graphId = "gate";
    first.bookmark.nodeId = "hello";
    first.bookmark.pending = DialoguePending::Choice;
    first.lines.onceHeard = { "ask" };
    first.lines.cycleHeard = { "hi" };
    first.lines.rng = 42u;
    DialogueMemoryComponent second;
    second.lines.cycleHeard = { "other" };
    second.lines.rng = 7u;
    world.emplace<DialogueMemoryComponent>(a, first);
    world.emplace<DialogueMemoryComponent>(b, second);
    stampProceduralId(world, a, "unit/dlg-a");
    stampProceduralId(world, b, "unit/dlg-b");

    Save::SaveSystem save;
    armSave(save, testHost());
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
    world.get<DialogueMemoryComponent>(a)->bookmark.pending = DialoguePending::Line;
    world.get<DialogueMemoryComponent>(a)->lines.onceHeard.clear();
    world.get<DialogueMemoryComponent>(b)->lines.cycleHeard = { "changed" };
    ASSERT_EQ(save.applyPayloadText(world, json), Save::SaveResult::Ok);

    const DialogueMemoryComponent* loadedA = world.get<DialogueMemoryComponent>(a);
    const DialogueMemoryComponent* loadedB = world.get<DialogueMemoryComponent>(b);
    ASSERT_NE(loadedA, nullptr);
    ASSERT_NE(loadedB, nullptr);
    EXPECT_TRUE(loadedA->bookmark.valid);
    EXPECT_EQ(loadedA->bookmark.pending, DialoguePending::Choice);
    EXPECT_EQ(loadedA->lines.onceHeard, std::vector<std::string>({ "ask" }));
    EXPECT_EQ(loadedA->lines.cycleHeard, std::vector<std::string>({ "hi" }));
    EXPECT_EQ(loadedA->lines.rng, 42u);
    EXPECT_EQ(loadedB->lines.cycleHeard, std::vector<std::string>({ "other" }));
    EXPECT_TRUE(loadedB->lines.onceHeard.empty());
}

TEST(DialogueSave, MissingKeysAndNewerVersionLeaveTheComponent)
{
    World world;
    Entity e = world.createEntity();
    DialogueFactsComponent facts;
    facts.set("met", 2);
    world.emplace<DialogueFactsComponent>(e, facts);
    stampProceduralId(world, e, "unit/dlg-facts");
    Save::SaveSystem save;
    armSave(save, testHost());
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
    nlohmann::ordered_json root;
    ASSERT_TRUE(parseSave(json, root));
    nlohmann::ordered_json* comp = nullptr;
    ASSERT_TRUE(componentJson(root, "DialogueFacts", comp));
    comp->erase("facts");
    rehashPayload(root);
    world.get<DialogueFactsComponent>(e)->set("met", 9);
    ASSERT_EQ(save.applyPayloadText(world, root.dump(2)), Save::SaveResult::Ok);
    EXPECT_EQ(world.get<DialogueFactsComponent>(e)->get("met"), 9);

    (*comp)["v"] = 99;
    (*comp)["facts"] = nlohmann::ordered_json::array({ nlohmann::ordered_json::object({ { "k", "met" }, { "v", 1 } }) });
    rehashPayload(root);
    EXPECT_EQ(save.applyPayloadText(world, root.dump(2)), Save::SaveResult::Ok);
    EXPECT_EQ(world.get<DialogueFactsComponent>(e)->get("met"), 9);
}

TEST(DialogueSave, BadStringLeavesMemoryUnchanged)
{
    World world;
    Entity e = world.createEntity();
    DialogueMemoryComponent memory;
    memory.lines.onceHeard = { "keep" };
    world.emplace<DialogueMemoryComponent>(e, memory);
    stampProceduralId(world, e, "unit/dlg-bad");
    Save::SaveSystem save;
    armSave(save, testHost());
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
    nlohmann::ordered_json root;
    ASSERT_TRUE(parseSave(json, root));
    nlohmann::ordered_json* comp = nullptr;
    ASSERT_TRUE(componentJson(root, "DialogueMemory", comp));
    (*comp)["onceHeard"] = nlohmann::ordered_json::array({ nlohmann::ordered_json::object({ { "id", std::string(80, 'x') } }) });
    rehashPayload(root);
    EXPECT_EQ(save.applyPayloadText(world, root.dump(2)), Save::SaveResult::Ok);
    EXPECT_EQ(world.get<DialogueMemoryComponent>(e)->lines.onceHeard, std::vector<std::string>({ "keep" }));
}

TEST(DialogueSave, UnknownItemSkipsOneStack)
{
    World world;
    Entity e = world.createEntity();
    InventoryComponent inv;
    const ItemDef* medkit = findItemDef("medkit");
    ASSERT_NE(medkit, nullptr);
    addItem(inv, *medkit, 2);
    world.emplace<InventoryComponent>(e, inv);
    stampProceduralId(world, e, "unit/bag");
    Save::SaveSystem save;
    armSave(save, testHost());
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
    nlohmann::ordered_json root;
    ASSERT_TRUE(parseSave(json, root));
    nlohmann::ordered_json* comp = nullptr;
    ASSERT_TRUE(componentJson(root, "Inventory", comp));
    (*comp)["bag"].push_back(nlohmann::ordered_json::object({ { "id", "nope" }, { "count", 1 } }));
    rehashPayload(root);
    world.get<InventoryComponent>(e)->bag[0] = {};
    ASSERT_EQ(save.applyPayloadText(world, root.dump(2)), Save::SaveResult::Ok);
    EXPECT_EQ(countBagItem(*world.get<InventoryComponent>(e), "medkit"), 2);
    EXPECT_EQ(countBagItem(*world.get<InventoryComponent>(e), "nope"), 0);
}

TEST(DialogueSave, DestroyedStampedPickupStaysGone)
{
    World world;
    Entity pickup = world.createEntity();
    world.emplace<TransformComponent>(pickup);
    world.emplace<ItemPickupComponent>(pickup);
    stampProceduralId(world, pickup, "level/pickup/0", "pickup");
    Save::SaveSystem save;
    armSave(save, testHost());
    save.captureBaseline(world);
    world.destroyEntity(pickup);
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);

    World fresh;
    Entity again = fresh.createEntity();
    fresh.emplace<TransformComponent>(again);
    fresh.emplace<ItemPickupComponent>(again);
    stampProceduralId(fresh, again, "level/pickup/0", "pickup");
    ASSERT_EQ(save.applyPayloadText(fresh, json), Save::SaveResult::Ok);
    int left = 0;
    fresh.each<PersistentIdComponent>([&](Entity, PersistentIdComponent&) { ++left; });
    EXPECT_EQ(left, 0);
}
