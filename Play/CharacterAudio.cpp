#include "Play/CharacterAudio.h"

#include "Assets/AssetManager.h"
#include "Audio/AudioSystem.h"
#include "Audio/SoundComponents.h"
#include "Core/EntityPins.h"
#include "ECS/World.h"
#include "Terrain/TerrainGround.h"
#include "Weapons/WeaponLoadoutComponent.h"

#include <cstring>

namespace Dark
{

    Audio::SoundStimulus stimulusForCue(CueOwner owner, const char* name)
    {
        auto make = [](Audio::StimulusKind kind, float radius, float loudness) {
            Audio::SoundStimulus s;
            s.radius     = radius;
            s.loudness   = loudness;
            s.kind       = kind;
            s.positional = true;
            return s;
        };
        if (!name || name[0] == '\0')
            return {};
        if (std::strcmp(name, "fire") == 0)
            return owner == CueOwner::Player ? make(Audio::StimulusKind::Weapon, 48.0f, 4.0f) : Audio::SoundStimulus{};
        if (std::strcmp(name, "swing") == 0 && owner == CueOwner::Player)
            return make(Audio::StimulusKind::Weapon, 12.0f, 3.4f);
        if (std::strcmp(name, "impact") == 0)
            return make(Audio::StimulusKind::Impact, 14.0f, 3.4f);
        if (std::strcmp(name, "pain") == 0)
            return make(Audio::StimulusKind::Voice, 28.0f, 5.0f);
        if (std::strcmp(name, "grunt") == 0 && owner == CueOwner::Hunter)
            return make(Audio::StimulusKind::Voice, 18.0f, 3.4f);
        if (std::strcmp(name, "death") == 0 && owner == CueOwner::Player)
            return make(Audio::StimulusKind::Voice, 36.0f, 5.0f);
        return {};
    }

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
        addSoundCue(bank, "pain", loadClip(audio, assets, "audio/pain.wav", 380.0f, 0.12f, 0.5f), 0.75f, true, stimulusForCue(CueOwner::Player, "pain"));
        addSoundCue(bank, "heal", loadClip(audio, assets, "audio/coin.wav", 880.0f, 0.16f, 0.4f), 0.7f, false);
        addSoundCue(bank, "fire", loadClip(audio, assets, "audio/whoosh.wav", 520.0f, 0.12f, 0.45f), 0.45f, true, stimulusForCue(CueOwner::Player, "fire"));
        addSoundCue(bank, "swing", loadClip(audio, assets, "audio/whoosh.wav", 520.0f, 0.12f, 0.45f), 0.7f, true, stimulusForCue(CueOwner::Player, "swing"));
        addSoundCue(bank, "impact", loadClip(audio, assets, "audio/place.wav", 180.0f, 0.10f, 0.5f), 0.5f, true, stimulusForCue(CueOwner::Player, "impact"));
        addSoundCue(bank, "status", loadClip(audio, assets, "audio/place.wav", 180.0f, 0.10f, 0.5f), 0.5f, true);
        addSoundCue(bank, "death", loadClip(audio, assets, "audio/whoosh.wav", 180.0f, 0.22f, 0.35f), 0.55f, true, stimulusForCue(CueOwner::Player, "death"));
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
            wlc->loadout->projectile().setAudio(&audio, &assets, fireId, impactId, stimulusForCue(CueOwner::Player, "fire"), stimulusForCue(CueOwner::Player, "impact"), e.id());
        }
    }

    void attachHunterSounds(World& world, AssetPinTable& pins, AssetManager& assets, Audio::AudioSystem& audio, Entity e, const Terrain::TerrainGround& ground)
    {
        SoundBankComponent bank;
        addGroundFootsteps(bank, audio, assets, ground);
        addSoundCue(bank, "pain", loadClip(audio, assets, "audio/pain.wav", 380.0f, 0.12f, 0.5f), 0.75f, true, stimulusForCue(CueOwner::Hunter, "pain"));
        addSoundCue(bank, "grunt", loadClip(audio, assets, "audio/grunt.wav", 140.0f, 0.18f, 0.5f), 0.95f, true, stimulusForCue(CueOwner::Hunter, "grunt"));
        addSoundCue(bank, "growl", loadClip(audio, assets, "audio/growl.wav", 90.0f, 0.55f, 0.5f), 0.9f, true);
        const AssetID impactClip = loadClip(audio, assets, "audio/place.wav", 180.0f, 0.10f, 0.5f);
        addSoundCue(bank, "impact", impactClip, 0.5f, true, stimulusForCue(CueOwner::Hunter, "impact"));
        // Untagged alias of the impact clip. Status effects play "status" and must not inherit a weapon tag.
        addSoundCue(bank, "fire", impactClip, 0.5f, true);
        addSoundCue(bank, "status", impactClip, 0.5f, true);
        addSoundCue(bank, "land", loadClip(audio, assets, "audio/land.wav", 70.0f, 0.12f, 0.55f), 0.75f, true);
        setSoundBank(world, pins, assets, e, std::move(bank));
    }

} // namespace Dark
