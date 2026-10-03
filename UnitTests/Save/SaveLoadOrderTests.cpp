#include <gtest/gtest.h>

#include "SaveTestWorld.h"

#include "Character/HealthComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Save/ProgressComponents.h"

using namespace Dark;

namespace
{
    int g_begin = 0;
    int g_respawn = 0;

    void beginLoad(void*, World&) { ++g_begin; }

    Entity respawn(void*, World& world, std::string_view, const Save::SavePose& pose)
    {
        ++g_respawn;
        Entity e = world.createEntity();
        TransformComponent xf{};
        xf.position = pose.position;
        xf.rotation = pose.rotation;
        xf.scale    = pose.scale;
        world.emplace<TransformComponent>(e, xf);
        return e;
    }

    bool poisonHealthAndAiClock(nlohmann::ordered_json& root)
    {
        auto payload = root.find("payload");
        if (payload == root.end() || !payload->is_object())
            return false;
        auto entities = payload->find("entities");
        if (entities == payload->end() || !entities->is_array())
            return false;
        for (auto& row : *entities)
        {
            if (!row.is_object())
                continue;
            auto comps = row.find("components");
            if (comps == row.end() || !comps->is_object())
                continue;
            auto clock = comps->find("AiClock");
            if (clock == comps->end() || !clock->is_object())
                continue;
            (*clock)["time"]  = "late";
            (*clock)["token"] = "0123456789abcdef";
            nlohmann::ordered_json health = nlohmann::ordered_json::object();
            health["v"]  = 1;
            health["hp"] = "full";
            (*comps)["Health"] = std::move(health);
            return true;
        }
        return false;
    }
}

TEST(SaveLoadOrder, DuplicateIdRollsBack)
{
    g_begin = 0;
    World world;
    Entity e = world.createEntity();
    TransformComponent xf{};
    xf.position = { 1.0f, 0.0f, 0.0f };
    world.emplace<TransformComponent>(e, xf);
    stampProceduralId(world, e, "unit/dup");
    Save::SaveSystem save;
    Save::SaveHost host = testHost();
    host.beginLoad = beginLoad;
    armSave(save, host);
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
    world.get<TransformComponent>(e)->position.x = 6.0f;
    nlohmann::ordered_json root;
    ASSERT_TRUE(parseSave(json, root));
    auto entities = root["payload"].find("entities");
    ASSERT_NE(entities, root["payload"].end());
    ASSERT_FALSE(entities->empty());
    entities->push_back((*entities)[0]);
    rehashPayload(root);
    EXPECT_EQ(save.applyPayloadText(world, root.dump(2)), Save::SaveResult::DuplicateId);
    EXPECT_EQ(g_begin, 0);
    EXPECT_EQ(world.get<TransformComponent>(e)->position.x, 6.0f);
}

TEST(SaveLoadOrder, RemovedBaselineEntityIsDestroyed)
{
    World world;
    Entity e = world.createEntity();
    world.emplace<TransformComponent>(e);
    stampProceduralId(world, e, "unit/gone", "box");
    Save::SaveSystem save;
    armSave(save, testHost());
    save.captureBaseline(world);
    const uint64_t pid = static_cast<uint64_t>(world.get<PersistentIdComponent>(e)->id);
    world.destroyEntity(e);
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
    Entity again = world.createEntity();
    world.emplace<TransformComponent>(again);
    stampPersistentId(world, again, UUID{ pid }, PersistOrigin::Procedural, "box");
    ASSERT_EQ(save.applyPayloadText(world, json), Save::SaveResult::Ok);
    EXPECT_FALSE(world.alive(again));
}

TEST(SaveLoadOrder, MissingSpawnedEntityRespawns)
{
    g_begin   = 0;
    g_respawn = 0;
    World world;
    Entity e = world.createEntity();
    TransformComponent xf{};
    xf.position = { 3.0f, 4.0f, 5.0f };
    world.emplace<TransformComponent>(e, xf);
    stampSpawned(world, e, "blob");
    const uint64_t pid = static_cast<uint64_t>(world.get<PersistentIdComponent>(e)->id);
    Save::SaveSystem save;
    Save::SaveHost host = testHost();
    host.beginLoad = beginLoad;
    host.respawn   = respawn;
    armSave(save, host);
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
    world.destroyEntity(e);
    ASSERT_EQ(save.applyPayloadText(world, json), Save::SaveResult::Ok);
    EXPECT_EQ(g_begin, 1);
    EXPECT_EQ(g_respawn, 1);
    bool found = false;
    world.each<PersistentIdComponent>([&](Entity live, PersistentIdComponent& id) {
        if (static_cast<uint64_t>(id.id) != pid)
            return;
        found = true;
        const TransformComponent* placed = world.get<TransformComponent>(live);
        ASSERT_NE(placed, nullptr);
        EXPECT_EQ(placed->position.x, 3.0f);
        EXPECT_EQ(placed->position.y, 4.0f);
    });
    EXPECT_TRUE(found);
}

TEST(SaveLoadOrder, MissingNonBaselineIdIsNotRespawned)
{
    g_respawn = 0;
    World world;
    Entity authored = world.createEntity();
    world.emplace<TransformComponent>(authored);
    stampAuthoredId(world, authored, "unit/scene", 4, "crate");
    Entity procedural = world.createEntity();
    world.emplace<TransformComponent>(procedural);
    stampProceduralId(world, procedural, "unit/not-baseline", "tree");
    Save::SaveSystem save;
    Save::SaveHost host = testHost();
    host.respawn = respawn;
    armSave(save, host);
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
    world.destroyEntity(authored);
    world.destroyEntity(procedural);
    EXPECT_EQ(save.applyPayloadText(world, json), Save::SaveResult::Ok);
    EXPECT_EQ(g_respawn, 0);
}

TEST(SaveLoadOrder, KilledBaselineActorRespawns)
{
    g_respawn = 0;
    World world;
    Entity e = world.createEntity();
    TransformComponent xf{};
    xf.position = { 2.0f, 0.0f, 0.0f };
    world.emplace<TransformComponent>(e, xf);
    stampAuthoredId(world, e, "unit/scene", 1, "crate");
    const uint64_t pid = static_cast<uint64_t>(world.get<PersistentIdComponent>(e)->id);
    Save::SaveSystem save;
    Save::SaveHost host = testHost();
    host.respawn = respawn;
    armSave(save, host);
    save.captureBaseline(world);
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
    world.destroyEntity(e);
    ASSERT_EQ(save.applyPayloadText(world, json), Save::SaveResult::Ok);
    EXPECT_EQ(g_respawn, 1);
    bool found = false;
    world.each<PersistentIdComponent>([&](Entity, PersistentIdComponent& id) {
        if (static_cast<uint64_t>(id.id) == pid)
            found = true;
    });
    EXPECT_TRUE(found);
}

TEST(SaveLoadOrder, MissingEmptyArchetypeIsNotRespawned)
{
    g_respawn = 0;
    World world;
    Entity e = world.createEntity();
    world.emplace<TransformComponent>(e);
    stampProceduralId(world, e, "unit/player");
    Save::SaveSystem save;
    Save::SaveHost host = testHost();
    host.respawn = respawn;
    armSave(save, host);
    save.captureBaseline(world);
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
    world.destroyEntity(e);
    EXPECT_EQ(save.applyPayloadText(world, json), Save::SaveResult::Ok);
    EXPECT_EQ(g_respawn, 0);
}

TEST(SaveLoadOrder, InvalidApplyDoesNotInsertOrRebind)
{
    World world;
    Entity session = world.createEntity();
    stampProceduralId(world, session, "unit/bad-apply");
    Entity token = world.createEntity();
    stampProceduralId(world, token, "unit/bad-token");
    AiClockComponent clock{};
    clock.time  = 3.5f;
    clock.token = token;
    world.emplace<AiClockComponent>(session, clock);
    Save::SaveSystem save;
    armSave(save, testHost());
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
    nlohmann::ordered_json root;
    ASSERT_TRUE(parseSave(json, root));
    ASSERT_TRUE(poisonHealthAndAiClock(root));
    rehashPayload(root);
    EXPECT_EQ(save.applyPayloadText(world, root.dump(2)), Save::SaveResult::Ok);
    EXPECT_FALSE(world.has<HealthComponent>(session));
    const AiClockComponent* live = world.get<AiClockComponent>(session);
    ASSERT_NE(live, nullptr);
    EXPECT_EQ(live->time, 3.5f);
    EXPECT_EQ(live->token, token);
}

TEST(SaveLoadOrder, UnknownEntityRefBecomesNull)
{
    World world;
    Entity session = world.createEntity();
    stampProceduralId(world, session, "unit/session");
    AiClockComponent clock{};
    clock.token = world.createEntity();
    world.emplace<AiClockComponent>(session, clock);
    Save::SaveSystem save;
    armSave(save, testHost());
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
    nlohmann::ordered_json root;
    ASSERT_TRUE(parseSave(json, root));
    nlohmann::ordered_json* comp = nullptr;
    ASSERT_TRUE(componentJson(root, "AiClock", comp));
    (*comp)["token"] = "0123456789abcdef";
    rehashPayload(root);
    ASSERT_EQ(save.applyPayloadText(world, root.dump(2)), Save::SaveResult::Ok);
    EXPECT_FALSE(world.get<AiClockComponent>(session)->token.valid());
}
