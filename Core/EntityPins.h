#pragma once

#include "Assets/AssetHandle.h"
#include "Audio/SoundComponents.h"
#include "ECS/Components.h"
#include "ECS/Entity.h"

namespace Dark
{

    class AssetManager;
    class AssetPinTable;
    class World;
    struct ParticleEmitterComponent;

    void pinMeshComponent(AssetPinTable& pins, AssetManager& assets, const MeshComponent& mc);
    void unpinMeshComponent(AssetPinTable& pins, const MeshComponent& mc);
    void pinModelComponent(AssetPinTable& pins, AssetManager& assets, const ModelComponent& mc);
    void unpinModelComponent(AssetPinTable& pins, const ModelComponent& mc);

    void setMeshComponent(World& world, AssetPinTable& pins, AssetManager& assets, Entity e, MeshComponent next);
    void setMeshMaterial(World& world, AssetPinTable& pins, AssetManager& assets, Entity e, AssetID matId);
    void setModelComponent(World& world, AssetPinTable& pins, AssetManager& assets, Entity e, ModelComponent next);

    void pinSoundEmitter(AssetPinTable& pins, AssetManager& assets, const SoundEmitterComponent& se);
    void unpinSoundEmitter(AssetPinTable& pins, const SoundEmitterComponent& se);
    void setSoundEmitterClip(World& world, AssetPinTable& pins, AssetManager& assets, Entity e, AssetID clipId);

    void pinSoundBank(AssetPinTable& pins, AssetManager& assets, const SoundBankComponent& bank);
    void unpinSoundBank(AssetPinTable& pins, const SoundBankComponent& bank);
    void setSoundBank(World& world, AssetPinTable& pins, AssetManager& assets, Entity e, SoundBankComponent next);

    void pinParticleEmitter(AssetPinTable& pins, AssetManager& assets, const ParticleEmitterComponent& pe);
    void unpinParticleEmitter(AssetPinTable& pins, const ParticleEmitterComponent& pe);
    void setParticleMaterial(World& world, AssetPinTable& pins, AssetManager& assets, Entity e, AssetID matId);

    // Unpin Mesh/Model/Sound. Never calls world.destroyEntity.
    void onEntityRemoved(World& world, Entity e, AssetPinTable* pins);

} // namespace Dark
