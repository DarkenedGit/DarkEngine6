#include "Play/PlayFeedback.h"

#include "Play/PlaySession.h"

#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"
#include "Particles/BloodSplatPool.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleTypes.h"
#include "Render/Camera3D.h"
#include "Terrain/HeightMap.h"

namespace Dark
{

    namespace
    {

        void pulseMuzzle(PlayFeedback& feedback)
        {
            if (!feedback.world || !feedback.camera || !feedback.muzzle || !feedback.muzzle->valid())
                return;
            if (!feedback.world->alive(*feedback.muzzle))
                return;
            World& world = *feedback.world;
            const Entity muzzle = *feedback.muzzle;
            if (TransformComponent* xf = world.get<TransformComponent>(muzzle))
                xf->position = feedback.camera->GetPosition() + feedback.camera->GetLook() * 0.8f;
            if (LocalLightComponent* light = world.get<LocalLightComponent>(muzzle))
            {
                light->enabled   = true;
                light->intensity = 37699.0f; // 12000*π after Fd/π
                light->range     = 6.0f;
            }
            if (feedback.muzzleTimer)
                *feedback.muzzleTimer = 0.05f;
        }

        void onMuzzle(void* user)
        {
            if (auto* feedback = static_cast<PlayFeedback*>(user))
                pulseMuzzle(*feedback);
        }

        void onBloodBurst(void* user, const Math::Vector3f& pos)
        {
            auto* feedback = static_cast<PlayFeedback*>(user);
            if (!feedback || !feedback->blood)
                return;
            feedback->blood->setTransform(Math::Vector3f{ pos.x, pos.y + 0.45f, pos.z });
            feedback->blood->emitBurst(12);
        }

        void onBloodSplat(void* user, float x, float z)
        {
            auto* feedback = static_cast<PlayFeedback*>(user);
            if (!feedback || !feedback->splats || !feedback->height || !feedback->height->valid())
                return;
            if (!feedback->splats->isValid())
                return;
            feedback->splats->spawn(x, z, *feedback->height);
        }

        void onRespawn(void* user)
        {
            auto* feedback = static_cast<PlayFeedback*>(user);
            if (!feedback)
                return;
            if (feedback->world && feedback->muzzle && feedback->muzzle->valid() && feedback->world->alive(*feedback->muzzle))
            {
                if (LocalLightComponent* light = feedback->world->get<LocalLightComponent>(*feedback->muzzle))
                    light->enabled = false;
            }
            if (feedback->muzzleTimer)
                *feedback->muzzleTimer = 0.0f;
            if (feedback->afterRespawn)
                feedback->afterRespawn(feedback->afterUser);
        }

    } // namespace

    void installPlayFeedback(PlayHost& host, PlayFeedback* feedback)
    {
        host.feedback     = feedback;
        host.onMuzzle     = feedback ? &onMuzzle : nullptr;
        host.onBloodBurst = feedback ? &onBloodBurst : nullptr;
        host.onBloodSplat = feedback ? &onBloodSplat : nullptr;
        host.onRespawn    = feedback ? &onRespawn : nullptr;
        if (feedback && feedback->muzzle)
            host.muzzle = *feedback->muzzle;
    }

    const ParticleEmitterDesc& playBloodBurstDesc()
    {
        static const ParticleEmitterDesc desc = [] {
            ParticleEmitterDesc blood{};
            blood.name          = "Blood";
            blood.maxParticles  = 128;
            blood.emissionRate  = 0.0f;
            blood.duration      = 0.0f;
            blood.looping       = false;
            blood.lifetime      = { 0.22f, 0.50f };
            blood.startSpeed    = { 1.8f, 4.2f };
            blood.startSize     = { 0.07f, 0.14f };
            blood.endSize       = { 0.02f, 0.05f };
            blood.startColor[0] = 0.72f;
            blood.startColor[1] = 0.04f;
            blood.startColor[2] = 0.06f;
            blood.startColor[3] = 0.95f;
            blood.endColor[0]   = 0.28f;
            blood.endColor[1]   = 0.00f;
            blood.endColor[2]   = 0.01f;
            blood.endColor[3]   = 0.00f;
            blood.gravity       = Math::Vector3f{ 0.0f, -11.0f, 0.0f };
            blood.direction     = Math::Vector3f{ 0.0f, 1.0f, 0.0f };
            blood.spreadDegrees = 75.0f;
            blood.shape         = ParticleEmitterDesc::Shape::Sphere;
            blood.shapeSize     = Math::Vector3f{ 0.12f, 0.0f, 0.0f };
            blood.additiveBlend = false;
            return blood;
        }();
        return desc;
    }

    void ensurePlayMuzzle(World& world, Entity& muzzle, float& timer)
    {
        if (muzzle.valid() && world.alive(muzzle))
            return;
        muzzle = world.createEntity();
        world.emplace<TagComponent>(muzzle, "Muzzle");
        world.emplace<TransformComponent>(muzzle, Math::Vector3f{}, Math::Quaternion::IDENTITY, Math::Vector3f{ 1.0f, 1.0f, 1.0f });
        LocalLightComponent& light = world.emplace<LocalLightComponent>(muzzle);
        light.type                 = LocalLightType::Point;
        light.color                = Math::Vector3f{ 1.0f, 0.82f, 0.45f };
        light.intensity            = 37699.0f;
        light.range                = 6.0f;
        light.enabled              = false;
        timer                      = 0.0f;
    }

    void destroyPlayMuzzle(World& world, Entity& muzzle, float& timer)
    {
        if (muzzle.valid() && world.alive(muzzle))
            world.destroyEntity(muzzle);
        muzzle = {};
        timer  = 0.0f;
    }

    void tickPlayMuzzle(const PlayFeedback& feedback, float dt)
    {
        if (!feedback.muzzleTimer || *feedback.muzzleTimer <= 0.0f || !feedback.world || !feedback.muzzle)
            return;
        *feedback.muzzleTimer -= dt;
        World&       world  = *feedback.world;
        const Entity muzzle = *feedback.muzzle;
        if (*feedback.muzzleTimer <= 0.0f)
        {
            *feedback.muzzleTimer = 0.0f;
            if (LocalLightComponent* light = (muzzle.valid() && world.alive(muzzle)) ? world.get<LocalLightComponent>(muzzle) : nullptr)
                light->enabled = false;
            return;
        }
        if (!feedback.camera || !muzzle.valid() || !world.alive(muzzle))
            return;
        if (TransformComponent* xf = world.get<TransformComponent>(muzzle))
            xf->position = feedback.camera->GetPosition() + feedback.camera->GetLook() * 0.8f;
    }

} // namespace Dark
