#include "Play/CharacterAudio.h"

#include "Assets/AssetManager.h"
#include "Audio/AudioSystem.h"
#include "Audio/SoundComponents.h"
#include "Core/EntityPins.h"
#include "ECS/World.h"
#include "Terrain/TerrainGround.h"
#include "Weapons/WeaponLoadoutComponent.h"

namespace Dark
{

    namespace
    {

        AssetID loadClip(Audio::AudioSystem& audio, AssetManager& assets, const char* path, float freq, float dur, float amp)
        {
            const auto clip = audio.loadOrBlip(assets, path, freq, dur, amp);
            return (clip && clip->id != NULL_ASSET) ? clip->id : NULL_ASSET;
        }

        void addGroundFootsteps(SoundBankComponent& bank, Audio::AudioSystem& audio, AssetManager& assets, const Terrain::TerrainGround& ground)
        {
            for (int i = 0; i < ground.layerCount(); ++i)
            {
                const Terrain::GroundContact& layer = ground.layer(i);
                if (!layer.cue || !layer.cue[0])
                    continue;
                addSoundCue(bank, layer.cue, loadClip(audio, assets, layer.footstep ? layer.footstep : "audio/foot_dirt.wav", layer.blipHz, 0.05f, 0.4f), 0.4f, true);
            }
        }

    } // namespace

    void attachPlayerSounds(World& world, AssetPinTable& pins, AssetManager& assets, Audio::AudioSystem& audio, Entity e, const Terrain::TerrainGround& ground)
    {
        SoundBankComponent bank;
        addGroundFootsteps(bank, audio, assets, ground);
        addSoundCue(bank, "step", loadClip(audio, assets, "audio/foot_grass.wav", 160.0f, 0.05f, 0.4f), 0.35f, true);
        addSoundCue(bank, "jump", loadClip(audio, assets, "audio/grunt.wav", 140.0f, 0.18f, 0.5f), 0.7f, false);
        addSoundCue(bank, "land", loadClip(audio, assets, "audio/land.wav", 70.0f, 0.12f, 0.55f), 0.75f, false);
        addSoundCue(bank, "splash", loadClip(audio, assets, "audio/splash.wav", 220.0f, 0.22f, 0.45f), 0.8f, false);
        addSoundCue(bank, "swim_step", loadClip(audio, assets, "audio/splash.wav", 220.0f, 0.22f, 0.45f), 0.42f, false);
        addSoundCue(bank, "pain", loadClip(audio, assets, "audio/pain.wav", 380.0f, 0.12f, 0.5f), 0.75f, true);
        addSoundCue(bank, "heal", loadClip(audio, assets, "audio/coin.wav", 880.0f, 0.16f, 0.4f), 0.7f, false);
        addSoundCue(bank, "fire", loadClip(audio, assets, "audio/whoosh.wav", 520.0f, 0.12f, 0.45f), 0.45f, true);
        addSoundCue(bank, "swing", loadClip(audio, assets, "audio/whoosh.wav", 520.0f, 0.12f, 0.45f), 0.7f, true);
        addSoundCue(bank, "impact", loadClip(audio, assets, "audio/place.wav", 180.0f, 0.10f, 0.5f), 0.5f, true);
        addSoundCue(bank, "death", loadClip(audio, assets, "audio/whoosh.wav", 180.0f, 0.22f, 0.35f), 0.55f, false);
        setSoundBank(world, pins, assets, e, std::move(bank));

        const AssetID waterId = loadClip(audio, assets, "audio/whoosh.wav", 70.0f, 0.8f, 0.25f);
        if (waterId != NULL_ASSET && !world.has<SoundEmitterComponent>(e))
        {
            SoundEmitterComponent se{};
            se.clipId  = waterId;
            se.volume  = 0.28f;
            se.looping = true;
            se.spatial = false;
            se.play    = false;
            world.emplace<SoundEmitterComponent>(e, se);
            pinSoundEmitter(pins, assets, se);
        }

        if (WeaponLoadoutComponent* wlc = world.get<WeaponLoadoutComponent>(e); wlc && wlc->loadout)
        {
            AssetID fireId   = NULL_ASSET;
            AssetID impactId = NULL_ASSET;
            if (const SoundBankComponent* cues = world.get<SoundBankComponent>(e))
            {
                if (const SoundCueDesc* c = findSoundCue(*cues, "fire"))
                    fireId = c->clipId;
                if (const SoundCueDesc* c = findSoundCue(*cues, "impact"))
                    impactId = c->clipId;
            }
            wlc->loadout->projectile().setAudio(&audio, &assets, fireId, impactId);
        }
    }

    void attachHunterSounds(World& world, AssetPinTable& pins, AssetManager& assets, Audio::AudioSystem& audio, Entity e, const Terrain::TerrainGround& ground)
    {
        SoundBankComponent bank;
        addGroundFootsteps(bank, audio, assets, ground);
        addSoundCue(bank, "pain", loadClip(audio, assets, "audio/pain.wav", 380.0f, 0.12f, 0.5f), 0.75f, true);
        addSoundCue(bank, "grunt", loadClip(audio, assets, "audio/grunt.wav", 140.0f, 0.18f, 0.5f), 0.95f, true);
        addSoundCue(bank, "growl", loadClip(audio, assets, "audio/growl.wav", 90.0f, 0.55f, 0.5f), 0.9f, true);
        const AssetID impactClip = loadClip(audio, assets, "audio/place.wav", 180.0f, 0.10f, 0.5f);
        addSoundCue(bank, "impact", impactClip, 0.5f, true);
        // Catalog Poison/Ignite applyCue is "fire"; hunters have no weapon fire row — alias impact so one-shots aren't silent.
        addSoundCue(bank, "fire", impactClip, 0.5f, true);
        addSoundCue(bank, "land", loadClip(audio, assets, "audio/land.wav", 70.0f, 0.12f, 0.55f), 0.75f, true);
        setSoundBank(world, pins, assets, e, std::move(bank));
    }

} // namespace Dark
