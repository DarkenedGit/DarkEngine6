#include <gtest/gtest.h>

#include "ECS/World.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/ThrowableBelt.h"
#include "Save/SaveJson.h"
#include "Weapons/ProjectileWeapon.h"
#include "Weapons/ThrowableFlight.h"
#include "Weapons/WeaponLoadoutComponent.h"

#include "third_party/nlohmann/json.hpp"

#include <cmath>
#include <memory>
#include <string>
#include <string_view>

using namespace Dark;
using namespace Dark::Math;

namespace
{

    nlohmann::ordered_json oldSlotZeroRecord()
    {
        nlohmann::ordered_json shot = nlohmann::ordered_json::object();
        shot["pos"]         = nlohmann::ordered_json::array({ 1.0, 2.0, 3.0 });
        shot["prev"]        = nlohmann::ordered_json::array({ 0.5, 2.0, 3.0 });
        shot["vel"]         = nlohmann::ordered_json::array({ 10.0, 0.0, 0.0 });
        shot["age"]         = 0.25;
        shot["traveled"]    = 4.0;
        shot["damageScale"] = 1.0;
        shot["alive"]       = true;

        nlohmann::ordered_json obj = nlohmann::ordered_json::object();
        obj["slot"]         = 0;
        obj["meleeCd"]      = 1.25;
        obj["projectileCd"] = 0.5;
        obj["shots"]        = nlohmann::ordered_json::array({ shot });
        return obj;
    }

    WeaponLoadoutComponent makeLoadout(int slot, float meleeCd, float projectileCd)
    {
        WeaponLoadoutComponent comp;
        comp.loadout = std::make_unique<WeaponLoadout>();
        comp.loadout->selectSlot(slot);
        comp.slot = comp.loadout->slot();
        comp.loadout->melee().setCooldownLeft(meleeCd);
        comp.loadout->projectile().setCooldownLeft(projectileCd);
        return comp;
    }

    nlohmann::ordered_json captureRecord(const WeaponLoadoutComponent& comp)
    {
        nlohmann::ordered_json obj = nlohmann::ordered_json::object();
        Save::SaveWriter       out(obj, nullptr);
        WeaponLoadoutComponent::kPersist.capture(&comp, &out);
        EXPECT_TRUE(out.ok());
        return obj;
    }

    bool applyRecord(WeaponLoadoutComponent& comp, const nlohmann::ordered_json& obj)
    {
        Save::SaveReader in(obj, nullptr);
        WeaponLoadoutComponent::kPersist.apply(&comp, &in, WeaponLoadoutComponent::kSaveVersion);
        return !in.failed();
    }

    int recordCount(const WeaponLoadoutComponent& comp, std::string_view id)
    {
        for (const ThrowableCountRecord& row : comp.throwCounts)
        {
            if (row.id == id)
                return row.count;
        }
        return 0;
    }

    void seedThrowState(WeaponLoadoutComponent& comp)
    {
        comp.throwCounts.push_back(ThrowableCountRecord{ "rock", 4 });
        comp.throwHand = "bottle";
        ThrowableFlightSnapshot seeded;
        seeded.itemId   = "rock";
        seeded.position = Vector3f{ 1.0f, 2.0f, 3.0f };
        seeded.velocity = Vector3f{ 0.0f, -1.0f, 0.0f };
        seeded.gravity  = 18.0f;
        seeded.life     = 4.0f;
        comp.throwFlights.push_back(seeded);
    }

    float heightZero(void*, float, float)
    {
        return 0.0f;
    }

    ThrowableWorld flatGround()
    {
        ThrowableWorld ground;
        ground.hasTerrain = true;
        ground.heightAt   = heightZero;
        return ground;
    }

    bool flyUntilLanding(ThrowableFlights& flights, const ThrowableWorld& ground, float maxTime, ThrowableLanding& out)
    {
        const float dt = 1.0f / 60.0f;
        float       t  = 0.0f;
        while (t < maxTime)
        {
            flights.tick(dt, ground);
            t += dt;
            if (!flights.landings().empty())
            {
                out = flights.landings().front();
                return true;
            }
        }
        return false;
    }

    int pickupCount(World& world)
    {
        int n = 0;
        world.each<ItemPickupComponent>([&](Entity, ItemPickupComponent&) { ++n; });
        return n;
    }

} // namespace

// Old record: slot 0 plus meleeCd, projectileCd, and shots. No throwable fields.
TEST(ThrowableSave, OldSlotZeroRecordLoadsSword)
{
    EXPECT_EQ(WeaponLoadoutComponent::kSaveVersion, 1);

    const nlohmann::ordered_json obj = oldSlotZeroRecord();
    EXPECT_FALSE(obj.contains("counts"));
    EXPECT_FALSE(obj.contains("hand"));
    EXPECT_FALSE(obj.contains("flights"));

    WeaponLoadoutComponent comp = makeLoadout(1, 9.0f, 8.0f);
    seedThrowState(comp);
    EXPECT_TRUE(applyRecord(comp, obj));

    ASSERT_NE(comp.loadout, nullptr);
    EXPECT_EQ(comp.slot, 0);
    EXPECT_EQ(comp.loadout->slot(), 0);
    EXPECT_EQ(comp.loadout->activeKind(), WeaponKind::Melee);
    EXPECT_FLOAT_EQ(comp.loadout->melee().cooldownLeft(), 1.25f);
    EXPECT_FLOAT_EQ(comp.loadout->projectile().cooldownLeft(), 0.5f);
    ASSERT_EQ(comp.loadout->projectile().live().size(), 1u);
    EXPECT_TRUE(comp.loadout->projectile().live()[0].alive);
    EXPECT_FLOAT_EQ(comp.loadout->projectile().live()[0].position.x, 1.0f);
    EXPECT_FLOAT_EQ(comp.loadout->projectile().live()[0].age, 0.25f);
    EXPECT_TRUE(comp.throwCounts.empty());
    EXPECT_TRUE(comp.throwHand.empty());
    EXPECT_TRUE(comp.throwFlights.empty());
}

TEST(ThrowableSave, SlotTwoCountsAndHandRoundTrip)
{
    World  world;
    Entity actor = world.createEntity();
    auto&  wlc   = world.emplace<WeaponLoadoutComponent>(actor);
    wlc.loadout  = std::make_unique<WeaponLoadout>();
    ThrowableBelt& belt = throwableBelt(world, actor);
    ASSERT_EQ(belt.add("rock"), 1);
    ASSERT_EQ(belt.add("rock"), 2);
    ASSERT_EQ(belt.add("bottle"), 1);
    ASSERT_TRUE(belt.setHand("bottle"));
    ASSERT_TRUE(wlc.loadout->selectSlot(2));
    wlc.slot = wlc.loadout->slot();

    ThrowableFlights flights;
    syncThrowableLoadout(world, actor, flights, ThrowableLoadoutSync::OntoRecord);

    const nlohmann::ordered_json obj = captureRecord(wlc);
    EXPECT_EQ(obj["slot"], 2);
    EXPECT_TRUE(obj.contains("counts"));
    EXPECT_EQ(obj["hand"], "bottle");
    EXPECT_EQ(recordCount(wlc, "rock"), 2);
    EXPECT_EQ(recordCount(wlc, "bottle"), 1);

    ASSERT_TRUE(wlc.loadout->selectSlot(0));
    wlc.slot = 0;
    wlc.throwCounts.clear();
    wlc.throwHand.clear();
    wlc.throwFlights.clear();
    ASSERT_TRUE(belt.setCount("rock", 0));
    ASSERT_TRUE(belt.setCount("bottle", 0));
    ASSERT_TRUE(belt.setHand(""));

    EXPECT_TRUE(applyRecord(wlc, obj));
    EXPECT_EQ(wlc.slot, 2);
    EXPECT_EQ(wlc.loadout->slot(), 2);
    EXPECT_EQ(wlc.loadout->activeKind(), WeaponKind::Throwable);
    EXPECT_EQ(wlc.throwHand, "bottle");
    EXPECT_EQ(recordCount(wlc, "rock"), 2);
    EXPECT_EQ(recordCount(wlc, "bottle"), 1);

    syncThrowableLoadout(world, actor, flights, ThrowableLoadoutSync::FromRecord);
    EXPECT_EQ(belt.count("rock"), 2);
    EXPECT_EQ(belt.count("bottle"), 1);
    EXPECT_EQ(belt.hand(), "bottle");
    EXPECT_EQ(wlc.loadout->activeKind(), WeaponKind::Throwable);
}

TEST(ThrowableSave, SlotOneLoadsRifle)
{
    WeaponLoadoutComponent comp = makeLoadout(1, 1.5f, 2.5f);
    nlohmann::ordered_json obj  = captureRecord(comp);
    obj.erase("counts");
    obj.erase("hand");
    obj.erase("flights");
    EXPECT_EQ(obj["slot"], 1);
    EXPECT_FALSE(obj.contains("counts"));

    ASSERT_TRUE(comp.loadout->selectSlot(0));
    comp.slot = 0;
    comp.loadout->melee().setCooldownLeft(0.0f);
    comp.loadout->projectile().setCooldownLeft(0.0f);
    seedThrowState(comp);

    EXPECT_TRUE(applyRecord(comp, obj));
    EXPECT_EQ(comp.slot, 1);
    EXPECT_EQ(comp.loadout->slot(), 1);
    EXPECT_EQ(comp.loadout->activeKind(), WeaponKind::Projectile);
    EXPECT_FLOAT_EQ(comp.loadout->melee().cooldownLeft(), 1.5f);
    EXPECT_FLOAT_EQ(comp.loadout->projectile().cooldownLeft(), 2.5f);
    EXPECT_TRUE(comp.throwCounts.empty());
    EXPECT_TRUE(comp.throwHand.empty());
    EXPECT_TRUE(comp.throwFlights.empty());
}

TEST(ThrowableSave, SlotTwoWithZeroCountsLoadsSword)
{
    WeaponLoadoutComponent comp = makeLoadout(2, 0.2f, 0.3f);
    comp.throwHand = "rock";
    nlohmann::ordered_json obj = captureRecord(comp);
    obj.erase("counts");
    EXPECT_EQ(obj["slot"], 2);
    EXPECT_FALSE(obj.contains("counts"));
    EXPECT_EQ(obj["hand"], "rock");

    ASSERT_TRUE(comp.loadout->selectSlot(1));
    comp.slot = 1;
    seedThrowState(comp);

    EXPECT_TRUE(applyRecord(comp, obj));
    EXPECT_EQ(comp.slot, 0);
    EXPECT_EQ(comp.loadout->slot(), 0);
    EXPECT_EQ(comp.loadout->activeKind(), WeaponKind::Melee);
    EXPECT_TRUE(comp.throwHand.empty());
    EXPECT_TRUE(comp.throwCounts.empty());
    EXPECT_TRUE(comp.throwFlights.empty());
    EXPECT_FLOAT_EQ(comp.loadout->melee().cooldownLeft(), 0.2f);
    EXPECT_FLOAT_EQ(comp.loadout->projectile().cooldownLeft(), 0.3f);
}

TEST(ThrowableSave, SlotOutsideRangeFails)
{
    WeaponLoadoutComponent comp = makeLoadout(1, 4.0f, 3.0f);
    seedThrowState(comp);

    const int badSlots[] = { -1, 3 };
    for (int bad : badSlots)
    {
        nlohmann::ordered_json obj = oldSlotZeroRecord();
        obj["slot"] = bad;
        EXPECT_FALSE(applyRecord(comp, obj)) << bad;
        EXPECT_EQ(comp.loadout->slot(), 1);
        EXPECT_EQ(comp.loadout->activeKind(), WeaponKind::Projectile);
        EXPECT_FLOAT_EQ(comp.loadout->melee().cooldownLeft(), 4.0f);
        EXPECT_FLOAT_EQ(comp.loadout->projectile().cooldownLeft(), 3.0f);
        EXPECT_EQ(comp.throwHand, "bottle");
        EXPECT_EQ(recordCount(comp, "rock"), 4);
        EXPECT_EQ(comp.throwFlights.size(), 1u);
    }
}

TEST(ThrowableSave, InFlightRockResumesAndBecomesPickup)
{
    World  world;
    Entity actor = world.createEntity();
    auto&  wlc   = world.emplace<WeaponLoadoutComponent>(actor);
    wlc.loadout  = std::make_unique<WeaponLoadout>();
    wlc.slot     = 0;

    ThrowableFlights flights;
    ThrowableLaunch  launch;
    launch.itemId        = "rock";
    launch.impactClip    = "audio/foot_rock.wav";
    launch.weight        = 1.0f;
    launch.throwDistance = 10.0f;
    launch.origin        = Vector3f{ 0.0f, kThrowableRadius, 0.0f };
    launch.direction     = Vector3f{ 0.0f, std::sin(0.4f), std::cos(0.4f) };
    launch.charged       = true;
    ASSERT_TRUE(flights.launch(launch));

    syncThrowableLoadout(world, actor, flights, ThrowableLoadoutSync::OntoRecord);
    ASSERT_EQ(wlc.throwFlights.size(), 1u);
    EXPECT_EQ(wlc.throwFlights[0].itemId, "rock");
    EXPECT_EQ(wlc.throwFlights[0].impactClip, "audio/foot_rock.wav");
    EXPECT_FLOAT_EQ(wlc.throwFlights[0].weight, 1.0f);
    EXPECT_FLOAT_EQ(wlc.throwFlights[0].throwDistance, 10.0f);
    EXPECT_GT(wlc.throwFlights[0].gravity, 0.0f);
    EXPECT_FALSE(wlc.throwFlights[0].clearOfThrower);

    const nlohmann::ordered_json obj = captureRecord(wlc);
    wlc.throwFlights.clear();
    flights.clear();
    EXPECT_EQ(flights.liveCount(), 0);

    EXPECT_TRUE(applyRecord(wlc, obj));
    ASSERT_EQ(wlc.throwFlights.size(), 1u);
    EXPECT_EQ(wlc.throwFlights[0].itemId, "rock");
    EXPECT_EQ(wlc.throwFlights[0].impactClip, "audio/foot_rock.wav");
    EXPECT_FLOAT_EQ(wlc.throwFlights[0].weight, 1.0f);
    EXPECT_FLOAT_EQ(wlc.throwFlights[0].throwDistance, 10.0f);
    EXPECT_FALSE(wlc.throwFlights[0].clearOfThrower);
    EXPECT_EQ(pickupCount(world), 0);

    syncThrowableLoadout(world, actor, flights, ThrowableLoadoutSync::FromRecord);
    EXPECT_EQ(flights.liveCount(), 1);
    EXPECT_EQ(flights.liveItemId(0), "rock");
    EXPECT_EQ(pickupCount(world), 0);

    ThrowableLanding landing;
    ASSERT_TRUE(flyUntilLanding(flights, flatGround(), 8.0f, landing));
    EXPECT_EQ(landing.itemId, "rock");
    EXPECT_EQ(landing.impactClip, "audio/foot_rock.wav");
    EXPECT_TRUE(landing.hitTerrain);
    EXPECT_FALSE(landing.expired);
    EXPECT_EQ(pickupCount(world), 0);

    const Entity pickup = placeThrowablePickup(world, landing.itemId, landing.position);
    ASSERT_TRUE(pickup.valid());
    const ItemPickupComponent* item = world.get<ItemPickupComponent>(pickup);
    ASSERT_NE(item, nullptr);
    ASSERT_NE(item->item.def, nullptr);
    EXPECT_STREQ(item->item.def->id, "rock");
    EXPECT_EQ(item->item.count, 1);
    EXPECT_EQ(pickupCount(world), 1);
}
