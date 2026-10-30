#pragma once

#include "ECS/Entity.h"
#include "ECS/Persist.h"
#include "Math/Vector3f.h"

namespace Dark
{

    class World;

    // What the player can use. Guards are taken down while alive and carried once dead.
    // Wolves are not guards, so takedown, carry, and the other human-guard queries never see them.
    enum class InteractKind : uint8_t
    {
        Item = 0,
        Loot,
        Power,
        Guard,
    };

    enum class PlayInteract : uint8_t
    {
        None,
        Body,
        Power,
        Item,
    };

    struct PlayInteractPick
    {
        PlayInteract kind = PlayInteract::None;
        Entity       e{};
    };

    struct InteractableComponent
    {
        static constexpr const char* kTypeName    = "Interactable";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;

        InteractKind kind  = InteractKind::Item;
        float        range = 2.2f;
    };

    inline constexpr float kInteractItemRange    = 2.2f;
    inline constexpr float kInteractLootRange    = 2.2f;
    inline constexpr float kInteractPowerRange   = 2.5f;
    inline constexpr float kInteractBodyRange    = 2.2f;
    inline constexpr float kGuardTakedownRange   = 2.5f;

    void setInteractable(World& world, Entity e, InteractKind kind, float range);
    void clearInteractable(World& world, Entity e);
    bool isInteractGuard(World& world, Entity e);

    // Best aim along look, past minAim, inside each component's planar range.
    // A carried body yields no pick. Dead guards are bodies. Loot counts as an item.
    PlayInteractPick pickInteractable(World& world, Entity player, const Math::Vector3f& eye, const Math::Vector3f& look, float minAim = 0.94f);

    // Nearest living guard the player stands behind. Alert guards and a carried body are skipped.
    Entity findTakedownTarget(World& world, Entity player, bool enabled);

} // namespace Dark
