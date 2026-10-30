#include <gtest/gtest.h>

#include "Save/SaveTestWorld.h"

#include "AI/AiComponents.h"
#include "Character/HealthComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/Item.h"
#include "Gameplay/PowerComponents.h"
#include "Play/Carry.h"
#include "Play/CharacterAttach.h"
#include "Play/Interaction.h"

#include <cmath>
#include <string>

using namespace Dark;

namespace
{

    const Math::Vector3f kEye{ 0.0f, 1.6f, 0.0f };
    const Math::Vector3f kLook{ 0.0f, 0.0f, 1.0f };
    const ItemDef        kFile{ "file", "File", ItemType::Quest, EquipSlot::None, 1 };

    Entity makePlayer(World& world)
    {
        const Entity e = world.createEntity();
        world.emplace<TransformComponent>(e, Math::Vector3f{ 0.0f, 0.0f, 0.0f }, Math::Quaternion::IDENTITY, Math::Vector3f{ 1.0f, 1.0f, 1.0f });
        return e;
    }

    Entity makePower(World& world, const Math::Vector3f& pos, bool withTransform)
    {
        const Entity e = world.createEntity();
        if (withTransform)
            world.emplace<TransformComponent>(e, pos, Math::Quaternion::IDENTITY, Math::Vector3f{ 1.0f, 1.0f, 1.0f });
        PowerBoxComponent box{};
        box.circuit = "A";
        box.pos     = pos;
        world.emplace<PowerBoxComponent>(e, box);
        setInteractable(world, e, InteractKind::Power, kInteractPowerRange);
        return e;
    }

    Entity makeGuard(World& world, const Math::Vector3f& pos, bool alive, AI::Alertness alert, const Math::Vector3f& forward)
    {
        const Entity e = world.createEntity();
        world.emplace<TransformComponent>(e, pos, Math::Quaternion::IDENTITY, Math::Vector3f{ 1.0f, 1.0f, 1.0f });
        HealthComponent& hp = world.emplace<HealthComponent>(e);
        if (!alive)
            hp.health.applyDamage(1000.0f);
        AiAgentComponent& ai = world.emplace<AiAgentComponent>(e);
        ai.forward           = forward;
        ai.aware.alertness   = alert;
        setInteractable(world, e, InteractKind::Guard, kInteractBodyRange);
        return e;
    }

    PlayInteractPick lookAt(World& world, Entity player)
    {
        return pickInteractable(world, player, kEye, kLook);
    }

} // namespace

TEST(InteractPick, PowerRangeAndAim)
{
    World        world;
    const Entity player = makePlayer(world);
    makePower(world, Math::Vector3f{ 0.0f, 1.6f, 2.6f }, true);
    EXPECT_EQ(lookAt(world, player).kind, PlayInteract::None);

    const Entity inRange = makePower(world, Math::Vector3f{ 0.0f, 1.6f, 2.4f }, true);
    PlayInteractPick pick = lookAt(world, player);
    EXPECT_EQ(pick.kind, PlayInteract::Power);
    EXPECT_EQ(pick.e, inRange);

    clearInteractable(world, inRange);
    const float dist = 2.0f;
    const float wideZ = 0.90f * dist;
    const float wideX = std::sqrt(dist * dist - wideZ * wideZ);
    makePower(world, Math::Vector3f{ wideX, 1.6f, wideZ }, true);
    EXPECT_EQ(lookAt(world, player).kind, PlayInteract::None);

    const float aimedZ = 0.99f * dist;
    const float aimedX = std::sqrt(dist * dist - aimedZ * aimedZ);
    const Entity aimed = makePower(world, Math::Vector3f{ aimedX, 1.6f, aimedZ }, true);
    pick = lookAt(world, player);
    EXPECT_EQ(pick.kind, PlayInteract::Power);
    EXPECT_EQ(pick.e, aimed);

    const Entity boxOnly = makePower(world, Math::Vector3f{ 0.0f, 1.6f, 1.2f }, false);
    pick = lookAt(world, player);
    EXPECT_EQ(pick.kind, PlayInteract::Power);
    EXPECT_EQ(pick.e, boxOnly);
}

TEST(InteractPick, DeadGuardIsABodyAndALiveGuardIsNot)
{
    World        world;
    const Entity player = makePlayer(world);
    const Entity guard  = makeGuard(world, Math::Vector3f{ 0.0f, 0.0f, 1.5f }, true, AI::Alertness::Calm, Math::Vector3f{ 0.0f, 0.0f, 1.0f });
    EXPECT_EQ(lookAt(world, player).kind, PlayInteract::None);
    EXPECT_EQ(findTakedownTarget(world, player, true), guard);

    world.get<TransformComponent>(guard)->position.y = 1.6f;
    world.get<HealthComponent>(guard)->health.applyDamage(1000.0f);
    const PlayInteractPick pick = lookAt(world, player);
    EXPECT_EQ(pick.kind, PlayInteract::Body);
    EXPECT_EQ(pick.e, guard);
    EXPECT_FALSE(findTakedownTarget(world, player, true).valid());
}

TEST(InteractPick, UnmarkedDeadAgentIsIgnored)
{
    World        world;
    const Entity player = makePlayer(world);
    const Entity wolf   = world.createEntity();
    world.emplace<TransformComponent>(wolf, Math::Vector3f{ 0.0f, 1.6f, 1.5f }, Math::Quaternion::IDENTITY, Math::Vector3f{ 1.0f, 1.0f, 1.0f });
    world.emplace<TagComponent>(wolf, "Wolf");
    HealthComponent& hp = world.emplace<HealthComponent>(wolf);
    hp.health.applyDamage(1000.0f);
    AiAgentComponent& ai = world.emplace<AiAgentComponent>(wolf);
    ai.forward           = Math::Vector3f{ 0.0f, 0.0f, 1.0f };

    EXPECT_EQ(lookAt(world, player).kind, PlayInteract::None);
    EXPECT_FALSE(findTakedownTarget(world, player, true).valid());
    EXPECT_FALSE(isInteractGuard(world, wolf));
}

TEST(InteractPick, ItemAndLootShareTheItemPrompt)
{
    World        world;
    const Entity player = makePlayer(world);

    const Entity item = world.createEntity();
    world.emplace<TransformComponent>(item, Math::Vector3f{ 0.0f, 1.6f, 1.2f }, Math::Quaternion::IDENTITY, Math::Vector3f{ 1.0f, 1.0f, 1.0f });
    world.emplace<ItemPickupComponent>(item, ItemPickupComponent{ ItemStack{ &kFile, 1 } });
    setInteractable(world, item, InteractKind::Item, kInteractItemRange);
    PlayInteractPick pick = lookAt(world, player);
    EXPECT_EQ(pick.kind, PlayInteract::Item);
    EXPECT_EQ(pick.e, item);

    world.get<ItemPickupComponent>(item)->item = {};
    EXPECT_EQ(lookAt(world, player).kind, PlayInteract::None);

    const Entity bag = world.createEntity();
    world.emplace<TransformComponent>(bag, Math::Vector3f{ 0.0f, 1.6f, 1.4f }, Math::Quaternion::IDENTITY, Math::Vector3f{ 1.0f, 1.0f, 1.0f });
    LootComponent loot{};
    world.emplace<LootComponent>(bag, loot);
    setInteractable(world, bag, InteractKind::Loot, kInteractLootRange);
    EXPECT_EQ(lookAt(world, player).kind, PlayInteract::None);

    world.get<LootComponent>(bag)->contents.bag[0] = ItemStack{ &kFile, 2 };
    pick = lookAt(world, player);
    EXPECT_EQ(pick.kind, PlayInteract::Item);
    EXPECT_EQ(pick.e, bag);
}

TEST(InteractPick, CarryingBlocksPickAndTakedown)
{
    World        world;
    const Entity player = makePlayer(world);
    const Entity guard  = makeGuard(world, Math::Vector3f{ 0.0f, 0.0f, 1.5f }, true, AI::Alertness::Calm, Math::Vector3f{ 0.0f, 0.0f, 1.0f });
    const Entity box    = makePower(world, Math::Vector3f{ 0.0f, 1.6f, 1.0f }, true);
    const Entity sack   = world.createEntity();
    world.emplace<CarriedByComponent>(sack);

    EXPECT_EQ(lookAt(world, player).kind, PlayInteract::None);
    EXPECT_FALSE(findTakedownTarget(world, player, true).valid());

    world.remove<CarriedByComponent>(sack);
    EXPECT_EQ(lookAt(world, player).e, box);
    EXPECT_EQ(findTakedownTarget(world, player, true), guard);
}

TEST(InteractTakedown, RejectsAlertFrontHeightAndADisabledChase)
{
    World        world;
    const Entity player = makePlayer(world);
    const Entity calm   = makeGuard(world, Math::Vector3f{ 0.0f, 0.0f, 2.0f }, true, AI::Alertness::Calm, Math::Vector3f{ 0.0f, 0.0f, 1.0f });
    EXPECT_EQ(findTakedownTarget(world, player, true), calm);
    EXPECT_FALSE(findTakedownTarget(world, player, false).valid());

    world.get<AiAgentComponent>(calm)->aware.alertness = AI::Alertness::Alert;
    EXPECT_FALSE(findTakedownTarget(world, player, true).valid());

    world.get<AiAgentComponent>(calm)->aware.alertness = AI::Alertness::Calm;
    world.get<AiAgentComponent>(calm)->forward         = Math::Vector3f{ 0.0f, 0.0f, -1.0f };
    EXPECT_FALSE(findTakedownTarget(world, player, true).valid());

    world.get<AiAgentComponent>(calm)->forward          = Math::Vector3f{ 0.0f, 0.0f, 1.0f };
    world.get<TransformComponent>(calm)->position.y     = 1.3f;
    EXPECT_FALSE(findTakedownTarget(world, player, true).valid());

    world.get<TransformComponent>(calm)->position.y = 0.0f;
    EXPECT_EQ(findTakedownTarget(world, player, true), calm);
}

TEST(InteractSave, KindAndRangeRoundTrip)
{
    World        world;
    const Entity e = world.createEntity();
    world.emplace<TransformComponent>(e);
    setInteractable(world, e, InteractKind::Guard, 3.5f);
    stampProceduralId(world, e, "unit/interact", "guard");

    Save::SaveSystem save;
    armSave(save, testHost());
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);

    nlohmann::ordered_json root;
    ASSERT_TRUE(parseSave(json, root));
    nlohmann::ordered_json* comp = nullptr;
    ASSERT_TRUE(componentJson(root, "Interactable", comp));
    EXPECT_EQ((*comp)["kind"].get<std::string>(), "Guard");
    EXPECT_FLOAT_EQ((*comp)["range"].get<float>(), 3.5f);

    InteractableComponent* live = world.get<InteractableComponent>(e);
    ASSERT_NE(live, nullptr);
    live->kind  = InteractKind::Item;
    live->range = 1.0f;
    ASSERT_EQ(save.applyPayloadText(world, json), Save::SaveResult::Ok);
    EXPECT_EQ(live->kind, InteractKind::Guard);
    EXPECT_FLOAT_EQ(live->range, 3.5f);

    (*comp)["kind"] = "Nope";
    rehashPayload(root);
    live->kind  = InteractKind::Power;
    live->range = 4.0f;
    ASSERT_EQ(save.applyPayloadText(world, root.dump()), Save::SaveResult::Ok);
    EXPECT_EQ(live->kind, InteractKind::Power);
    EXPECT_FLOAT_EQ(live->range, 4.0f);

    (*comp)["kind"]  = "Loot";
    (*comp)["range"] = 1001.0f;
    rehashPayload(root);
    ASSERT_EQ(save.applyPayloadText(world, root.dump()), Save::SaveResult::Ok);
    EXPECT_EQ(live->kind, InteractKind::Power);
    EXPECT_FLOAT_EQ(live->range, 4.0f);
}

TEST(InteractSave, OldAgentsGainAGuardMarkWithoutClobberingOne)
{
    World world;

    const Entity hunter = world.createEntity();
    world.emplace<TagComponent>(hunter, "Hunter");
    world.emplace<AiAgentComponent>(hunter);

    const Entity wolf = world.createEntity();
    world.emplace<TagComponent>(wolf, "Wolf");
    world.emplace<AiAgentComponent>(wolf);

    const Entity kept = world.createEntity();
    world.emplace<TagComponent>(kept, "Hunter");
    world.emplace<AiAgentComponent>(kept);
    setInteractable(world, kept, InteractKind::Guard, 4.0f);

    applyAuthoredMovement(world);

    const InteractableComponent* marked = world.get<InteractableComponent>(hunter);
    ASSERT_NE(marked, nullptr);
    EXPECT_EQ(marked->kind, InteractKind::Guard);
    EXPECT_FLOAT_EQ(marked->range, kInteractBodyRange);
    EXPECT_FALSE(world.has<InteractableComponent>(wolf));
    const InteractableComponent* restored = world.get<InteractableComponent>(kept);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->kind, InteractKind::Guard);
    EXPECT_FLOAT_EQ(restored->range, 4.0f);
}
