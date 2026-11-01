#include <gtest/gtest.h>

#include "SaveTestWorld.h"

#include "ECS/World.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/Item.h"
#include "Play/Facility.h"
#include "Save/ProgressComponents.h"
#include "Scene/Building.h"

using namespace Dark;

namespace
{
    std::string capture(Save::SaveSystem& save, World& world)
    {
        armSave(save, testHost());
        std::string json;
        EXPECT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
        return json;
    }
}

TEST(FacilitySave, DoorOpenAndLockRoundTrip)
{
    World  world;
    Entity e = world.createEntity();
    DoorComponent door{};
    door.lock     = "keycard";
    door.open     = 1.0f;
    door.wantOpen = true;
    world.emplace<DoorComponent>(e, door);
    stampProceduralId(world, e, "unit/door");
    Save::SaveSystem  save;
    const std::string json = capture(save, world);
    auto* live     = world.get<DoorComponent>(e);
    live->lock     = "";
    live->open     = 0.0f;
    live->wantOpen = false;
    ASSERT_EQ(save.applyPayloadText(world, json), Save::SaveResult::Ok);
    EXPECT_EQ(live->lock, "keycard");
    EXPECT_EQ(live->open, 1.0f);
    EXPECT_TRUE(live->wantOpen);
}

TEST(FacilitySave, CameraSweepRoundTrip)
{
    World  world;
    Entity e = world.createEntity();
    SecurityCameraComponent cam{};
    cam.phase  = 2.5f;
    cam.seen   = 0.75f;
    cam.aimYaw = 1.25f;
    world.emplace<SecurityCameraComponent>(e, cam);
    stampProceduralId(world, e, "unit/cam");
    Save::SaveSystem  save;
    const std::string json = capture(save, world);
    auto* live   = world.get<SecurityCameraComponent>(e);
    live->phase  = 0.0f;
    live->seen   = 0.0f;
    live->aimYaw = 0.0f;
    ASSERT_EQ(save.applyPayloadText(world, json), Save::SaveResult::Ok);
    EXPECT_EQ(live->phase, 2.5f);
    EXPECT_EQ(live->seen, 0.75f);
    EXPECT_EQ(live->aimYaw, 1.25f);
}

TEST(FacilitySave, LootContentsRoundTripAndEmptyStaysEmpty)
{
    const ItemDef* helmet = findItemDef("helmet");
    ASSERT_NE(helmet, nullptr);
    World  world;
    Entity full  = world.createEntity();
    Entity empty = world.createEntity();
    LootComponent loot{};
    loot.label = "Locker";
    addItem(loot.contents, *helmet, 1);
    world.emplace<LootComponent>(full, loot);
    world.emplace<LootComponent>(empty, LootComponent{});
    stampProceduralId(world, full, "unit/loot/full");
    stampProceduralId(world, empty, "unit/loot/empty");
    Save::SaveSystem  save;
    const std::string json = capture(save, world);
    world.get<LootComponent>(full)->contents = {};
    addItem(world.get<LootComponent>(empty)->contents, *helmet, 1);
    ASSERT_EQ(save.applyPayloadText(world, json), Save::SaveResult::Ok);
    EXPECT_EQ(itemCount(world.get<LootComponent>(full)->contents), 1);
    EXPECT_EQ(world.get<LootComponent>(full)->contents.bag[0].def, helmet);
    EXPECT_STREQ(world.get<LootComponent>(full)->label, "Locker");
    EXPECT_EQ(itemCount(world.get<LootComponent>(empty)->contents), 0);
}

TEST(FacilitySave, ProgressKeepsUpgradesAlarmAndHideSpot)
{
    World  world;
    Entity session = world.createEntity();
    Entity hide    = world.createEntity();
    world.emplace<HideSpotComponent>(hide);
    stampProceduralId(world, session, "unit/session");
    stampProceduralId(world, hide, "unit/hide");
    FacilityState state;
    state.owned     = { "quiet_steps", "long_breath" };
    state.alarmTime = 12.0f;
    state.hiddenIn  = hide;
    writeFacilityProgress(state, world.emplace<FacilityProgressComponent>(session));
    Save::SaveSystem  save;
    const std::string json = capture(save, world);
    world.remove<FacilityProgressComponent>(session);
    ASSERT_EQ(save.applyPayloadText(world, json), Save::SaveResult::Ok);
    const FacilityProgressComponent* saved = world.get<FacilityProgressComponent>(session);
    ASSERT_NE(saved, nullptr);
    FacilityState loaded;
    readFacilityProgress(*saved, loaded, world, Entity{});
    EXPECT_EQ(loaded.owned, state.owned);
    EXPECT_EQ(loaded.alarmTime, 12.0f);
    EXPECT_EQ(loaded.hiddenIn, hide);
    EXPECT_TRUE(loaded.upgradesDirty);
}

TEST(FacilitySave, ReadingNoProgressClearsSession)
{
    World         world;
    FacilityState state;
    state.owned     = { "quiet_steps" };
    state.alarmTime = 5.0f;
    readFacilityProgress(FacilityProgressComponent{}, state, world, Entity{});
    EXPECT_TRUE(state.owned.empty());
    EXPECT_EQ(state.alarmTime, 0.0f);
    EXPECT_FALSE(state.hiddenIn.valid());
}
