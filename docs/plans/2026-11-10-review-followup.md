# Review follow-up — 2026-11-10

Decisions against [DarkEngine6-review-14945a3.md](DarkEngine6-review-14945a3.md) (read-only review of `14945a3`, 9 Nov 2026). The review is unchanged. This note says what was already fixed, what this pass changes, and what stays deferred.

## Scope

This pass is the parity and correctness list only.

Deferred on purpose, and not started here:

- Splitting `PlayHost` into `PlayServices` / `PlayFrameInput` / `IPlayEvents`.
- A `GameData` registry, or moving items, statuses, and sound cues to JSON.
- Renderer modernization: bindless descriptors, DXC, D3D12MA, a render graph, a job pool, an asset cooker, Tracy, CI.

## Locked behavior

- Player body motion uses the Sandbox teleport. Both apps call `stepPossessedBody` (`Play/PlayBody.cpp`): push poses, apply crouch, `setBodyPose`, step, write dynamic poses. The Editor does not sweep the player with `moveKinematicTo` and does not restore the motor pose. Editor prop-pushing from the player body goes away. `moveKinematicTo` stays for other callers. Edit mode still uses `syncEditPhysics`.
- Walk bake never adds the 1 m origin cube, even when the net demo cube exists. AI can path through the origin. Obstacles are scene cubes and spheres. Volume cubes (goal, conceal, zone, pickup, non-blocking, or `|scale.y| < 0.3`) are excluded. Sphere radius is `0.5 * max(|sx|, |sy|, |sz|)`. Losing the origin cube from Sandbox exposure occluders is accepted. Physics obstacle bodies are still cubes only.

## Part A — already fixed before this pass

| Item | Status | What landed |
|---|---|---|
| P2-1 asset and GPU eviction | Fixed | `Application::releaseUnreferencedAssets` runs `collectGarbage` and `collectUnused` on Editor new/load and on Sandbox2D level clear. Sandbox 3D stays one scene, so it does not GC mid-session. Synth clip keys are FNV-1a hashes (`dialogueSynthCacheKey`), not the full line. `ClipKeep` holds PCM until XAudio2 `OnBufferEnd`. That also closes R2. |
| P2-2 movie BGRA UAV | Fixed | `movieFrameResourceFlags`: software frames use `D3D12_RESOURCE_FLAG_NONE`. Hardware frames request UAV only after `movieBgraTypedUavStoreSupported`. Covered by `UnitTests/Render/MovieFrameTextureTests.cpp`. |
| P3-1 particle `waitForGpu` | Fixed, with a leftover | `GpuParticleSystem::prepare` retires pool buffers with `deferRelease`. `waitForGpu` remains only in `destroy()`. Still open, and not scheduled: `isAccepted` is a linear scan, and the accepted/seen vectors are rebuilt each `prepare`. Emitter counts are small. |
| P3-2 previous-world map | Fixed, narrower than the review | Entity ids include a generation, so a brand-new entity does not read another entity's matrix. The real smear was a quickload of a surviving mesh id, plus TAA history. `saveBeginLoad` clears the map and invalidates animation previous-world matrices. `SceneRenderer::discardTemporalHistory` runs on that path. `dropDeadPrevWorlds()` runs once per rendered frame. The Editor has no such map. |
| P3-3 log capture and `g_logTarget` | Fixed, with an accepted leftover | `g_logTarget` is read and written under `g_logMutex`. `Log::write` copies capture functions under the log mutex and invokes them after the unlock. `removeCapture` does not wait for a callback already in progress. An epoch was not added. |
| P3-4 foliage phase text | Fixed | `m_foliagePhase` is guarded by `m_foliagePhaseMutex`. |
| P3-5 per-frame sets and asset lookups | Fixed for the hot path | Both shells use `FrameEntitySet` (index bitset) plus a member `rejectedShots` vector. Shadow and color passes use `contains()`. Render lookups use `AssetManager::find`. `retainModel` / `retainMaterial` take a `shared_ptr` only when the GPU copy is missing or packed material maps changed. `DialogueSpeech::acquire` reuses `m_requestKey` while the same request is in flight. Default cascades are 3 (`kMaxShadowCascades`), not 4. `getAs` still locks; non-render call sites still use it. `SandboxApp.cpp` has no `getAs`. `find()` is not safe if another thread unloads that id while the pointer is in use. Writers of `m_assets` are the main thread. A second owning `shared_ptr` was not added on the publish path. |

## Part A — this pass

| Item | Status | Solution |
|---|---|---|
| R5 pool mutation during `each()` | Fixed as an assert only | `ComponentPool` counts `m_iterating` across `each()` and `visit()`. `insert` and `remove` call `notePoolIterateViolation` while that count is non-zero. With the trap on (the default), Debug builds `DE_ASSERT`. Nested iteration of the same pool is fine. Mutating a different component type is fine. Deferred add/destroy queues were not added. The unit test turns the trap off so it can count violations without `__debugbreak`. A production `each` that inserts or removes the same component type will break the Debug test binary. That is the bug the guard is there to catch. |
| R6 stale async save apply | Fixed | Each async load job stores `World::generation()` when the job is queued inside the save lock, not when the request is made and not inside apply. On apply, a mismatch logs "Save: dropped load, the scene changed while it was reading", sets `SaveResult::SceneMismatch`, and does not apply the text. Net `allowLoad` rules are unchanged. The Editor bumps generation in `clearScene` and again on `setPlayMode(false)`. A double bump is fine. Generation is not bumped on play start or inside the save apply, or the stamp would never match. Sandbox has one world and never bumps, so its loads still apply. Synchronous `loadNow` / `applyPayloadText` are outside this bug. |

## Part A — deferred

| Item | Why it stays |
|---|---|
| R1 shadow worker | Stays off. `g_shadowWorker` defaults false. Tests may toggle it. Do not enable it and do not delete those tests. Turning it on still shares `DrawList`, `SkinnedInstanceBuffer`, and `FoliagePipeline` with the main thread. |
| R3 descriptor heap `Reset` | Live callers run after `waitForGpu`. The mid-session repack paths have no callers. When one of those is revived, route the old heap through `deferRelease`. |
| R4 12-bit generation ABA | After 4096 reuses of one slot, a stale `Entity` can pass `alive()`. Churn is low (projectiles are not entities). A 32/32 id or a FIFO free list with a minimum depth is a later change. |
| R7 speech cancel | Queue cancel landed in the second pass. `Speak` is still blocking, so a line already inside `Speak` finishes. `SPF_ASYNC` stays deferred. |
| R8 one job condvar | `notify_all` wakes every waiter. Fine at today's load. Unused `JobStatus` fields stay. |
| R9 log I/O under the mutex | `fputs` / `fflush` / `OutputDebugStringA` still run while the log mutex is held. A logger thread is deferred. |
| Upload ring, D3D12MA, committed-resource strategy | One allocator and one list per upload, and 64 KB minimum committed allocations, stay as they are. |
| Runtime GPU stalls besides particles | Camouflage, echo, and underwater resizes can still `waitForGpu` mid-frame. Particle pools no longer do (P3-1). |
| ECS `unordered_map` sparse set | `get` / `has` stay hash lookups. A paged sparse array is deferred. The iteration assert (R5) does not change storage. |
| Modernization table (section 4) and the suggested order (section 5) | Job pool, render graph, bindless heap, DXC, PSO library, cooker, Tracy, CI. None of this is in scope. |
| Host lifetime tokens | Hosts stay non-owning pointer structs filled each pull. No generation token on the host. |

## Part B — divergences and incidental findings

All six divergences in §3.3, and all seven incidental findings in §6, are addressed here. §6 item 3 is an intentional behavior change: the Sandbox teleport wins, so the Editor player body no longer pushes props.

| # | Issue | Solution |
|---|---|---|
| 3.3.1 / §6.2 | Spawner hunters talk only in Sandbox | `spawnListedUnit` (`Play/SpawnListed.cpp`) is the one path both apps call for a listed spawner. Hunters always get a visual, a physics callback, stock, and dialogue. Wolves skip stock and dialogue. Crows use `spawnCrow`. The Editor does not write `EditorObjectComponent` or a persistent id for these spawns. If the model fails, the Editor cube-proxies (`allowStaticModel`). Physics binds only when a `ModelComponent` exists. If `spawnHunter` fails, the entity is destroyed and the call returns null. Scene actors placed in the file still use the existing Editor attach path and `PathChase::spawnListedHunter` at Sandbox boot. `SandboxApp::bindNpcDialogue` was removed. Its only caller was the old spawn. |
| 3.3.2 / §6.3 | Player kinematic step | See "Locked behavior". Needs a manual play pass: the Editor player should no longer shove dynamic cubes and spheres. |
| 3.3.3 / §6.1 | Editor Play missing feedback | `PlayFeedback` is an app-owned pointer struct. `installPlayFeedback` sets muzzle, blood burst, blood splat, and respawn. Those callbacks receive the feedback pointer. The shared respawn turns the muzzle light off, zeros its timer, then runs an optional `afterRespawn`. Sandbox uses that for vision modes only. The Editor leaves it null. Vision modes stay Sandbox-only. The Editor creates a blood emitter and a `BloodSplatPool` at init, draws them beside the particle pass, and ticks blood and the muzzle inside the unlocked play block before `tickWorld` so a 0.05 s pulse is not decayed in the same step. The muzzle light exists only in Hybrid Deferred: created on play start, destroyed on stop. There is no camera entity. The muzzle follows `Camera3D*`. Living and death decals still come from impact decals when those are ready. The splat callback is the fallback. Needs a manual Editor play pass for blood and the muzzle flash. |
| 3.3.4 / §6.4 | Walk maps differ | `classifyWalkObstacle` (`Scene/WalkObstacles.h`) is shared. `ObstacleSphereComponent` is emitted for scene spheres. `collectWalkObstacles` walks cubes and spheres and does not add an origin cube. `bakeHunterWalk` wraps the hunter bake. The Editor still walks `EditorObjectComponent` and keeps the flat 41×41 grid when there is no terrain. `PathChase::init` copies boot obstacles and does not push the origin cube. `PathChase::bake` collects spheres from the world. It does not re-collect cubes, because boot already copied those and a second collect would duplicate them. Sandbox exposure uses the chase cube list and the chase sphere list. |
| 3.3.5 | Editor quest banners have no colour | `QuestBanner` and `makeQuestBanner` live in `Play/QuestBanner.h`. Started and QuestDone are `(240, 205, 110)`. NewObjective is `(235, 235, 235)`. ObjectiveDone is `(120, 230, 140)`. Packed as `IM_COL32` layout `0xFF000000 \| (b << 16) \| (g << 8) \| r`. Labels: NEW QUEST, NEW OBJECTIVE, OBJECTIVE COMPLETE, QUEST COMPLETE. Duration stays 3.5 s. The cap is unchanged (non-objective notes, or fewer than 4 banners). HUD drawing stays in each exe. The Editor draws with `ImGui::TextColored`. |
| 3.3.6 / §5.4 prompts | Prompts hard-code key letters | `ActionMap::keyBracket` returns a label such as `[G]` from the first Key binding (letters, digits, F1–F12, Space, Esc, Enter, Tab, Shift, Ctrl, Alt). Anything else uses the fallback. Per-app default bindings stay. Climb uses the pickup action. Talk uses `talk`. The prompt string is built at the draw site so it outlives the ImGui call. Sandbox prompts rewired: item, power, drop body, takedown, pickup body, hide, doors, climb, step out. The Editor climb prompt uses the pickup key with fallback `[E]`. Unrelated editor keys were left alone. |
| §6.5 | Hunter pounce defined twice | `hunterJumpDefFromPattern` overlays only `telegraphSeconds` (JSON windup) and `connectDamage` (JSON damage). `hunterJumpRanges` returns the JSON min/max, or 4 and 9 when no Jump move is loaded. Code-only numbers that stay: cooldown 3.5, ground offset 1, connect damage 22, pound damage 14, leap vertical 10, leap forward max 12, gravity 24, homing 1.5, knockdown duration 0.8, knockdown force 1. JSON `stunSeconds` (0.85) does not replace knockdown duration. JSON poise (16) does not replace connect poise. `attachHunter` loads attack patterns before `setDef`. A missing Jump move logs once and keeps the code numbers. `applyNpcMovementFile` may still overwrite gravity and leap afterward. Telegraph and damage survive that. |
| §6.6 | Dead Sandbox aliases and CMake | Removed `Sandbox/CMakeLists.txt`. It was not `add_subdirectory`'d and listed only three sources. Removed 16 `SandboxApp` reference members that only aliased `PlayRuntime` and were unused outside their initializer: hit-stop, shake, shake time, swing charged, swing lock, combo window, combo step, combo count, face yaw, face left, block recoil, shield bump, player wet, player submerged, swim dive edge, swim stand edge. The other 23 play references stayed after this pass. The second pass removed them. See below. |
| §6.7 | Defense timer decremented in draw | Both apps store `m_defenseParry`, set from `onPlayDefenseText` (`"PARRY!"` true, `"BLOCKED"` false). Sandbox ticks `m_defenseTextLeft` in the gameplay update beside the quest timer, using scaled gameplay dt, so it pauses with the game and during dialogue lock. The Editor already ticked in `EditorUi` and still does, including while dialogue is up. Draw no longer inspects the first character. Stop clears the flag. |

## Part B — recommendations

| # | Recommendation | Status |
|---|---|---|
| 1 | Split `PlayHost` | Deferred. Hooks are a `PlayFeedback` pointer struct on the existing host. Callbacks stay function pointers. No virtual `IPlayEvents`. |
| 2 | One `PlayActors` factory, delete every hunter attach | Partial. Listed spawner units go through `spawnListedUnit`. `PathChase::spawnListedHunter` stays for Sandbox boot scene pawns. Editor scene hunters and wolves still use `attachEditorHunter` / `attachEditorWolf`. |
| 3 | `ScenePlaySetup` | Deferred. |
| 4 | `GameData`, items, statuses, cues as JSON | Deferred. |
| 5 | `D3D12Util` and `FullscreenPass` | Deferred. |
| 6 | `PlaySaveHost` and a full `PlayHud` | Partial. Banners, prompt labels, and the defense timer moved as described above. Save hosts stay per app. Upgrade and loot panels stayed in the Sandbox exe. The second pass moved them into `SandboxHud`. They are still not a shared `PlayHud`. |
| 7 | `render.json` and `user/settings.json` | Deferred. |
| 8 | Module include cycle, `Frustum3f` to Math, loader registry | Deferred. |
| 9 | `FrameComposer` | Deferred. Each app still submits its own frame. |
| 10 | One JSON helper, schemas, content validation test | Deferred. |
| 11 | `NetSession`, shared argument parser | Deferred. |
| 12 | Vision modes in the Editor | Deferred. The Editor still fills camouflage as false. Sandbox respawn still resets its own vision flags through `afterRespawn`. |
| 13 | `Ui/` as a lib, drop `imgui_demo`, dead Sandbox cmake and aliases | Partial. The dead file and the 16 aliases are gone. `Ui/` is not its own library. `imgui_demo.cpp` stays. |
| 14 | `ContentWatcher` hot reload | Deferred. |
| 15 | Templatize 2D/3D math pairs | Deferred. |

Also deferred from §5, and called out so they are not mistaken for leftovers of this pass: the demo `level.json` filename gate, Sandbox terrain layer names, the UI palette / `IM_COL32` literals (banner colours moved with the quest banner only), and the rest of the hard-coded tuning tables.

## Tests added for this pass

- `UnitTests/Scene/WalkObstacleTests.cpp` — spheres included, origin cube absent, thin and volume cubes skipped, absolute scale.
- `UnitTests/Gameplay/QuestBannerTests.cpp` — labels and packed colours.
- `UnitTests/AI/HunterJumpTuneTests.cpp` — JSON windup and damage overlay; missing pattern keeps the code numbers.
- `UnitTests/Save/SaveGenerationTests.cpp` — async load dropped on generation mismatch, and a matching generation still applies.
- `UnitTests/ECS/WorldTests.cpp` — `World.EachNotesAMutationAndNestedReadsAreFine`, `World.SceneGenerationStartsAtOneAndBumps`.

## Manual play pass

No in-game check was run with this change. Two things need a person in Editor Play:

- The player body teleports. It should no longer push dynamic props. The visual pose is not restored after the physics step.
- Blood burst, blood splat, and the muzzle flash should show on hit and on fire in Hybrid Deferred, and the muzzle light should go out on respawn and on Stop.

## Second pass — Sandbox god files and queued speech

Sandbox and Editor were confirmed working after the parity pass. This pass does not change play rules. It splits `SandboxApp` and drops speech jobs that have not started `Speak`.

| What | Where it lives now |
|---|---|
| The 23 `PlayRuntime` reference aliases still named in §6.6 | Removed. Call sites use `m_play.runtime()`, `m_play.quest()`, and `m_play.targets()`. |
| `netRoleName` and `pawnPaletteColor` | Once, in `Network/NetTypes.h`. Sandbox and Sandbox2D call them. Pawn spawn stays in each app, because the 2D and 3D prefabs differ. |
| Queued speech | `SpeechSynth::cancel` removes a job that is still in the queue, under the existing mutex. `DialogueSpeech::acquire` cancels the previous id when the line changes, when a file clip is ready, and when the 4 second fallback gives up. A line already inside `Speak` still finishes. `stop()` is unchanged. No `SPF_ASYNC`. |
| Demo content | `SandboxDemo`: origin cube, glTF samples, wiggle actor, skeleton lines, hybrid local lights, and the health packs placed at boot. |
| 3D frame | `SandboxFrame`: the old `onRender` body, projectile draws, debug overlays, the draw list, skinned instances, `SceneRenderer`, the accepted-entity sets, rejected shots, and the previous-world map. `SandboxApp::onRender` calls `render()`. The Editor frame was not touched. |
| Dev HUD | `SandboxHud`: the methods that were declared on `SandboxApp` and defined in `DevToolsPanel.cpp`, plus quest banners, defense text, the journal, the loot target, the upgrade panel, ImGui, and the main menu. `DevToolsPanel.cpp` is gone. |
| Vision flags | `SandboxApp::SandboxVision`, still filled into `PlayHost` from Sandbox only. The Editor still hard-codes camouflage false. |

`onUpdate` stays the Sandbox schedule. `tickLeaves` was not merged with the Editor.

Still deferred, and not started here: `FrameComposer`, the `PlayHost` split, `ScenePlaySetup`, `GameData`, Editor vision modes, `D3D12Util`, and renderer modernization (bindless descriptors, DXC, D3D12MA, a render graph, a job pool, an asset cooker, Tracy, CI). Also still deferred: resize and particle-capacity `waitForGpu`, the shadow worker (R1), descriptor `Reset` (R3), generation width (R4), the job condvar (R8), log I/O under the mutex (R9), `SPF_ASYNC` speech, `Ui/` as a library, and `imgui_demo.cpp`.

No in-game check was run for this split. Sandbox boot, play, the dev HUD, and one rendered frame still need a look. The move is the same calls in new types.
