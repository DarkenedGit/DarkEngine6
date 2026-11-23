#include "Play/CharacterAudio.h"

#include "Assets/AssetManager.h"
#include "Audio/AudioSystem.h"
#include "Audio/SoundComponents.h"
#include "Core/ContentRoots.h"
#include "Core/EntityPins.h"
#include "Core/Json.h"
#include "Core/Log.h"
#include "ECS/World.h"
#include "Terrain/TerrainGround.h"
#include "Weapons/WeaponLoadoutComponent.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace Dark
{
    namespace
    {
        enum class StimulusOwner : uint8_t
        {
            Player,
            Hunter,
            Both
        };

        struct StimulusRow
        {
            StimulusOwner        owner = StimulusOwner::Player;
            std::string          name;
            Audio::SoundStimulus stimulus{};
        };

        struct StimulusCache
        {
            std::vector<StimulusRow>        rows;
            bool                            manual        = false;
            bool                            haveStamp     = false;
            bool                            ok            = false;
            bool                            loggedMissing = false;
            std::filesystem::file_time_type stamp{};
            std::filesystem::path           path;
        };

        StimulusCache g_cues;

        constexpr std::uintmax_t kMaxJsonBytes = 64u * 1024u;
        constexpr const char*    kWhere        = "CueStimuli";

        bool failCue(std::string* error, const std::string& message)
        {
            DE_LOG_ERROR(LogCategory::Audio, "CueStimuli: {}", message);
            if (error)
                *error = message;
            return false;
        }

        bool overlapsOwner(StimulusOwner a, StimulusOwner b)
        {
            return a == b || a == StimulusOwner::Both || b == StimulusOwner::Both;
        }

        bool parseKind(const std::string& text, Audio::StimulusKind& kind)
        {
            if (text == "none")
            {
                kind = Audio::StimulusKind::None;
                return true;
            }
            if (text == "footstep")
            {
                kind = Audio::StimulusKind::Footstep;
                return true;
            }
            if (text == "impact")
            {
                kind = Audio::StimulusKind::Impact;
                return true;
            }
            if (text == "weapon")
            {
                kind = Audio::StimulusKind::Weapon;
                return true;
            }
            if (text == "voice")
            {
                kind = Audio::StimulusKind::Voice;
                return true;
            }
            return false;
        }

        bool parseOwner(const std::string& text, StimulusOwner& owner)
        {
            if (text == "player")
            {
                owner = StimulusOwner::Player;
                return true;
            }
            if (text == "hunter")
            {
                owner = StimulusOwner::Hunter;
                return true;
            }
            if (text == "both")
            {
                owner = StimulusOwner::Both;
                return true;
            }
            return false;
        }

        bool parseRow(const nlohmann::json& item, std::vector<StimulusRow>& rows, std::string* error)
        {
            if (!item.is_object())
                return failCue(error, "cue entry is not an object");

            std::string name;
            if (!jsonStringOr(item, "name", name, "", kWhere, error))
                return false;
            if (name.empty())
                return failCue(error, "cue name is empty");

            std::string ownerText;
            if (!jsonStringOr(item, "owner", ownerText, "", kWhere, error))
                return false;
            StimulusOwner owner = StimulusOwner::Player;
            if (!parseOwner(ownerText, owner))
                return failCue(error, "cue \"" + name + "\" has owner \"" + ownerText + "\"");

            std::string kindText;
            if (!jsonStringOr(item, "kind", kindText, "", kWhere, error))
                return false;
            Audio::StimulusKind kind = Audio::StimulusKind::None;
            if (!parseKind(kindText, kind))
                return failCue(error, "cue \"" + name + "\" has kind \"" + kindText + "\"");

            Audio::SoundStimulus stimulus{};
            stimulus.kind = kind;
            if (kind != Audio::StimulusKind::None)
            {
                if (!item.contains("radius"))
                    return failCue(error, "cue \"" + name + "\" is missing radius");
                if (!item.contains("loudness"))
                    return failCue(error, "cue \"" + name + "\" is missing loudness");
                if (!jsonFloatOr(item, "radius", stimulus.radius, 0.0f, kWhere, error))
                    return false;
                if (!jsonFloatOr(item, "loudness", stimulus.loudness, 0.0f, kWhere, error))
                    return false;
                if (stimulus.radius < 0.0f || stimulus.loudness < 0.0f)
                    return failCue(error, "cue \"" + name + "\" has a negative radius or loudness");
            }
            if (!jsonBoolOr(item, "positional", stimulus.positional, true, kWhere, error))
                return false;

            for (const StimulusRow& existing : rows)
            {
                if (existing.name == name && overlapsOwner(existing.owner, owner))
                    return failCue(error, "duplicate cue \"" + name + "\"");
            }

            StimulusRow row;
            row.owner     = owner;
            row.name      = std::move(name);
            row.stimulus  = stimulus;
            rows.push_back(std::move(row));
            return true;
        }

        bool parseCueStimuli(std::string_view jsonText, std::vector<StimulusRow>& out, std::string* error)
        {
            if (jsonText.empty() || jsonText.size() > kMaxJsonBytes)
                return failCue(error, "empty or oversized json");

            const nlohmann::json root = nlohmann::json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
            if (root.is_discarded() || !root.is_object())
                return failCue(error, "discarded or non-object json");

            int version = -1;
            if (!jsonIntOr(root, "version", version, -1, kWhere, error))
                return false;
            if (version != 1)
                return failCue(error, "version must be 1");

            const auto cues = root.find("cues");
            if (cues == root.end() || !cues->is_array())
                return failCue(error, "cues must be an array");

            std::vector<StimulusRow> parsed;
            parsed.reserve(cues->size());
            for (const nlohmann::json& item : *cues)
            {
                if (!parseRow(item, parsed, error))
                    return false;
            }
            out = std::move(parsed);
            return true;
        }

        Audio::SoundStimulus lookupCue(CueOwner owner, const char* name)
        {
            if (!name || name[0] == '\0')
                return {};
            for (const StimulusRow& row : g_cues.rows)
            {
                if (row.name != name)
                    continue;
                const bool player = owner == CueOwner::Player && row.owner == StimulusOwner::Player;
                const bool hunter = owner == CueOwner::Hunter && row.owner == StimulusOwner::Hunter;
                const bool ownerMatch = player || hunter || row.owner == StimulusOwner::Both;
                if (ownerMatch)
                    return row.stimulus;
            }
            return {};
        }

        std::filesystem::path findCueStimulusFile()
        {
            namespace fs = std::filesystem;

            std::error_code    ec;
            const fs::path     authored = authoringContentRoot() / "ai" / "sound-stimuli.json";
            if (fs::is_regular_file(authored, ec) && !ec)
                return authored;

            for (const fs::path& root : contentRootCandidates())
            {
                if (root.empty())
                    continue;
                const fs::path path = root / "ai" / "sound-stimuli.json";
                if (fs::is_regular_file(path, ec) && !ec)
                    return path;
            }
            return {};
        }

        bool readCueFile(const std::filesystem::path& path, std::string& text, std::string* error)
        {
            namespace fs = std::filesystem;

            std::error_code ec;
            const auto      sz = fs::file_size(path, ec);
            if (ec || sz == 0 || sz > kMaxJsonBytes)
                return failCue(error, "unreadable or oversized '" + path.string() + "'");

            std::ifstream in(path, std::ios::binary);
            if (!in)
                return failCue(error, "failed to open '" + path.string() + "'");
            text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return true;
        }

        void refreshCueStimuli()
        {
            if (g_cues.manual)
                return;

            namespace fs = std::filesystem;

            const fs::path path = findCueStimulusFile();
            if (path.empty())
            {
                if (!g_cues.loggedMissing)
                {
                    DE_LOG_ERROR(LogCategory::Audio, "CueStimuli: content/ai/sound-stimuli.json was not found");
                    g_cues.loggedMissing = true;
                }
                g_cues.rows.clear();
                g_cues.haveStamp = false;
                g_cues.ok        = false;
                return;
            }

            g_cues.loggedMissing = false;
            std::error_code ec;
            const auto      stamp = fs::last_write_time(path, ec);
            if (ec)
            {
                failCue(nullptr, "failed to stat '" + path.string() + "'");
                return;
            }
            if (g_cues.haveStamp && g_cues.path == path && g_cues.stamp == stamp)
                return;

            std::string              text;
            std::vector<StimulusRow> parsed;
            std::string              error;
            if (!readCueFile(path, text, &error) || !parseCueStimuli(text, parsed, &error))
            {
                g_cues.rows.clear();
                g_cues.path      = path;
                g_cues.stamp     = stamp;
                g_cues.haveStamp = true;
                g_cues.ok        = false;
                return;
            }

            g_cues.rows      = std::move(parsed);
            g_cues.path      = path;
            g_cues.stamp     = stamp;
            g_cues.haveStamp = true;
            g_cues.ok        = true;
            DE_LOG_INFO(LogCategory::Audio, "CueStimuli: loaded '{}' ({} cues)", path.string(), static_cast<unsigned>(g_cues.rows.size()));
        }

        AssetID loadClip(Audio::AudioSystem& audio, AssetManager& assets, const char* path, float freq, float dur, float amp)
        {
            const auto clip = audio.loadOrBlip(assets, path, freq, dur, amp);
            return (clip && clip->id != NULL_ASSET) ? clip->id : NULL_ASSET;
        }

        // An audible row forces the cue spatial so the play has a position to report.
        void addOwnedCue(SoundBankComponent& bank, CueOwner owner, const char* name, AssetID clipId, float volume, bool spatial)
        {
            const Audio::SoundStimulus stimulus = stimulusForCue(owner, name);
            addSoundCue(bank, name, clipId, volume, spatial || Audio::stimulusAudible(stimulus), stimulus);
        }

        void addGroundFootsteps(SoundBankComponent& bank, Audio::AudioSystem& audio, AssetManager& assets, const Terrain::TerrainGround& ground, CueOwner owner)
        {
            for (int i = 0; i < ground.layerCount(); ++i)
            {
                const Terrain::GroundContact& layer = ground.layer(i);
                if (!layer.cue || !layer.cue[0])
                    continue;
                addOwnedCue(bank, owner, layer.cue, loadClip(audio, assets, layer.footstep ? layer.footstep : "audio/foot_dirt.wav", layer.blipHz, 0.05f, 0.4f), 0.4f, true);
            }
        }

    } // namespace

    Audio::SoundStimulus stimulusForCue(CueOwner owner, const char* name)
    {
        refreshCueStimuli();
        return lookupCue(owner, name);
    }

    bool loadCueStimuli(std::string_view json, std::string* error)
    {
        std::vector<StimulusRow> parsed;
        if (!parseCueStimuli(json, parsed, error))
            return false;
        g_cues.rows   = std::move(parsed);
        g_cues.manual = true;
        g_cues.ok     = true;
        return true;
    }

    bool reloadCueStimuli()
    {
        g_cues.manual        = false;
        g_cues.haveStamp     = false;
        g_cues.ok            = false;
        g_cues.loggedMissing = false;
        refreshCueStimuli();
        return g_cues.ok;
    }

    void attachPlayerSounds(World& world, AssetPinTable& pins, AssetManager& assets, Audio::AudioSystem& audio, Entity e, const Terrain::TerrainGround& ground)
    {
        SoundBankComponent bank;
        addGroundFootsteps(bank, audio, assets, ground, CueOwner::Player);
        addOwnedCue(bank, CueOwner::Player, "step", loadClip(audio, assets, "audio/foot_grass.wav", 160.0f, 0.05f, 0.4f), 0.35f, true);
        addOwnedCue(bank, CueOwner::Player, "jump", loadClip(audio, assets, "audio/grunt.wav", 140.0f, 0.18f, 0.5f), 0.7f, false);
        addOwnedCue(bank, CueOwner::Player, "land", loadClip(audio, assets, "audio/land.wav", 70.0f, 0.12f, 0.55f), 0.75f, false);
        addOwnedCue(bank, CueOwner::Player, "splash", loadClip(audio, assets, "audio/splash.wav", 220.0f, 0.22f, 0.45f), 0.8f, false);
        addOwnedCue(bank, CueOwner::Player, "swim_step", loadClip(audio, assets, "audio/splash.wav", 220.0f, 0.22f, 0.45f), 0.42f, false);
        addOwnedCue(bank, CueOwner::Player, "pain", loadClip(audio, assets, "audio/pain.wav", 380.0f, 0.12f, 0.5f), 0.75f, true);
        addOwnedCue(bank, CueOwner::Player, "heal", loadClip(audio, assets, "audio/coin.wav", 880.0f, 0.16f, 0.4f), 0.7f, false);
        addOwnedCue(bank, CueOwner::Player, "fire", loadClip(audio, assets, "audio/whoosh.wav", 520.0f, 0.12f, 0.45f), 0.45f, true);
        addOwnedCue(bank, CueOwner::Player, "swing", loadClip(audio, assets, "audio/whoosh.wav", 520.0f, 0.12f, 0.45f), 0.7f, true);
        addOwnedCue(bank, CueOwner::Player, "impact", loadClip(audio, assets, "audio/place.wav", 180.0f, 0.10f, 0.5f), 0.5f, true);
        addOwnedCue(bank, CueOwner::Player, "status", loadClip(audio, assets, "audio/place.wav", 180.0f, 0.10f, 0.5f), 0.5f, true);
        addOwnedCue(bank, CueOwner::Player, "death", loadClip(audio, assets, "audio/whoosh.wav", 180.0f, 0.22f, 0.35f), 0.55f, true);
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
        addGroundFootsteps(bank, audio, assets, ground, CueOwner::Hunter);
        addOwnedCue(bank, CueOwner::Hunter, "pain", loadClip(audio, assets, "audio/pain.wav", 380.0f, 0.12f, 0.5f), 0.75f, true);
        addOwnedCue(bank, CueOwner::Hunter, "grunt", loadClip(audio, assets, "audio/grunt.wav", 140.0f, 0.18f, 0.5f), 0.95f, true);
        addOwnedCue(bank, CueOwner::Hunter, "growl", loadClip(audio, assets, "audio/growl.wav", 90.0f, 0.55f, 0.5f), 0.9f, true);
        const AssetID impactClip = loadClip(audio, assets, "audio/place.wav", 180.0f, 0.10f, 0.5f);
        addOwnedCue(bank, CueOwner::Hunter, "impact", impactClip, 0.5f, true);
        // Status effects play "status". Hunter "fire" alerts only when the JSON lists it.
        addOwnedCue(bank, CueOwner::Hunter, "fire", impactClip, 0.5f, true);
        addOwnedCue(bank, CueOwner::Hunter, "status", impactClip, 0.5f, true);
        addOwnedCue(bank, CueOwner::Hunter, "land", loadClip(audio, assets, "audio/land.wav", 70.0f, 0.12f, 0.55f), 0.75f, true);
        setSoundBank(world, pins, assets, e, std::move(bank));
    }

} // namespace Dark
