#pragma once

#include "ECS/Entity.h"

namespace Dark
{

    class World;

    namespace Physics
    {
        class PhysicsWorld;
    }

    // Teleport the possessed body to the motor pose, then step. Does not sweep and does not restore a previous pose.
    void stepPossessedBody(Physics::PhysicsWorld& physics, World& world, Entity player, float dt);

} // namespace Dark
