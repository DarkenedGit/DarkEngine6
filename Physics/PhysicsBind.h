#pragma once

#include "ECS/Entity.h"

namespace Dark
{
    class Model;
    class World;
}

namespace Dark::Physics
{
    class PhysicsSurfaceCatalog;
    class PhysicsWorld;

    // Cooks a CollisionShape from the entity's PhysicsComponent and binds a Box3D body.
    // `model` is used for shape Model (and for capsule height). Disabled settings remove an
    // existing body and return true. Box3D copies the cooked shape.
    bool bindPhysicsEntity(PhysicsWorld& phys, World& world, Entity entity, const Model* model = nullptr,
                           const PhysicsSurfaceCatalog* surfaces = nullptr);
} // namespace Dark::Physics
