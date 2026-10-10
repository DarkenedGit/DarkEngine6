#include "Character/ItemView.h"
#include "Assets/AssetManager.h"
#include "Assets/Model.h"
#include "Core/EntityPins.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Physics/PhysicsBind.h"
#include "Physics/PhysicsComponent.h"
#include "Physics/PhysicsWorld.h"
#include "Render/GpuUpload.h"

namespace Dark
{

    Entity spawnItemPickup(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer, Physics::PhysicsWorld& physics,
                           const ItemStack& item, const Math::Vector3f& position, const Math::Vector3f& velocity)
    {
        if (item.empty())
            return {};
        AssetRef<Model> cube = loadAndUploadModel(renderer, assets, "models/unit_cube.gltf");
        if (!cube)
            return {};

        const Entity e = world.createEntity();
        world.emplace<TransformComponent>(e, position, Math::Quaternion::IDENTITY, Math::Vector3f{ 0.3f, 0.3f, 0.3f });
        ModelComponent mc{};
        mc.modelAssetID = cube->id;
        mc.castShadow   = true;
        setModelComponent(world, pins, assets, e, mc);
        world.emplace<ItemPickupComponent>(e, ItemPickupComponent{ item });

        if (physics.valid())
        {
            PhysicsComponent phys;
            phys.mode  = PhysicsBodyMode::Dynamic;
            phys.shape = PhysicsShapeKind::Box;
            world.emplace<PhysicsComponent>(e, phys);
            if (Physics::bindPhysicsEntity(physics, world, e))
                physics.setBodyVelocity(physics.bodyOf(e), velocity, Math::Vector3f{});
        }
        return e;
    }

    Entity findPickupNear(World& world, const Math::Vector3f& position, float radius)
    {
        Entity best{};
        float  bestD = radius * radius;
        world.each<ItemPickupComponent>([&](Entity e, ItemPickupComponent& pickup) {
            const TransformComponent* xf = world.get<TransformComponent>(e);
            if (!xf || pickup.item.empty())
                return;
            const float d = (xf->position - position).MagnitudeSqrd();
            if (d <= bestD)
            {
                best  = e;
                bestD = d;
            }
        });
        return best;
    }

    void destroyPickup(World& world, AssetPinTable& pins, Physics::PhysicsWorld& physics, Entity pickup)
    {
        if (!pickup.valid() || !world.alive(pickup))
            return;
        physics.destroyBody(world, pickup);
        if (const ModelComponent* mc = world.get<ModelComponent>(pickup))
            unpinModelComponent(pins, *mc);
        world.destroyEntity(pickup);
    }

} // namespace Dark
