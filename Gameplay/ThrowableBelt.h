#pragma once

#include "ECS/Entity.h"
#include "Gameplay/Inventory.h"
#include "Math/Vector3f.h"

#include <string>
#include <string_view>
#include <vector>

namespace Dark
{

    class AssetManager;
    class AssetPinTable;
    class Renderer;
    class World;

    namespace Physics
    {
        class PhysicsWorld;
    }

    // Counts beside the weapon loadout, keyed by throwable catalog id.
    // Pickup never changes which weapon is selected.
    // WeaponLoadout::spendThrowable spends one and updates the slot.
    class ThrowableBelt
    {
    public:
        int              count(std::string_view id) const;
        bool             any() const;
        std::string_view hand() const;
        // Empty clears. A catalog id sticks. Unknown ids are refused. Pickup does not call this.
        bool             setHand(std::string_view id);
        // +1 when id is in the catalog. Does not change hand(). Returns the new count, or 0 if unknown.
        int              add(std::string_view id);
        // -1 when the count is above zero. Returns the count left, or -1 if nothing was spent.
        int              consume(std::string_view id);

    private:
        struct Count
        {
            std::string id;
            int         count = 0;
        };

        std::vector<Count> m_counts;
        std::string        m_hand;
    };

    struct ThrowableBeltComponent
    {
        static constexpr const char* kTypeName = "ThrowableBelt";

        ThrowableBelt belt;
    };

    struct CarryPickupResult
    {
        bool throwable = false;
        int  bagAdded  = 0;
    };

    ThrowableBelt& throwableBelt(World& world, Entity actor);

    // Catalog id: add one on the belt and destroy the entity. The bag is not touched.
    // Any other id: addItem into bag when bag is non-null. A null bag does not create an inventory.
    CarryPickupResult takeCarryPickup(World& world, AssetPinTable& pins, Physics::PhysicsWorld& physics, Entity pickup, ThrowableBelt& belt, InventoryComponent* bag);

    bool worldHasThrowablePickup(World& world, std::string_view id);

    struct ThrowablePickupSpot
    {
        std::string    id;
        Math::Vector3f position{};
    };

    // Catalog throwables only. Non-throwables such as medkit are left out.
    std::vector<ThrowablePickupSpot> captureThrowablePickups(World& world);

    void respawnThrowablePickups(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer, Physics::PhysicsWorld& physics,
                                 const std::vector<ThrowablePickupSpot>& spots);

    const ItemDef* throwablePickupDef(std::string_view id);

    Entity findThrowablePickupNear(World& world, const Math::Vector3f& position, float radius);

    // Spawns a rock and/or bottle at origin when that type was not already in the scene.
    void stockMissingThrowablePickups(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer, Physics::PhysicsWorld& physics,
                                      const Math::Vector3f& origin, bool haveRock, bool haveBottle);

} // namespace Dark
