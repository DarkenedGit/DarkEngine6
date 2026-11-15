#include "Particles/ParticleScene.h"

#include "Assets/AssetManager.h"
#include "Core/AssetPinTable.h"
#include "Core/EntityPins.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Particles/ParticleComponents.h"
#include "Content/SceneObjectSpawn.h"
#include "Particles/ParticleMaterials.h"

namespace Dark
{
    void particleDescFromSceneData(const SceneObjectData& data, ParticleEmitterDesc& out)
    {
        sceneReadParticle(data, out);
    }

    void sceneDataFromParticleDesc(const ParticleEmitterDesc& desc, SceneObjectData& data)
    {
        assignSceneParticle(data, desc);
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
