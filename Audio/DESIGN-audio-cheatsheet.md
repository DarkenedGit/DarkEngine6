# Audio Plan A cheatsheet

Companion to [DESIGN-audio-system.md](./DESIGN-audio-system.md). Tip baseline `8c539626`.

**Rule:** game thread commands, mixer mixes. AI noise is not player loudness. Float32 at the device. **No FMOD.**

## Stack

| Layer | Pick |
| --- | --- |
| Device now | XAudio2 + X3DAudio (`kMaxVoices = 24`) |
| Device next | miniaudio, XAudio `#if` until `play2D` / `play3D` parity |
| Voices | Engine-owned tiers 0–4. Do not raise the physical cap first. |
| Spatial later | Steam Audio C API. Headphones HRTF. Speakers matrix / pan. |
| Stealth | `NoiseEvent` loudness, not `Voice` volume |
| Dialogue | Line-ID banks. VO ducks Music. After buses. |

## Keep calling

`AudioSystem::play` / `play2D` / `play3D` / `setMusic` / `stop` / `tick`. `SoundEmitter` and `SoundBank` stay. New `PlayDesc` fields default to SFX + Medium.

## Buses

```
Master
├── Music     ← setMusic
├── SFX (Combat / Foley / World)
├── VO
├── UI        ← explicit tag only
└── Ambient → Reverb
```

Ducking off until a snapshot. Untagged `play2D` is SFX, not UI.

## Steal (today vs later)

Today: last non-music, non-loop. Later: lowest audibility among non-protected. Music and active VO never stolen. Loops keep their clock.

## Build order

1. Extract `XAudioBackend`. No behavior change.
2. miniaudio side by side. Flip only after parity.
3. Buses + virtual voices. X3DAudio stays.
4. Steam Audio.
5. NoiseEvent, snapshots, VO duck.

## Explode list

- Rip XAudio before `play2D` parity
- HRTF on speakers
- malloc / decode / file I/O in the miniaudio callback
- Duck Music on every existing cue
- Restart loops when stealing
- AI alert from Music or master volume
- FMOD, Resonance, live cloud TTS
- One uncapped voice pool in a city

## Size

Extract: small. miniaudio parity: medium (the risk). Buses: medium-large. Steam Audio: large.
