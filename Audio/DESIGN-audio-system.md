# Audio system (Plan A)

| Field | Value |
|-------|-------|
| **Status** | Draft rev 1 |
| **Date** | 2026-09-17 |
| **Tip baseline** | `8c5396266e8ad7cb3e0489652286e0511fec11f2` (`main`) |
| **Depends on** | `Audio/AudioSystem`, `Audio/SoundClip`, `Audio/WavFile`, `SoundEmitterComponent`, `SoundBankComponent`, `Assets/AssetManager` audio intern |
| **Out of this PR** | No code. Backend extract, miniaudio, buses, and Steam Audio are follow-up PRs in the order below. |

## Purpose

Freeze an implementable audio stack for DarkEngine6 that matches ResearchBot Plan A (revised 2026-09-17: **no FMOD**) without throwing away the working XAudio2 mixer.

`AudioSystem` stays the only facade Sandbox, emitters, and cues call. The device becomes an `IAudioBackend`. Ship order is sequential: behavior-preserving XAudio extract, then miniaudio behind the same calls, then engine buses and virtual voices while X3DAudio is still the spatializer, then Steam Audio.

Companion one-pager: [DESIGN-audio-cheatsheet.md](./DESIGN-audio-cheatsheet.md).

Research inputs (workspace, not in-repo): `/workspace/darkengine6-audio-system-brief.md`, `/workspace/darkengine6-audio-cheatsheet.md`.

## Current tip (gap snapshot)

**Present**

- `Audio/AudioSystem` — XAudio2 mastering voice + source voices. `create` / `shutdown` / `CoInitializeEx`.
- `kMaxVoices = 24`. `allocSlot` reuses idle slots, then steals the **last non-music, non-loop** voice. Music is a flag set by `setMusic` after `play`.
- `VoiceId` packs generation in the high bits and slot index in the low 8 (`(gen << 8) | (index + 1)`). `0` is invalid.
- `play` / `play2D` / `play3D` / `setMusic` / `stop` / `stopAll` / `isPlaying` / `setVoicePosition` / `tick`.
- `PlayDesc`: volume, pitch, loop, spatial, position, `minDistance` (default 2), `maxDistance` (default 64).
- PCM is **int16**. One `XAUDIO2_BUFFER` per play. Clip lifetime is a `shared_ptr<SoundClip>` on the slot. XAudio resamples the source format to the mastering rate.
- `SoundClip::createTone` / `createBlip` default to **44100 Hz**. `loadOrBlip` falls back to a tone when a WAV is missing.
- 3D is `X3DAudioCalculate` (`X3DAUDIO_CALCULATE_MATRIX | X3DAUDIO_CALCULATE_DOPPLER`) each `tick` / `setVoicePosition`. Mono spatial sources. Non-spatial mono is centered with a 1,1 matrix.
- ECS: `SoundEmitterComponent` (play flag, `VoiceId`, follow transform), `SoundBankComponent` + `playSoundCue` / `playSoundCueAt` / `playMusicCue`. `tickSoundEmitters` does **not** call `AudioSystem::tick` (Application does).
- `Audio/` is compiled by the DarkEngine umbrella (`DE_ENGINE_REST_FOLDERS`), not a separate layer.

**Missing**

- Buses, ducking, snapshots, master limiter as policy (only a single mastering volume).
- Virtual voices and category caps. Steal is positional, not audibility.
- Occlusion, transmission, HRTF, pathing / portals.
- `NoiseEvent` decoupled from mix loudness.
- Dialogue line banks, VO duck, command ring, mix callback discipline.
- A second platform. The device is `#include <xaudio2.h>` / `<x3daudio.h>`.

## Goals (v1 track)

1. Callers keep using `AudioSystem`. No Sandbox or emitter rewrite in the backend PRs.
2. `IAudioBackend` so XAudio2 and miniaudio are interchangeable behind the same `play` / `tick`.
3. First code PR is a **pure extract**: same 24 slots, same steal, same Doppler, same voice-id packing, same COM init.
4. Miniaudio lands only after `play2D` / `play3D` parity (loop, volume, stop, music protection, emitter follow). XAudio stays as a compile-time fallback until that gate.
5. Engine-owned virtual voices and the bus tree below. Device still sees only real voices.
6. Steam Audio C API replaces `X3DAudioCalculate` **after** buses exist. Headphones get HRTF. Speakers stay matrix / pan. No HRTF bake into 5.1.
7. `NoiseEvent` is a gameplay struct. It must not read `VoiceSlot` volume. Music and VO must not alert AI.
8. No C++ exceptions. Failures are `bool` + `DE_LOG_*` (same as today).
9. No malloc, decode, or file I/O inside a miniaudio data callback.

## Non-goals (v1)

- FMOD Studio / FMOD Core. Do not reintroduce without a new plan review.
- Wwise. Escape hatch later if a dedicated audio designer shows up, not this track.
- Resonance Audio (archived).
- SoLoud as the ship mixer. Optional shortcut only if buses are needed before custom virtual voices. Its pause-clock restarts loops; do not use that default for machines or music.
- Live cloud TTS on stealth-critical paths.
- Retargeting every asset to 48 kHz in the first PR. Backend resamples. `createTone` may stay 44100 until a later content pass.
- Raising `kMaxVoices` before category caps exist. A city will just be louder and hotter.
- Dialogue VM, Yarn/Ink, or lipsync in the audio PRs.

## Default stack (frozen)

| Layer | Pick | Until |
| --- | --- | --- |
| Device | **XAudio2** (today) | miniaudio `play2D` / `play3D` parity |
| Device next | **miniaudio** node graph, XAudio `#if` fallback | buses own the policy |
| Voices / events | **Engine-owned** tiers 0–4, category soft-caps | — |
| Spatial now | **X3DAudio** matrix + Doppler | Steam Audio PR |
| Spatial later | **Steam Audio C API** (HRTF, occlusion, pathing). Speakers: surround / VBAP decode, no HRTF | — |
| Stealth AI | Engine `NoiseEvent`, decoupled | — |
| Dialogue | Line-ID banks; VO bus ducks Music | after buses |
| Sample rate | Mix **48 kHz**, period **256–512** frames once miniaudio is the device. Measure latency; do not invent milliseconds. | — |

## Buses (ship tree)

Defaults preserve today’s mix. Ducking is **off** until a snapshot turns it on. A `play2D` with no bus tag routes to SFX, not UI, so existing cues do not suddenly sit on a dry UI bus or duck music.

```
Master [limiter, off until a snapshot]
├── Music          ← setMusic / playMusicCue
├── SFX
│   ├── Combat
│   ├── Foley
│   └── World      ← spatial emitters default here
├── VO             ← never shared with Music
├── UI             ← explicit non-spatial tag only
└── Ambient ──send──► Reverb → Master
```

Snapshots (later, not the extract PR): Explore, Stealth, Combat, DialogueFocus, Pause.

## Priority and steal

Replace “last non-music non-loop” only in the virtual-voice PR. Until then, keep `allocSlot` bit-identical.

| Tier | Examples | Steal? |
| --- | --- | --- |
| 0 Critical | UI confirm, active hero VO | Never |
| 1 High | Player weapon, alert sting | Prefer virtualize others first |
| 2 Medium | Near footsteps, combat foley (default for untagged `play`) | OK |
| 3 Low | Distant props | First to virtualize |
| 4 Bed | Crowd beds | Cap, don’t thrash |

Music and an active VO line stay protected, matching today’s `slot.music` skip. Looping emitters stay protected until a voice is explicitly virtualized with its clock kept (do not restart loops on steal).

Category soft-caps (start, then profile): Music 4–8, VO 4–6, player foley 8, combat 16–24, world 16, crowd one-shots 8, UI 4. Physical pool stays 24 until those caps exist.

## Facade (keep)

These stay. New fields default so existing call sites compile unchanged.

```cpp
namespace Dark::Audio
{
    enum class AudioBus : uint8_t
    {
        Sfx = 0,   // default for play / play2D
        Music,
        Combat,
        Foley,
        World,     // default when spatial == true, if bus left at Sfx
        Vo,
        Ui,
        Ambient
    };

    enum class VoicePriority : uint8_t
    {
        Critical = 0,
        High,
        Medium,    // default
        Low,
        Bed
    };

    struct PlayDesc
    {
        float          volume      = 1.0f;
        float          pitch       = 1.0f;
        bool           loop        = false;
        bool           spatial     = false;
        Math::Vector3f position;
        float          minDistance = 2.0f;
        float          maxDistance = 64.0f;
        AudioBus       bus         = AudioBus::Sfx;
        VoicePriority  priority    = VoicePriority::Medium;
    };

    class IAudioBackend
    {
    public:
        virtual ~IAudioBackend() = default;
        virtual bool create() = 0;
        virtual void shutdown() = 0;
        virtual VoiceId play(const AssetRef<SoundClip>& clip, const PlayDesc& desc) = 0;
        virtual void stop(VoiceId id) = 0;
        virtual void setPosition(VoiceId id, const Math::Vector3f& position) = 0;
        virtual void setMasterVolume(float volume) = 0;
        virtual void setListener(const AudioListener& listener) = 0;
        virtual void tick() = 0;
        virtual bool isPlaying(VoiceId id) const = 0;
    };
}
```

`AudioSystem` owns the backend, the music id, master volume, and (from the virtual-voice PR) the virtual list. `play2D` / `play3D` / `setMusic` remain thin wrappers. `setMusic` still marks the voice protected.

`NoiseEvent` (game thread only, not a voice):

```cpp
struct NoiseEvent
{
    Entity         emitter{};
    Math::Vector3f worldPos{};
    float          loudnessLogical = 0.f; // designer units, not Voice volume
    float          radius          = 0.f;
    uint32_t       tags            = 0;
};
```

## Build order

1. **Extract `XAudioBackend`.** Move `Device`, `VoiceSlot`, `apply3D`, `allocSlot` behind `IAudioBackend`. Public methods forward. No behavior change. Unit test: voice-id generation, music not stolen, loop not stolen, idle reuse, spatial flag reaches `PlayDesc`.
2. **miniaudio backend, side by side.** Same tests plus a parity checklist: one-shot ends, loop holds, `stop` invalidates id, `setMusic` survives a full SFX pool, emitter `setVoicePosition` updates pan, missing WAV still `loadOrBlip`. Do not delete XAudio. Flip the default only when that list passes on Windows.
3. **Buses + virtual voices.** Submix or engine gain stages. `PlayDesc::bus` / `priority` default as above. Steal score replaces tail-steal. X3DAudio stays. Ducking off.
4. **Steam Audio C API.** Job thread runs the sim. Mix thread applies coefficients only. Headphones HRTF; speakers matrix. Occlusion is LPF + gain on the dry path, not volume-only. Pathing bakes are content, not a blocker for the HRTF spike.
5. **NoiseEvent, snapshots, VO duck.** After buses. Dialogue banks are a later doc.

## What must not break

- `play`, `play2D`, `play3D`, `setMusic`, `stop`, `stopAll`, `isPlaying`, `setMasterVolume`, `setListener`, `tick`.
- `SoundEmitterComponent.voice` and `tickSoundEmitters` follow/stop.
- `SoundBank` cue names and `playMusicCue`.
- `loadWav` / `createTone` / `createBlip` / `loadOrBlip` and `AssetManager` intern keys (`tone:…`, `blip:…`).
- VoiceId `0` invalid; generation bump on stop so a recycled slot does not look live.
- COM: `CoInitializeEx` stays in the XAudio backend. Other subsystems (WIC) already tolerate `RPC_E_CHANGED_MODE`.
- Clip bytes stay alive for the whole voice. The slot holds `shared_ptr<SoundClip>`. A mix callback may read that pointer; it may not lock `AssetManager` or allocate.

## What will change (and when)

| Change | When | Effect |
| --- | --- | --- |
| Doppler factor and stereo matrix vs today’s `apply3D` | miniaudio flip, and again at Steam Audio | 3D sounds move in the image. API unchanged. Gate on a listening pass, not only a unit test. |
| Steal victim | virtual-voice PR | Different one-shot dies under load. Music and loops still protected. |
| 44.1 k assets on a 48 k device | miniaudio backend | Must resample in the backend. XAudio does this today. A callback that assumes device-rate PCM will play tones fast. |
| Default ducking | if someone turns it on in the bus PR | Sandbox gets quieter. Keep duck gain at 1 until a snapshot. |
| HRTF | Steam Audio, headphones only | Speaker builds must keep the matrix path. |

A hard switch off XAudio before the parity list is a break. Do not do it.

## Tradeoffs

- **No Studio editor.** Events stay `SoundBank` cues plus later JSON. That matches the engine. Authoring pain is accepted.
- **XAudio submixes vs miniaudio.** Buses can be XAudio submix voices with no new mixer, and Windows-only is fine for a long time. Miniaudio is the Plan A device because a second platform and a real node graph are the point. Do not pay that cost before parity.
- **Engine virtual voices vs SoLoud.** Custom policy keeps `VoiceId`, music protection, and loop clocks. SoLoud is faster to buses and worse for looping machines.
- **Steam Audio vs X3DAudio.** X3DAudio is distance, Doppler, and a matrix. It is enough for the prototype. Steam Audio is the stealth/city spatializer (occlusion, pathing, HRTF) and a large dependency (Apache-2, HRTF data, probe bakes, sim thread). Replacing X3D early changes every 3D sound before the mix is bused.
- **Voice budget.** Leave the physical cap at 24 until category caps exist.

## Acceptance

**Extract (must match tip behavior)**

- [ ] 25th non-loop one-shot steals the newest stealable slot, not the music voice, not a looping emitter.
- [ ] `stop` then reuse: old `VoiceId` returns false from `isPlaying`.
- [ ] `play3D` sets `spatial` and updates matrix on `tick` when the listener moves.
- [ ] `loadOrBlip` still returns a tone when the path is missing.

**miniaudio gate (Windows, before deleting or un-defaulting XAudio)**

- [ ] One-shot reaches idle without `stop`.
- [ ] Loop does not end.
- [ ] `setMusic` still playing after 24 SFX one-shots.
- [ ] Emitter follow changes pan (listener left vs right of a spatial source).
- [ ] 44100 Hz tone is not pitched up on a 48000 Hz device.

**Later**

- [ ] Untagged `play` does not duck Music.
- [ ] A `NoiseEvent` fired while Music is loud does not scale with `masterVolume`.
- [ ] Headphone path uses HRTF; a stereo speaker device does not.

## Size

| PR | Size |
| --- | --- |
| This doc | Docs only |
| `XAudioBackend` extract | Small. Move the existing `Device` pimpl. |
| miniaudio parity | Medium. This is the risky one. |
| Buses + virtual voices | Medium-large. |
| Steam Audio | Large. Not started until the two above are in. |

Full Plan A is several PRs. It is not a rewrite of the ECS surface.
