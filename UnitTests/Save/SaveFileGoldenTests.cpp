#include <gtest/gtest.h>

#include "SaveTestWorld.h"

#include "ECS/Components.h"
#include "ECS/World.h"
#include "Save/SaveTypes.h"

using namespace Dark;

TEST(SaveFileGolden, PayloadChecksumRoundTrips)
{
    World world;
    Entity e = world.createEntity();
    TransformComponent xf{};
    xf.position = { 4.0f, 5.0f, 6.0f };
    world.emplace<TransformComponent>(e, xf);
    stampProceduralId(world, e, "unit/golden", "box");
    Save::SaveSystem save;
    armSave(save, testHost());
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);

    nlohmann::ordered_json root;
    ASSERT_TRUE(parseSave(json, root));
    const auto payload = root.find("payload");
    ASSERT_NE(payload, root.end());
    const std::string compact = payload->dump();
    const uint64_t hash = Save::fnv1a64(compact.data(), compact.size());
    const auto integrity = root.find("integrity");
    ASSERT_NE(integrity, root.end());
    const auto* hex = integrity->find("payload")->get_ptr<const std::string*>();
    ASSERT_NE(hex, nullptr);
    uint64_t stored = 0;
    ASSERT_TRUE(Save::fromHex16(*hex, stored));
    EXPECT_EQ(stored, hash);
    EXPECT_NE(json.find("\"format\""), std::string::npos);
    EXPECT_NE(json.find("DarkEngine6.Save"), std::string::npos);

    world.get<TransformComponent>(e)->position = { 0.0f, 0.0f, 0.0f };
    ASSERT_EQ(save.applyPayloadText(world, json), Save::SaveResult::Ok);
    EXPECT_EQ(world.get<TransformComponent>(e)->position.x, 4.0f);
}
