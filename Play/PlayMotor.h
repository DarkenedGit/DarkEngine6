#pragma once

#include "Character/PlayerMotor.h"
#include "Combat/JumpAttack.h"
#include "Math/Vector3f.h"
#include "Physics/PhysicsWorld.h"

namespace Dark
{

    inline constexpr float kPlayBodyRadius = 0.45f;

    struct PlayMotorEdges
    {
        bool sprintDown    = false;
        bool crouchPressed = false;
        bool crouchDown    = false;
        bool jumpPressed   = false;
        bool forward       = false;
        bool back          = false;
        bool left          = false;
        bool right         = false;
    };

    // Fills steer, crouch latch, jump, and dodge. speedScale and the skill scales stay with the caller.
    // sprintAllowed already includes the sandbox swing lock. Pass a fresh motorIn; this does not read the old one.
    inline void buildPlayMotorInput(PlayerMotorInput& motorIn, bool& crouchLatch, const Math::Vector3f& wish, const Math::Vector3f& flat, const Math::Vector3f& right, bool canSteer, bool jumpBusy, bool uiKeys, bool inAirCommit, const Combat::JumpAttack* jump, bool sprintAllowed, const PlayMotorEdges& edges)
    {
        motorIn.wish            = canSteer ? wish : Math::Vector3f{ 0.0f, 0.0f, 0.0f };
        motorIn.sprint          = sprintAllowed && edges.sprintDown;
        if (crouchLatch && edges.crouchPressed)
            crouchLatch = false;
        motorIn.crouch          = canSteer && !jumpBusy && !uiKeys && (crouchLatch || edges.crouchDown);
        motorIn.jumpPressed     = canSteer && !jumpBusy && !uiKeys && edges.jumpPressed;
        motorIn.allowDoubleJump = !inAirCommit;
        motorIn.allowJumpBuffer = !jumpBusy;
        motorIn.airControlScale = (inAirCommit && jump) ? jump->def().airControlScale : 1.0f;
        motorIn.allowDodge      = canSteer && !jumpBusy && !uiKeys;
        if (!motorIn.allowDodge)
            return;
        motorIn.dodgeTap     = moveCardinalFromEdges(edges.forward, edges.back, edges.left, edges.right);
        motorIn.dodgeTapWish = moveCardinalWish(motorIn.dodgeTap, flat, right);
    }

    // Capsule against static physics after the app's own solid sweep. bottom is -radius so the feet clear the floor.
    inline float tightenPlanarClip(const Physics::PhysicsWorld& physics, Physics::PhysicsBodyId ignoreBody, Physics::PhysicsBodyId groundBody, const Math::Vector3f& origin, const Math::Vector3f& delta, float bestT)
    {
        if (!physics.valid())
            return bestT;
        Physics::PhysicsWorld::MoverCast cast;
        cast.origin      = origin;
        cast.radius      = kPlayBodyRadius;
        cast.bottom      = -kPlayBodyRadius;
        cast.top         = 1.35f;
        cast.translation = delta;
        cast.ignore      = ignoreBody;
        cast.ignore2     = groundBody;
        cast.staticOnly  = true;
        const float physicsT = physics.clipMover(cast);
        return physicsT < bestT ? physicsT : bestT;
    }

    inline void commitPlanarClip(Math::Vector3f& position, const Math::Vector3f& before, Math::Vector3f& delta, float bestT, float dt, PlayerMotor* motor)
    {
        const float keep = bestT < 1.0f ? Math::Max(0.0f, bestT - 0.02f) : 1.0f;
        delta *= keep;
        position.x = before.x + delta.x;
        position.z = before.z + delta.z;
        if (dt > 1.0e-4f && motor && keep < 1.0f)
            motor->setHorizontalVelocity(delta.x / dt, delta.z / dt);
    }

} // namespace Dark
