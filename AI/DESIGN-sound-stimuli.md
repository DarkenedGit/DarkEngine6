# Gameplay sound stimuli

| Field | Value |
| --- | --- |
| **Status** | Implemented |
| **Date** | 2026-11-22 |

Tagged cues report themselves when they play. Guards hear weapon fire, impacts, and voices without a manual `addNoise` at each call site. Footsteps stay on the exposure stride.

## Tag

The tag lives on the cue (`SoundCueDesc::stimulus` in `Audio/SoundComponents.h`), not on the wav and not as a loudness argument at the call. The rows are `content/ai/sound-stimuli.json`, keyed by owner (`player`, `hunter`, or `both`) and cue name. `stimulusForCue` reads that file. A name that is not listed does not alert. Hunter `fire` is omitted, so a status tick is not a gunshot.

Edit the JSON and start play again, or spawn the character again. The loader prefers the authoring `content/` folder over the copy next to the executable, so the change does not wait for a rebuild. A bank that is already in the world keeps the row it copied at attach. An audible row forces that cue spatial, so the play has a position. Kind `none`, radius 0, or loudness 0 leaves the cue quiet. `positional: false` does the same.

`Audio::SoundStimulus` (`Audio/SoundStimulus.h`) holds radius (meters), loudness, kind (`Footstep`, `Impact`, `Weapon`, `Voice`), and a positional flag. A stimulus is audible only when the kind is set, it is positional, and both radius and loudness are above zero.

Shipped rows:

| Cue | Owner | Kind | Radius | Loudness |
| --- | --- | --- | --- | --- |
| fire | player | weapon | 48 | 4 |
| swing | player | weapon | 12 | 3.4 |
| impact | both | impact | 14 | 3.4 |
| pain | both | voice | 28 | 5 |
| grunt | hunter | voice | 18 | 3.4 |
| death | player | voice | 36 | 5 |

Loudness 3.4 is the floor. With the default noise curve (`noiseBase` 0.3), one in-range event of that loudness fills a Calm guard into Suspicious.

Omitted, and quiet until a row is added: hunter fire, status, footsteps, jump, land, splash, swim step, heal, growl, hunter land. Clicks, music, barks, dialogue, the alarm `play2D`, Editor preview, crow caws, and rock or bottle impacts are not this table. Throwables still search through `postThrowableLanding`. Tagging `step` or a ground footstep cue adds a second impulse on top of the exposure stride.

The point-blank loudness floor (`kSoundAlertLoudness`, 2) and the wait curve (`soundInvestigateDelaySeconds`) stay in code.

Player death is spatial, so the cue has a position to report. `play3D` stays untagged and has no source id. Editor preview does not set a stimulus.

Status effects play the untagged `status` cue (the place clip). Poison, Ignite, Bleed, Chill, Shock, and Stun no longer use `fire` or `impact`.

## Report

`AudioSystem::play` calls the stimulus hook after the main-thread check and before the device, the clip, and the 24-voice cap. A mute, volume 0, occlusion, a full mixer, a dead device, or world-hold still alerts. The hook ignores UI, loops, and non-spatial plays. `play2D` and `setMusic` never copy a stimulus.

`PlaySession::tickWorld` installs the hook when both audio and the AI system are set. The hook calls `AiSystem::addNoise` and is cleared when the tick returns. `resetPawn` clears it as well, so Editor stop and Sandbox boot cannot leave a dangling AI pointer. Sandbox and Editor share this path. There is no second copy in either shell.

A shot fired in `tickCombat` is scored on the next hunter tick. `tickHunters` clears the noise list at the end. Combat order is unchanged.

## Hearing

Only awareness guards read the list. Wolves and hunters with awareness off do not. A guard skips a noise whose source is his own id, so a scream does not retarget the victim. The best impulse wins. Same-frame shots do not add together.

Impulse is `loudness * (noiseBase + noiseFalloff * (1 - distance / effectiveRange))`. A cube on the ray to the sound multiplies the radius by `wallScale` (default 0.5). That scaled radius decides whether the guard scores the sound. It does not change the point-blank test.

The list holds 32 events. A new event replaces the quietest only when its loudness is strictly greater. Dropping a Weapon, Impact, or Voice event logs a warning. Dropped footsteps stay quiet.

A scored Weapon, Impact, or Voice of impulse at least 1 moves a Calm guard to Suspicious, at the sound, heard-only, with the meter cleared by `enter`. The stored wait is `soundInvestigateDelaySeconds(distance, effectiveRange, loudness)`:

```
closeness = clamp(1 - distance / effectiveRange, 0, 1)
intensity = clamp(loudness * (0.35 + 0.65 * closeness), 0, 4)
delay     = lerp(6.0, 0.35, intensity / 4)
```

`effectiveRange` is the radius after the wall scale. Open air uses the authored radius. Sample waits, open air: a loudness-4 shot at 47.9 m of a 48 m radius waits about 4.02 s; at 4.1 m about 0.66 s; at the source 0.35 s; a loudness-1 shot at 24 m about 5.05 s. Loudness 4 cannot reach the 6 s end, because `4 * 0.35` is already 1.4.

That wait does not read `investigateAfter`. Sight still does. A louder or closer shot while the guard is still Suspicious stores a shorter wait only, and restarts the wait when the new delay is already less than the time spent. A worse shot does not move `noticePos`. An Investigating guard retargets to a better shot and does not rewrite the delay or drop back to Suspicious. Searching keeps the search. Noise does not fill the Suspicious meter into Alert.

Footsteps keep loudness 1 and the old fractional curve, and they still come from `tickPlayerExposure`. A footstep inside `instantRange` does not Alert.

## Point-blank Alert

A Weapon or Voice with loudness at least 2 (`kSoundAlertLoudness`) and horizontal distance strictly inside `instantRange` (default 4 m) enters Alert. The guard does not need line of sight. `seesNow` stays false, so he does not radio and does not shout. Squad mates inside the shot's authored radius are sent to heard-only Investigating. Wolves are skipped on that ranged share. Mates past the radius stay Calm.

The Alert holds while that cue is posted again, then runs `alertLoseSeconds` (default 2 s). It does not trace the player and does not use the catch timer.

`content/scenes/NPC.json` sets `instantRange` to 0. That scene does not point-blank alert on a shot. The file is left as authored. Sound suspicion still ignores that scene's 60 s `investigateAfter`.

## Share

`shareSuspicion(caller, where, range)`:

- Squad 0 does not share.
- Range 0 is the sight path: every living Calm squad mate, any distance, including the old wolf behavior.
- Range above 0 is the heard-sound path: living Calm non-wolf squad mates whose horizontal distance to the sound is within that radius. The radius is the shot's authored radius, not `alertShareRadius`, and no second ray is cast. A mate whose own wall kept the impulse under threshold still walks if he stands inside the shot.

Hearing never calls `shoutAlert`. A hit still `forceAlert`s the victim, and that shout is unchanged. An already Alert victim ignores the shot and the scream, so his notice stays on the player.

## Save

`soundInvestigateDelay`, `soundCueLoudness`, `soundCueDistance`, and `hearingAlert` are runtime fields on `AiAgentComponent`. They are not written. `kSaveVersion` stays 1. Respawn and a return to Calm zero them. Leaving Alert clears `hearingAlert` with `spotCalled`.
