#pragma once

#include "ECS/Entity.h"
#include "Particles/ParticleTypes.h"
#include "Scene/SceneTypes.h"

namespace Dark
{

    class World;
    class AssetManager;
    class AssetPinTable;

    void particleDescFromSceneData(const SceneObjectData& data, ParticleEmitterDesc& out);
    void sceneDataFromParticleDesc(const ParticleEmitterDesc& desc, SceneObjectData& data);
    Entity spawnSceneParticleEmitter(World& world, AssetManager& assets, AssetPinTable* pins, const SceneObjectData& data);

} // namespace Dark
