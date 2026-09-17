#include "Core/EntityPins.h"

#include "Assets/AssetManager.h"
#include "Audio/SoundComponents.h"
#include "Particles/ParticleComponents.h"
#include "Core/AssetPinTable.h"
#include "Core/Log.h"
#include "ECS/World.h"

namespace Dark
{

    void pinMeshComponent(AssetPinTable& pins, AssetManager& assets, const MeshComponent& mc)
    {
        pins.pin(assets, mc.meshAssetID);
        pins.pin(assets, mc.matAssetID);
    }

    void unpinMeshComponent(AssetPinTable& pins, const MeshComponent& mc)
    {
        pins.unpin(mc.meshAssetID);
        pins.unpin(mc.matAssetID);
    }

    void pinModelComponent(AssetPinTable& pins, AssetManager& assets, const ModelComponent& mc)
    {
        pins.pin(assets, mc.modelAssetID);
    }

    void unpinModelComponent(AssetPinTable& pins, const ModelComponent& mc)
    {
        pins.unpin(mc.modelAssetID);
    }

    void setMeshComponent(World& world, AssetPinTable& pins, AssetManager& assets, Entity e, MeshComponent next)
    {
        if (!world.alive(e))
        {
            DE_LOG_ERROR("setMeshComponent: dead entity");
            return;
        }
        if (const MeshComponent* old = world.get<MeshComponent>(e))
            unpinMeshComponent(pins, *old);
        world.emplace<MeshComponent>(e, next);
        pinMeshComponent(pins, assets, next);
    }

    void setModelComponent(World& world, AssetPinTable& pins, AssetManager& assets, Entity e, ModelComponent next)
    {
        if (!world.alive(e))
        {
            DE_LOG_ERROR("setModelComponent: dead entity");
            return;
        }
        if (const ModelComponent* old = world.get<ModelComponent>(e))
            unpinModelComponent(pins, *old);
        world.emplace<ModelComponent>(e, next);
        pinModelComponent(pins, assets, next);
    }

    void setMeshMaterial(World& world, AssetPinTable& pins, AssetManager& assets, Entity e, AssetID matId)
    {
        MeshComponent* mc = world.get<MeshComponent>(e);
        if (!mc)
        {
            DE_LOG_ERROR("setMeshMaterial: entity has no MeshComponent");
            return;
        }
        if (mc->matAssetID == matId)
            return;
        pins.unpin(mc->matAssetID);
        mc->matAssetID = matId;
        pins.pin(assets, matId);
    }

    void pinSoundEmitter(AssetPinTable& pins, AssetManager& assets, const SoundEmitterComponent& se)
    {
        pins.pin(assets, se.clipId);
    }

    void unpinSoundEmitter(AssetPinTable& pins, const SoundEmitterComponent& se)
    {
        pins.unpin(se.clipId);
    }

    void setSoundEmitterClip(World& world, AssetPinTable& pins, AssetManager& assets, Entity e, AssetID clipId)
    {
        SoundEmitterComponent* se = world.get<SoundEmitterComponent>(e);
        if (!se)
        {
            DE_LOG_ERROR("setSoundEmitterClip: entity has no SoundEmitterComponent");
            return;
        }
        if (se->clipId == clipId)
            return;
        pins.unpin(se->clipId);
        se->clipId = clipId;
        pins.pin(assets, clipId);
    }

    void pinSoundBank(AssetPinTable& pins, AssetManager& assets, const SoundBankComponent& bank)
    {
        for (const SoundCueDesc& cue : bank.cues)
            pins.pin(assets, cue.clipId);
    }

    void unpinSoundBank(AssetPinTable& pins, const SoundBankComponent& bank)
    {
        for (const SoundCueDesc& cue : bank.cues)
            pins.unpin(cue.clipId);
    }

    void setSoundBank(World& world, AssetPinTable& pins, AssetManager& assets, Entity e, SoundBankComponent next)
    {
        if (!world.alive(e))
        {
            DE_LOG_ERROR("setSoundBank: dead entity");
            return;
        }
        if (const SoundBankComponent* old = world.get<SoundBankComponent>(e))
            unpinSoundBank(pins, *old);
        world.emplace<SoundBankComponent>(e, std::move(next));
        if (const SoundBankComponent* bank = world.get<SoundBankComponent>(e))
            pinSoundBank(pins, assets, *bank);
    }

    void pinParticleEmitter(AssetPinTable& pins, AssetManager& assets, const ParticleEmitterComponent& pe)
    {
        pins.pin(assets, pe.matAssetID);
    }

    void unpinParticleEmitter(AssetPinTable& pins, const ParticleEmitterComponent& pe)
    {
        pins.unpin(pe.matAssetID);
    }

    void setParticleMaterial(World& world, AssetPinTable& pins, AssetManager& assets, Entity e, AssetID matId)
    {
        ParticleEmitterComponent* pe = world.get<ParticleEmitterComponent>(e);
        if (!pe)
        {
            DE_LOG_ERROR("setParticleMaterial: entity has no ParticleEmitterComponent");
            return;
        }
        if (pe->matAssetID == matId)
            return;
        pins.unpin(pe->matAssetID);
        pe->matAssetID = matId;
        pins.pin(assets, matId);
    }

    void onEntityRemoved(World& world, Entity e, AssetPinTable* pins)
    {
        if (!pins || !world.alive(e))
            return;
        if (const MeshComponent* mc = world.get<MeshComponent>(e))
            unpinMeshComponent(*pins, *mc);
        if (const ModelComponent* mo = world.get<ModelComponent>(e))
            unpinModelComponent(*pins, *mo);
        if (const SoundEmitterComponent* se = world.get<SoundEmitterComponent>(e))
            unpinSoundEmitter(*pins, *se);
        if (const SoundBankComponent* bank = world.get<SoundBankComponent>(e))
            unpinSoundBank(*pins, *bank);
        if (const ParticleEmitterComponent* pe = world.get<ParticleEmitterComponent>(e))
            unpinParticleEmitter(*pins, *pe);
    }

} // namespace Dark
