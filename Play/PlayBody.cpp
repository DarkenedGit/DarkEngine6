#include "Play/PlayBody.h"

#include "Character/PlayerMotorComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Physics/PhysicsWorld.h"

namespace Dark
{

    void stepPossessedBody(Physics::PhysicsWorld& physics, World& world, Entity player, float dt)
    {
        if (!physics.valid())
            return;
        physics.pushPoses(world, false);
        if (player.valid() && physics.isBound(player))
        {
            if (const PlayerMotorComponent* motor = world.get<PlayerMotorComponent>(player))
            {
                if (const TransformComponent* xf = world.get<TransformComponent>(player))
                {
                    Math::Vector3f bodyPos = xf->position;
                    bodyPos.y += motor->motor.crouchOffset();
                    physics.setBodyPose(physics.bodyOf(player), bodyPos, xf->rotation);
                }
            }
        }
        physics.step(dt, world);
        physics.writeDynamicPoses(world);
    }

} // namespace Dark
