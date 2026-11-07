#include "Particles/ParticleScene.h"

#include "Assets/AssetManager.h"
#include "Core/AssetPinTable.h"
#include "Core/EntityPins.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Particles/ParticleComponents.h"
#include "Particles/ParticleMaterials.h"

namespace Dark
{
    namespace
    {

        void copy4(float dst[4], const float src[4])
        {
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
        }

        ParticleCollisionReaction reactionFromInt(int value, ParticleCollisionReaction fallback)
        {
            switch (value)
            {
            case static_cast<int>(ParticleCollisionReaction::None):
                return ParticleCollisionReaction::None;
            case static_cast<int>(ParticleCollisionReaction::Bounce):
                return ParticleCollisionReaction::Bounce;
            case static_cast<int>(ParticleCollisionReaction::Stop):
                return ParticleCollisionReaction::Stop;
            case static_cast<int>(ParticleCollisionReaction::SpawnSecondary):
                return ParticleCollisionReaction::SpawnSecondary;
            default:
                return fallback;
            }
        }

        ParticleCollisionReaction storedChildReaction(ParticleCollisionReaction reaction)
        {
            if (reaction == ParticleCollisionReaction::SpawnSecondary)
                return ParticleCollisionReaction::Stop;
            return reaction;
        }

    } // namespace

    void particleDescFromSceneData(const SceneObjectData& data, ParticleEmitterDesc& out)
    {
        out                 = ParticleEmitterDesc{};
        out.name            = data.particleName;
        out.maxParticles    = data.maxParticles;
        out.emissionRate    = data.emissionRate;
        out.duration        = data.duration;
        out.looping         = data.looping;
        out.lifetime        = { data.lifetimeMin, data.lifetimeMax };
        out.startSpeed      = { data.startSpeedMin, data.startSpeedMax };
        out.startSize       = { data.startSizeMin, data.startSizeMax };
        out.endSize         = { data.endSizeMin, data.endSizeMax };
        copy4(out.startColor, data.startColor);
        copy4(out.endColor, data.endColor);
        out.gravity         = data.gravity;
        out.direction       = data.direction;
        out.spreadDegrees   = data.spreadDegrees;
        out.shape           = static_cast<ParticleEmitterDesc::Shape>(data.shape);
        out.shapeSize       = data.shapeSize;
        out.additiveBlend   = data.additiveBlend;
        out.simulationSpeed = data.simulationSpeed;
        out.renderMode      = static_cast<ParticleEmitterDesc::RenderMode>(data.renderMode);
        out.ribbonCount     = data.ribbonCount;
        out.ribbonUvScale   = data.ribbonUvScale;
        out.simMode         = (data.simMode == static_cast<int>(ParticleSimMode::Gpu)) ? ParticleSimMode::Gpu : ParticleSimMode::Cpu;
        out.collision       = reactionFromInt(data.collision, ParticleCollisionReaction::None);
        out.restitution     = data.restitution;
        out.friction        = data.friction;
        out.collisionThickness = data.collisionThickness;
        out.collisionBias   = data.collisionBias;
        out.restLifetime    = data.restLifetime;
        out.maxSecondaryPerFrame = data.maxSecondaryPerFrame;
        out.hasSecondary    = data.hasSecondary;
        out.secondary.maxParticles  = data.secondaryMaxParticles;
        out.secondary.lifetime      = { data.secondaryLifetimeMin, data.secondaryLifetimeMax };
        out.secondary.startSize     = { data.secondaryStartSizeMin, data.secondaryStartSizeMax };
        out.secondary.restLifetime  = data.secondaryRestLifetime;
        out.secondary.additiveBlend = data.secondaryAdditiveBlend;
        out.secondary.collision     = storedChildReaction(reactionFromInt(data.secondaryCollision, ParticleCollisionReaction::Stop));
    }

    void sceneDataFromParticleDesc(const ParticleEmitterDesc& desc, SceneObjectData& data)
    {
        data.hasParticle     = true;
        data.particleName    = desc.name;
        data.maxParticles    = desc.maxParticles;
        data.emissionRate    = desc.emissionRate;
        data.duration        = desc.duration;
        data.looping         = desc.looping;
        data.lifetimeMin     = desc.lifetime.min;
        data.lifetimeMax     = desc.lifetime.max;
        data.startSpeedMin   = desc.startSpeed.min;
        data.startSpeedMax   = desc.startSpeed.max;
        data.startSizeMin    = desc.startSize.min;
        data.startSizeMax    = desc.startSize.max;
        data.endSizeMin      = desc.endSize.min;
        data.endSizeMax      = desc.endSize.max;
        copy4(data.startColor, desc.startColor);
        copy4(data.endColor, desc.endColor);
        data.gravity         = desc.gravity;
        data.direction       = desc.direction;
        data.spreadDegrees   = desc.spreadDegrees;
        data.shape           = static_cast<int>(desc.shape);
        data.shapeSize       = desc.shapeSize;
        data.additiveBlend   = desc.additiveBlend;
        data.simulationSpeed = desc.simulationSpeed;
        data.renderMode      = static_cast<int>(desc.renderMode);
        data.ribbonCount     = desc.ribbonCount;
        data.ribbonUvScale   = desc.ribbonUvScale;
        data.simMode         = static_cast<int>(desc.simMode);
        data.collision       = static_cast<int>(desc.collision);
        data.restitution     = desc.restitution;
        data.friction        = desc.friction;
        data.collisionThickness = desc.collisionThickness;
        data.collisionBias   = desc.collisionBias;
        data.restLifetime    = desc.restLifetime;
        data.maxSecondaryPerFrame = desc.maxSecondaryPerFrame;
        data.hasSecondary    = desc.hasSecondary;
        data.secondaryMaxParticles  = desc.secondary.maxParticles;
        data.secondaryLifetimeMin   = desc.secondary.lifetime.min;
        data.secondaryLifetimeMax   = desc.secondary.lifetime.max;
        data.secondaryStartSizeMin  = desc.secondary.startSize.min;
        data.secondaryStartSizeMax  = desc.secondary.startSize.max;
        data.secondaryRestLifetime  = desc.secondary.restLifetime;
        data.secondaryAdditiveBlend = desc.secondary.additiveBlend;
        data.secondaryCollision     = static_cast<int>(storedChildReaction(desc.secondary.collision));
    }

    Entity spawnSceneParticleEmitter(World& world, AssetManager& assets, AssetPinTable* pins, const SceneObjectData& data)
    {
        Entity e = world.createEntity();

        TransformComponent xf{};
        xf.position = data.position;
        xf.rotation = data.rotation;
        xf.scale    = data.scale;
        world.emplace<TransformComponent>(e, xf);

        ParticleEmitterComponent pe{};
        particleDescFromSceneData(data, pe.desc);
        pe.playing = true;
        const bool ribbon = pe.desc.renderMode == ParticleEmitterDesc::RenderMode::Ribbon;
        if (AssetRef<Material> sprite = internParticleSpriteMaterial(assets, ribbon))
            pe.matAssetID = sprite->id;
        ensureParticleRuntime(pe);
        pe.runtime->setTransform(xf.position, xf.rotation);

        world.emplace<ParticleEmitterComponent>(e, std::move(pe));
        if (pins)
        {
            if (const ParticleEmitterComponent* spawned = world.get<ParticleEmitterComponent>(e))
                pinParticleEmitter(*pins, assets, *spawned);
        }
        return e;
    }

} // namespace Dark
