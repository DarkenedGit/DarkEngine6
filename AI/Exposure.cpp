#include "AI/Exposure.h"

#include "AI/AiSystem.h"
#include "Character/PlayerMotorComponent.h"
#include "Character/PlayerStealth.h"
#include "Collision/StaticCollision.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/MathHelper.h"
#include "Math/Ray3f.h"

#include <cmath>

namespace Dark::AI
{

    float sampleLightLevel(World& world, const Math::Vector3f& chest, float sunElevation, const Math::Vector3f& sunDir, const Math::AABox3f* cubes, int cubeCount, Entity ignoreLight)
    {
        float          sun   = Math::Clamp((sunElevation + 0.05f) * 3.0f, 0.0f, 1.0f);
        Math::Vector3f toSun = sunDir;
        if (sun > 0.0f && toSun.MagnitudeSqrd() > 1.0e-6f && cubes && cubeCount > 0)
        {
            toSun.Normalize();
            const Math::Ray3f ray{ chest, toSun };
            for (int i = 0; i < cubeCount; ++i)
            {
                const Collision::RayHit3D hit = Collision::Intersect(ray, cubes[i]);
                if (hit.hit && hit.t > 0.0f && hit.t < 80.0f)
                {
                    sun *= 0.3f;
                    break;
                }
            }
        }
        float lamps = 0.0f;
        world.each<LocalLightComponent>([&](Entity e, LocalLightComponent& light) {
            if (!light.enabled || (ignoreLight.valid() && e.id() == ignoreLight.id()))
                return;
            const TransformComponent* lxf = world.get<TransformComponent>(e);
            if (!lxf || light.range <= 0.0f)
                return;
            Math::Vector3f to   = chest - lxf->position;
            const float    dist = to.Magnitude();
            if (dist >= light.range)
                return;
            if (light.type == LocalLightType::Spot && dist > 1.0e-3f)
            {
                const Math::Vector3f fwd = lxf->rotation.Rotate(Math::Vector3f::Z_AXIS);
                if (fwd.Dot(to * (1.0f / dist)) < std::cosf(light.outerConeDeg * 0.0174533f))
                    return;
            }
            const float f = 1.0f - dist / light.range;
            lamps += f * f;
        });
        return Math::Clamp(0.12f + 0.88f * sun + lamps, 0.0f, 1.0f);
    }

    bool tickPlayerExposure(World& world, Entity player, AiSystem& ai, float dt, bool sprintDown, bool camouflage, float sunElevation, const Math::Vector3f& sunDir,
                            const Math::AABox3f* cubes, int cubeCount, Entity ignoreLight)
    {
        PlayerStealthComponent* stealth = player.valid() ? world.get<PlayerStealthComponent>(player) : nullptr;
        if (!stealth)
            return false;
        const PlayerMotor* motor = nullptr;
        if (const PlayerMotorComponent* pmc = world.get<PlayerMotorComponent>(player))
            motor = &pmc->motor;
        const float speed     = motor ? Math::Vector3f{ motor->velocity().x, 0.0f, motor->velocity().z }.Magnitude() : 0.0f;
        const bool  crouched  = motor && motor->state() == PlayerMoveState::Crouch;
        const bool  moving    = speed >= stealth->settings.stillSpeed;
        const bool  sprinting = moving && !crouched && sprintDown;
        ai.setPreySense(preySenseFor(stealth->settings, crouched, moving, sprinting));
        ai.ensureAwarenessSettings();
        const AwarenessSettings& aware = ai.awarenessSettings();

        float level = 1.0f;
        if (const TransformComponent* xf = world.get<TransformComponent>(player))
        {
            const Math::Vector3f chest{ xf->position.x, xf->position.y + 0.45f, xf->position.z };
            level = sampleLightLevel(world, chest, sunElevation, sunDir, cubes, cubeCount, ignoreLight);
        }
        float stance = crouched ? (moving ? aware.stanceCrouchWalk : aware.stanceCrouchStill) : (sprinting ? aware.stanceSprint : (moving ? aware.stanceWalk : aware.stanceStand));
        if (camouflage)
            stance *= aware.stanceCamouflage;
        const float exposure = stealth->hidden ? 0.0f : Math::Clamp((0.45f + 0.55f * level) * stance * stealth->exposureScale, 0.0f, 1.0f);
        stealth->lightLevel  = level;
        stealth->exposure    = exposure;
        ai.setPlayerExposure(exposure);
        ai.setPlayerMotion(stealth->hidden ? 0.0f : Math::Clamp((speed - 0.15f) / 1.35f, 0.0f, 1.0f));
        const bool grounded = motor && (motor->state() == PlayerMoveState::Grounded || motor->state() == PlayerMoveState::Crouch);
        if (grounded && moving && !stealth->hidden)
        {
            stealth->strideLeft -= speed * dt;
            if (stealth->strideLeft <= 0.0f)
            {
                stealth->strideLeft = crouched ? aware.strideCrouch : (sprinting ? aware.strideSprint : aware.strideWalk);
                if (const TransformComponent* pxf = world.get<TransformComponent>(player))
                    ai.addNoise(pxf->position, (crouched ? stealth->settings.hearCrouchWalk : (sprinting ? stealth->settings.hearSprint : stealth->settings.hearWalk)) * stealth->noiseScale);
            }
        }
        return true;
    }

} // namespace Dark::AI
