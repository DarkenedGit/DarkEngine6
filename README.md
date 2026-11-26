# DarkEngine6

A home C++ game engine, the sixth version of DarkEngine. Windows only for now: **C++23**, Direct3D 12, XAudio2, XInput.

This engine is still in progress. This README covers only what is in the tree today. Design notes in the tree (`*/DESIGN-*.md`) each give their own status (Draft, Proposed, Approved, Accepted, Implemented). A design doc does not mean the feature has shipped.

## Targets

| Target | Kind | Role |
|--------|------|------|
| `DarkFoundation` | static lib | `Math`, `Collision`, `ECS`, foundation `Core` (`Log`, `Paths`, `ContentRoots`, `UUID`) |
| `DarkAssets` | static lib | `Assets/*` (WIC image decode, glTF via cgltf) |
| `DarkRender` | static lib | `Render/*` + D3D12 (Agility SDK headers) / DXGI / DXC (loaded at runtime) / `d3dcompiler` (FXC fallback only); PIX + NVTX markers when `DE_ENABLE_GPU_MARKERS` is on |
| `DarkNet` | static lib | `Network/*` (`ws2_32`) |
| `DarkGameplay` | static lib | `Weapons/*`, `Gameplay/*`, `Combat/*`, `Game/*` (the game's render code: echolocation) |
| `DarkEngine` | static lib | Umbrella: `AI`, `Animation`, `Audio`, `Character`, `Debug`, `Input`, `Media`, `Particles`, `Physics`, `Play`, `Save`, `Scene`, `Sky`, `Sprite`, `Terrain`, `Water`, the content database (`content/Content*.cpp`, top-level only), plus `Application` / `Window` / `MemoryTracker` and `Ui/MainMenu`. PUBLIC-links every layer; PRIVATE-links Box3D |
| `Sandbox` | exe | 3D sample game (ImGui Dev Tools) |
| `Sandbox2D` | exe | Side-scrolling 2D sample (Box2D) |
| `Editor` | exe | ImGui editor (Win32 + DX12) with Play-in-editor |
| `VisualDebugger` | exe | Connects to a running host over TCP for performance and debug data |
| `UnitTests` | exe | GoogleTest suite (built when `DE_BUILD_TESTS` is on) |

Hosts link only `DarkEngine`. Each engine `.cpp` compiles in exactly one target. Every `Render/*.cpp` compiles into `DarkRender`; the loading screen and the movie presenters, which call the movie player, live in `Media/` and compile into `DarkEngine`. The layer graph is documented at the top of `cmake/DarkEngineTargets.cmake`.

HLSL lives in `content/shaders/`. Debug builds, the Editor and UnitTests compile it at runtime with DXC; Release game exes load DXIL baked at build time (see [Shaders](#shaders)).

## What is in the tree

- **Core**: `Application`, Win32 `Window`, logging (`DE_LOG_*` + `LogCategory`: `Core`, `Render`, `Audio`, `Collision`, `AI`, `Input`, `Networking`, `Debug`, `Media`), paths and content roots, UUID, memory tracker, and type-checked JSON readers (`Core/Json.h`: `jsonStringOr` / `jsonBoolOr` / `jsonIntOr` / `jsonFloatOr` / `jsonDoubleOr`). These log and fall back on a wrong type instead of calling nlohmann `value()`, which aborts when exceptions are off. Configure generates `Core/Version.h` (git describe / commit when available). `Application::releaseUnreferencedAssets()` runs garbage collection between levels (Editor scene new/load, Sandbox2D level clear), calling `AssetManager::collectGarbage()` and `GpuResourceCache::collectUnused()` to free unreferenced assets and GPU resources.
- **ECS**: `World` with generation-packed `EntityID` (slot + generation; `NULL_ENTITY` is 0), O(1) `alive()`, safe emplace over existing entities.
- **Render**: a D3D12 renderer with one 3D path: deferred (G-buffer, fullscreen lighting, then forward translucents into HDR). The forward opaque path was removed (render graph plan D2). 3D hosts call `Application::createSceneTargets()` and quit with an error if it fails. 2D hosts (Sandbox2D, the Editor 2D view, the splash, VisualDebugger) draw straight to the swap chain. It uses reverse-Z depth (`GREATER` compare). `SceneRenderer` is shared by Sandbox and Editor. Passes and systems in `Render/`:
  - Cascaded directional shadows (up to 3 cascades, `ShadowSystem`) and local-light shadows (`LocalShadowSystem`). Static and model shadow casters are culled against each shadow view's frustum.
  - Local lights drawn as light volumes (`LocalLightVolumePipeline`)
  - Deferred decals as oriented boxes (`DecalPipeline`, `DecalPool`)
  - GTAO, SSR (half-res, see `Render/DESIGN-reflections.md`), fog, IBL bake (`IblBake`, `GpuIbl`)
  - Sky with a distant cloud layer (`Sky/CloudLayer`, `content/shaders/CloudLayer.hlsli`, see `Render/DESIGN-cloud-layer.md`), volumetric clouds (`Sky/CloudVolumeFeature`; the cloud volume in the default scene is disabled), water (`Water/WaterPipeline`, drawn through the host-owned `Water/WaterRendering`)
  - Terrain, terrain foliage instancing with alpha-cutout shadows (`FoliagePipeline`), GPU terrain erosion bake (`TerrainErosionPipeline`)
  - Procedural grass (`GrassPipeline`): blade LODs, per-tile Perlin wind, and bending around the player (see Terrain below and `Terrain/DESIGN-procedural-grass.md`)
  - Static and skinned meshes, sprites, lines, particles, camouflage
  - Post: auto exposure, bloom, TAA, motion blur, tonemap. Sandbox echolocation (B) is a deferred chirp drawn in the post group before bloom, not its own pass. One 1/8-resolution snapshot per pulse (about 0.8 Hz) is held world-locked until the next pulse. The shell expands at 50 m/s. Both live in `content/render/echolocation.json`; Dev Tools can change them for the session. The picture is one hue on black. A hard near ping's leading edge can bloom; the held return stays under the threshold of 1.0. It is exclusive with infrared and ultraviolet, holds auto-exposure, resets TAA, and skips motion blur.
  - Underwater view (`Water/UnderwaterPipeline`, owned by `Water/WaterRendering`, `content/shaders/Underwater.hlsl`): when the camera goes below the water level, a fullscreen pass copies HDR and fogs the scene by depth. The lake is then drawn again with front faces culled, so you see its surface from below as a ceiling. Its pass sits after particles and before post.
  - Loading screen / boot splash (image and movie playback), HUD (crosshair, health), debug overlay (G-buffer tiles)
- **Draw submission** (`DrawList`, `RenderItem`, `InstancedMeshPipeline`, `SkinnedInstanceBuffer`, `InstanceCull`):
  - Static opaque meshes and models are batched and drawn instanced. Batches sort by population, mesh, alpha mask, then material (`BatchKey`).
  - Batches can be drawn with `ExecuteIndirect`. A compute pass (`InstanceCull.hlsl`) frustum-culls static instances per view (camera and shadow views) and writes the indirect arguments.
  - Skinned characters draw instanced: bone palettes are uploaded per frame and skinning runs in the instanced vertex shaders (`SkinnedInstance.hlsli`). Skinned parts have their own GPU cull (`SkinnedInstanceCull`).
  - Terrain foliage is culled on the GPU (`FoliageCull.hlsl`). Grass is culled per 8 m tile on the CPU.
  - Dev Tools and the Editor View menu have **Instanced static meshes** and **Indirect static draws** toggles (both on by default). Wireframe and point fill stay on the immediate path.
- **Render graph**: `Render/RenderGraph*` is the CPU core: pass setup/execute, versioned handles, culling, ordering, async-compute queue assignment and sync points, enhanced-barrier records, history pairs and a transient alias plan. The graph drives the 3D frame (`Render/FrameComposer.*`, render-graph plan Phases 4–6). D3D12MA 3.3.0 backs persistent targets and transient aliasing (`Render/GpuAllocator`, `Render/RenderTargetPool`, `Render/GraphTargets`). Enhanced barriers only: every resource barrier uses `ID3D12GraphicsCommandList7::Barrier` through `Render/EnhancedBarrierEmitter` with a record-order `ResourceStateTracker`. Passes declare their resource uses and bind descriptor-ring tables (`Render/DescriptorRing`, `Render/RgViews`); the graph plans barriers between declared passes. Post (auto exposure, bloom, TAA, motion blur, tonemap), GTAO, SSR, lighting, local volumes, G-buffer, decals, sky, water, clouds, and underwater are declared passes. Shadows, camouflage, translucent, particles, and overlays remain opaque (Phase 6 parts 1–7 complete; next: more declared passes, then async compute in Phase 10). Tests: `UnitTests/Render/RenderGraph*Tests.cpp`.
- **Pass order**: `Render/FrameFeatures.*` holds one feature list per host (Sandbox, Editor); each host sets a pass callback per `PassId`, and the graph runs them in list order. The compiled order is checked against the list and a mismatch logs once and invalidates the baseline. The order is: shadows, local shadows, G-buffer, decals, GTAO, SSR, deferred lighting, local light volumes, sky, SSR capture, water, clouds, camouflage (Sandbox) or editor grid, light gizmos and debug overlays (Editor), translucent, debug overlays (Sandbox), particles, underwater, post, overlay.
- **Assets**: `AssetManager` / handles, texture cache, glTF load via cgltf (`GltfLoader`), animated glTF.
- **Animation**: skeletons, clips, sampler, anim graph (`*.anim.json`), locomotion, anim notifies. Locomotion clip rate follows planar speed. Procedural layers add a directional hit flinch and a shield guard pose.
- **AI**: hierarchical state machines loaded from JSON (`content/ai/*.hsm.json`), attack patterns, pathfinding, walkability, sight, wolf approach / flanking, water watch (`AI/WaterPeek`). An awareness meter (`AI/Awareness`) moves enemies through Calm, Suspicious, Investigating, Alert, and Searching from sight, motion, noise, and proximity. A tagged world cue reports itself when it plays (`Audio/SoundStimulus.h`, `AI/DESIGN-sound-stimuli.md`). Which cues alert, and how far, is `content/ai/sound-stimuli.json`: weapon fire, impacts, and voices wake nearby guards. A point-blank gunshot or scream goes straight to Alert and does not shout. Other heard shots wait less when they are louder or closer, and only squad mates inside that shot's radius join the search. Footsteps stay on the exposure stride. `content/scenes/NPC.json` sets `instantRange` to 0, so that scene does not point-blank alert on a shot. Per-type movement speeds come from `content/entities/*.entity.movement.json`.
- **Media**: `MoviePlayer` decodes H.264 and MPEG-2 video (Media Foundation) into an XAudio2 voice and a GPU texture, with NV12-to-RGB conversion on upload. Movies are authored on scene nodes and played by the loading screen (`MoviePresentPipeline`). See `Media/MoviePlayer.h`.
- **Play** (`Play/`): code shared by Sandbox play and Editor Play-in-editor: character attach for players and NPCs, character sound banks, play look and shoulder camera, motor input (planar slide along walls instead of stopping), physics ground height, terrain streaming, impact decals, weapon target queries, the player flashlight, crows, spawners, and facility hooks (doors, guards). `PlaySession` holds one `PlayRuntime` for look, offhand, combat timers, throw flights, and the quest runtime. It also owns the guard voice, the crow flock, `PlayFeedback` (muzzle timer, blood burst, and splat pool), the shield, muzzle, camera, and session entities, the scene quest id, and the building fixture lists (containers, hides, doors, cameras, labels). Both apps read the runtime through `runtime()`, `quest()`, and `targets()`, and the owned objects through the accessors on `PlaySession`. Each app keeps one `PlaySession`. The possessed body steps through `stepPossessedBody` (`Play/PlayBody.cpp`). Listed spawner units go through `spawnListedUnit` (`Play/SpawnListed.cpp`). Quest banner text and colours are `Play/QuestBanner.h`. Walk obstacles are `Scene/WalkObstacles.h`.
  - **Spawners**: placed level objects that spawn units (up to 16 slots). `unit` is an archetype id. `hunter`, `wolf`, and `crow` (a bare name or a `core:` id) use the built-in spawn path. Any other id is spawned from its archetype. `Maintain` replaces dead units; `Waves` starts the next wave on a timer or when the current wave is dead. Spawned units are not saved; Stop Play removes them.
  - **Crows**: flock, perch in trees, forage on the ground, and flee from living hunters and the player with an alarm call. Dead crows fall to the ground.
- **Physics**: Box3D wrapper (`third_party/box3d`, v0.1.0). Collision shapes are cooked from primitives, model `_col*` parts, or `*.collision.json` sidecars. Per-model `*.physics.json` settings. Surfaces load from `content/physics/surfaces.json`. Foliage trunks and rocks block the player. See `Physics/README.md` and `Collision/DESIGN-box3d.md`.
- **Collision**: collision query code in `Collision/` (separate from the Box3D wrapper in `Physics/`).
- **Character**: player motor (walk, sprint, crouch, swim, jump, double jump, dodge, wall climb, ledge hang and climb-up, wall jump), health, hit reactions with directional knockback, shield, stealth, and six skills (`Shoot`, `Swim`, `Run`, `Jump`, `Hear`, `See`) with use-based XP (`content/skills/*.json`, `Character/DESIGN-skills.md`). Ground movement accelerates between stop, walk, and run; wall climbing and hanging use a stamina pool. Speeds, acceleration, gravity, and jump values load from `content/entities/<type>.entity.movement.json`.
- **Combat / Gameplay / Weapons**: `CombatSystem` (damage resolve, status effects, DoT ticks), status catalog (`Poison`, `Bleed`, `Ignite`, `Chill`, `Shock`, `Stun`, plus crowd-control rows), jump attacks, charged attacks, knockdown. The compiled catalog is the set of status ids. `content/combat/statuses.json` overlays name, duration, magnitude, tick damage, tick interval, and max stacks. A name that is not in the catalog is logged and does not add an effect. Armor pieces (`Combat/ArmorPieces`) absorb hits and break off. `PlayerModeComponent` adds God and Reaper dev switches (both off by default, not saved). See `Combat/DESIGN-status-effects.md`.
  - **Inventory** (`Gameplay/Inventory`, `Gameplay/Item`): a 20-slot bag plus Head, Chest, Arms, Legs, and Light equip slots. The catalog is `content/items/*.items.json` (sword, rifle, medkit, armor, keycard, flashlight, quest items, resources). `type` and `slot` are strings. If no item file is found, the compiled table is used. A new game grants `content/player/loadout.json` (sword, rifle, two medkits). Weapon switching needs the weapon in the bag, and the flashlight works while it is equipped. Items on the ground show a label and are picked up with G.
  - **Loot**: dead hunters drop a loot bag with their items and armor. G opens the nearest bag so you can take items.
  - **Throwables** (`Gameplay/ThrowableCatalog`, `Gameplay/ThrowableBelt`, `Weapons/ThrowableFlight`, `Character/ThrowableView`): throwable types come from `content/items/throwables.json`, which sets weight, throw distance, impact sound, call distance, and model. The shipped types are `rock` and `bottle`. Picked-up throwables go onto a belt that keeps a count per type, selected as weapon 3. You can throw while in the air or charge a throw by holding before release. A throw flies on an arc and lands as a pickup you can grab again. The model shows in the hand, in flight, and on the ground. When one lands, nearby NPCs within its call distance come to search the spot. Belt counts and throws still in flight are saved with the loadout (`Save/PersistCombat.cpp`).
  - **Quests** (`Gameplay/Quest`): data-driven quests in `content/quests/<id>.quest.json`, with `reach` (scene zone), `collect` (item and count), and `event` objectives (`power_off`, `takedown`, `body_hidden`), which can run in sequence, plus rewards. Sandbox shows banners for a new quest, a new objective, and completion, and opens a journal with J. Scenes can place quest zones and pickups (`zone`, `pickup`, `pickupCount` fields). The sample quest is `north_lab`, played in `stealth_test.json`. Quest progress is not saved yet.
- **Terrain**: height and splat maps, LOD, `TerrainGrid` streaming ring, procedural Generate with CPU erosion and GPU erosion, World Engine / World Creator map import (`Terrain/WorldEngineMap`, sample under `content/terrain/HurricaneRidge`), and foliage spawned from splat weights and saved per tile (DEFL sidecar). See `Terrain/DESIGN-*.md`.
  - **Procedural grass** (`Terrain/GrassField`, `GrassMesh`, `GrassWind`, `GrassBend`): blade tiles spawned around a point, four blade LODs, per-tile Perlin wind, and blades that bend around the player. Parameters (height, flexibility, density, wind, seed) are saved in the scene's `terrain.grass` block and can be changed in Sandbox Dev Tools under **Procedural grass**.
  - **LOD distances**: `content/terrain/lod.json` holds the terrain chunk LOD multiples and the grass LOD band distances (promote / demote, create / evict radius, fade). If the file is missing, built-in values are used and a warning is logged.
- **Water**: water planes, multiple finite lakes (`WaterBody`), swells, and streams (`WaterStream`: ribbon meshes along a path; the Editor can trace one downhill to the lake). `WaterWorld` stores the ribbons (`addRibbon`, cap 16). Sandbox boot adds one ribbon per slice until that cap. The Editor keeps its own list for tracing and editing streams. Streams are visual only; you cannot swim in them. The player can swim in lakes, and going under the surface switches to the underwater view (see Render). `Water/DESIGN-water-bodies.md` has its own status line.
- **Scene**: JSON scene files under `content/scenes/` (`level.json`, `level2d.json`, `parkour_test.json`, `stealth_test.json`) that hold entities, terrain, grass, atmosphere, water, spawners, movies, buildings (with doors, guards, props), and exposure settings. Readers accept version 1, 2, and 3. The Editor writes version 3. Each object has an `archetype` id. See [Content](#content). Each app owns one `SceneWorld` (`Scene/SceneWorld.h`): sky, image-based lighting, ambient occlusion, screen-space reflections, stealth, terrain, ground, foliage, grass, leaves, wind, physics, and water. Sandbox's view camera is `SceneWorld::camera`. The Editor edit camera stays on `EditorApp`.
- **Save**: quick and manual saves through `SaveSystem` with atomic file writes. Saves go to `%LOCALAPPDATA%\DarkEngine6\Saves`. The root lists the enabled content packages (id and version). Load warns when a listed package is not loaded. Spawned-entity archetype ids are full strings. The payload hash does not cover the package list. See `Save/DESIGN-save-load.md`.
- **Audio**: XAudio2, WAV clips, sound emitters, PCM streaming (used by `MoviePlayer`). A spatial cue copies its stimulus onto the play. The stimulus rows are `content/ai/sound-stimuli.json`. `AudioSystem::play` reports that stimulus before the mixer allocates a voice, so a mute, a full mixer, occlusion, or a dead device still reaches the AI. UI, music, loops, `play2D`, and Editor preview do not report. `SpeechSynth` turns a line into PCM on a worker thread. `cancel` drops a job that is still queued. A `Speak` already in progress finishes. `DialogueSpeech` cancels the previous id when the line changes, when a file clip is ready, and when the 4 second fallback gives up. Spoken lines stay untagged.
- **Input**: keyboard, mouse, XInput gamepad, `ActionMap` bindings. Bindings load from JSON (see [Input files](#input-and-config-files)).
- **Network**: UDP sockets, packets, reliability, replication, `NetworkSystem`, a fake transport for tests. Protocol version is **2**. The default game port is **26160** and UDP beacon discovery uses **26161**. Connect carries a hash of the enabled archetypes. A different hash is refused (`ConnectRejectReason::Content`). Hash 0 on both sides still connects. A spawn carries a session archetype index. Index 0 is the legacy `NetPrefab` and does not replicate extra component fields. `NetworkSystem` does not draw. Hosts attach `MeshComponent` (Editor also adds `EditorObjectComponent`) in `onNetSpawn` (see `Network/Replication.h`). `netRoleName` and `pawnPaletteColor` live once in `Network/NetTypes.h`. Sandbox and Sandbox2D call them. Each app still spawns its own pawns.
- **Debug**: `DebugServer` / `DebugClient` over TCP (default port **26162**) and `PerfCounters` (per-slot frame timing plus a 240-frame history) for VisualDebugger.
- **Editor**: scene editing on ECS, translate gizmo, asset browser (F10), drag-and-drop placement, terrain Generate and World Engine import, foliage density panel, water bodies and a **Water** window (add lakes, swells, streams), spawner placement, cloud layer settings, local-light authoring, physics inspector, animation / HSM / particle editors, audio preview panel, network host/join, and Play-in-editor (F12). The Create menu and the asset palette list archetypes that are not one of the legacy scene types. Panels → Content lists loaded packages and load errors. While the Editor is not in Play it reloads changed content about once a second. Placed entities stay as they are until the scene is loaded again. Procedural grass and water draw in edit mode as well as in Play.
- **Particles**: GPU particle system (`Particles/GpuParticleSystem`) with compute-based integration, CPU fallback integrator when GPU particles are disabled (toggled via Dev Tools or Editor View menu), and billboard rendering. Emitters are ECS components. The CPU integrator runs when `GpuParticleSystem::applyIntegratorFallback` is called with `gpuParticlesEnabled = false`, allowing particles to continue updating even when the GPU compute path is turned off.
- **UI**: Dear ImGui **v1.91.8-docking** (fetched at configure time) with optional FreeType font rendering, built as static lib `imgui_editor` linked by Sandbox, Editor, and VisualDebugger. Shared `Ui/` styles and components for ImGui hosts. `Ui/MainMenu` is a scene picker compiled into `DarkEngine` that provides both a CPU menu (keyboard, mouse, gamepad navigation) and an optional GPU sprite overlay (`SpritePipeline` with bitmap font). The main menu appears in Sandbox on boot (unless `-host` / `-join` / `-no-menu` / `DE_NO_MENU` skips it) and can be reopened with Esc. Menu sounds (`content/audio/UI/ui_move.wav`, `ui_select.wav`) are loaded at init and play on navigation and selection.

## Content

Archetypes, items, statuses, the starting loadout, and mods load at startup from `content/` and from mod folders. `Application` builds the content database, reloads the item and status catalogs, and stores the content hash on `NetworkSystem`. The Editor reloads the same set when a watched file changes and Play is off.

### Packages

`content/package.json` is the `core` package. Its id is forced to `core` (version `6.0.0`, engine `>=6.0`). A mod is a folder with its own `package.json`:

| Folder | Role |
|--------|------|
| `<exe>/mods/<id>/` | Shipped or loose mods next to the executable |
| `<cwd>/mods/<id>/` | Mods next to the working directory |
| `%LOCALAPPDATA%/DarkEngine6/mods/<id>/` | Local mods. This folder is scanned last, so a repeated package id here replaces an earlier copy |

`depends` and `loadAfter` set load order. A cycle is logged and that package is left out. A later package replaces a whole archetype record that uses the same id. The log names the package that won.

`mods.json` is `{ "enable": ["some_mod"] }`. The first file found beside the exe, beside the working directory, or in `%LOCALAPPDATA%/DarkEngine6/` is the filter. `core` stays enabled. With no file, every discovered package loads.

`engine` may be empty, or `>=` with a major of 6 or less. `>=7` is rejected. Any other text is logged and the package still loads.

Asset lookup checks enabled mod roots first, later packages first, then the usual content roots. The exe-adjacent `content/` is still the first of those roots and is replaced on every build. Scene and terrain saves go to the repo `content/` through `authoringContentRoot()`.

### Archetypes

Files are `content/archetypes/*.archetype.json`. An id with no package prefix is stored as `core:<id>`.

```json
{
  "id": "core:wolf",
  "version": 1,
  "base": "",
  "editor": { "label": "Wolf", "category": "Creatures", "icon": "bug", "hotkey": "", "modes": ["3d"], "matchModel": "wolf" },
  "components": {
    "Model": { "gltf": "models/wolf.gltf", "anim": "models/wolf.anim.json" },
    "Physics": { "file": "models/wolf.physics.json" },
    "Combat": { "attacks": "ai/wolf.attacks.json" },
    "Health": { "max": 60 }
  },
  "behaviors": [ { "name": "ai.wolf" } ],
  "net": { "replicate": true }
}
```

Registered components: `Model` (`gltf`, `anim`), `Physics` (`file`), `Combat` (`attacks`), `Health` (`max`), `Dialogue` (`graph`). `Mesh`, `LocalLight`, `ParticleEmitter`, `CloudVolume`, `Stream`, `Spawner`, `Movie`, `Pickup`, and `Tag` are accepted and stored. They do not load GPU assets at parse time. An unknown component disables that archetype.

Behaviors run once at spawn. They attach components. They are not tick systems.

| Behavior | What it attaches |
|----------|------------------|
| `prop.solid` | A mesh for a solid prop |
| `model` | The archetype model |
| `ai.animal` | A wolf-kind body on the existing entity, from `Model` |
| `ai.wolf` | Same body path, wolf kind, health from `Health` or 60 |
| `ai.hunter` | Hunter kind, health from `Health` or 100 |
| `player.controller` | The local player rig, health from `Health` or 200 |
| `spawner` | `SpawnerComponent`. Params `unit` is the archetype id to spawn |
| `light.searchlight`, `pickup` | Accepted. Spawn does not add extra components |

`base` copies the parent's components and behaviors, then the child's same-named entries replace them. A missing base or a cycle disables the child. Empty `editor` fields (icon, hotkey, category, modes, `matchModel`) are copied from the base.

The 19 scene types and crow have explicit archetype files. `content/archetypes/legacy_aliases.json` maps old names (`enemy` → `core:hunter`, `gltf` → `core:model`, `emitter` / `particle` → `core:particle_emitter`, and the light and cloud aliases). `content/entities/*.entity.json` still loads. A type that has no archetype file yet becomes one. Types outside the legacy set get `ai.animal`. A file that already has an archetype is left alone. `*.entity.combat.json` is not an entity master.

Editor and Sandbox still spawn those legacy types through the existing place and boot paths. A scene object whose archetype is not one of them is spawned with `instantiateArchetype` (`Content/ArchetypeSpawn.h`). Sandbox2D keeps the legacy 2D types.

`editor.icon` uses a Font Awesome name already in the UI atlas (`cube`, `circle`, `bolt`, `bug`, `cloud`, `lightbulb`, `layer-group`, `forward`, and a few aliases such as `sun` and `water`). `editor.hotkey` is one character (`9`, `a`). It selects that place brush. Keys 1–8 stay the legacy place types. `matchModel` is the glTF stem the asset browser uses when a drop is not a legacy pawn. The Create menu and the palette show archetypes that are not the legacy types, filtered by `modes` (`3d` or `2d`). An empty `modes` list is shown in both. Panels → Content lists packages and load errors.

### Scenes

Each saved object has `archetype` (`core:cube`, `core:hunter`, a mod id, …). `overrides` is an optional object. Its keys replace that archetype's component params at spawn. An unknown type or an unknown archetype is kept, drawn as a missing placeholder, and written back unchanged. Buildings write `core:cube` for boxes and pickups and `core:hunter` for guards.

A spawner's `unit` string is the archetype id. Known unit names stay short in the file (`hunter`, `wolf`, `crow`). Any other id is written as authored.

### Items, statuses, upgrades, loadout

| File | Role |
|------|------|
| `content/items/*.items.json` | Item catalog. `type` is `weapon`, `consumable`, `armor`, `key`, `tool`, `quest`, or `resource`. `slot` is `none`, `head`, `chest`, `arms`, `legs`, or `light`. A later file with the same id replaces that row |
| `content/items/throwables.json` | Throwable belt (`rock`, `bottle`). Separate from the item catalog |
| `content/player/loadout.json` | New-game grants. The shipped file is sword 1, rifle 1, medkit 2 |
| `content/combat/statuses.json` | Tunes the compiled status rows. It does not add status effects |
| `content/upgrades.json` | Body upgrades. Each row may use `effect` plus `value`, or a `mods` array of `{ "stat", "op", "value" }` (`op` is `mul` or `add`). `body.health` adds, `move.speed` multiplies. The shipped rows use `effect`: `breath`, `quiet`, `health`, `speed`, `exposure` |

JSON under `content/` is UTF-8 without a BOM.

### Network and saves

The connect packet carries `contentSetHash()`, a hash of the enabled archetypes (name, version, components, behaviors, replicate flag). Peers must match. A spawn's `archetypeIndex` is the session's 1-based list of replicated archetypes. Index 0 means the legacy prefab only.

A save root has `packages`: `{ "id", "version" }` for each enabled package. Load warns when one is missing. A spawned entity whose archetype is not loaded is skipped and logged. The integrity hash covers the payload only.

## Requirements

What the build files require:

- Windows (Win32, D3D12, XAudio2, XInput, Winsock)
- To run: **Windows 11** (build 22000+), a GPU with D3D12 feature level 12_1, Shader Model 6.6, Resource Binding Tier 3 and enhanced barriers (minimum **NVIDIA GeForce RTX 2080** or equivalent), and NVIDIA driver **572.16+**. Every host checks this at startup and exits with a message box naming the failed check (`Render/StartupGate.*`, plan §3.15).
- CMake **3.22+**
- An MSVC toolchain with **C++23** support. Flags include `/W4 /WX /permissive-`, so warnings fail the build.
- Git (Dear ImGui and GoogleTest are fetched with `FetchContent` on first configure)
- Network access on first configure. CMake downloads the Agility SDK and DXC NuGet packages, the D3D12 Memory Allocator source archive, plus WinPixEventRuntime when `DE_ENABLE_GPU_MARKERS` is on (default). Each download is SHA-256 checked.

The repo does not pin a Visual Studio or Windows SDK version.

## Build

There are no CMake presets. Configure with the default generator (or double-click `BuildProjectFiles.bat`, which runs the same command):

```bat
cmake -S . -B ./build
```

Build everything (Debug):

```bat
cmake --build build --config Debug
```

Executables land in `build/bin/<Config>/`. On each build, `content/` is copied next to `Sandbox.exe`, `Sandbox2D.exe`, `Editor.exe`, and `UnitTests.exe`. `WinPixEventRuntime.dll` is copied next to every executable when GPU markers are on. Every executable also gets the Agility runtime in `D3D12/` (`D3D12Core.dll`, plus `d3d12SDKLayers.dll` in Debug). Exes that compile shaders at runtime get `dxc/` (`dxcompiler.dll`, `dxil.dll`); game exes get the baked shaders in `shaders/`. The per-exe setup is `de_host_exe` in `cmake/HostExe.cmake`.

### CMake options

| Option | Default | Effect |
|--------|---------|--------|
| `DE_ENABLE_ASSERTS` | `ON` | Enables `DE_ASSERT` |
| `DE_BUILD_TESTS` | `ON` | Builds `UnitTests` and registers CTest tests |
| `DE_ENABLE_GPU_MARKERS` | `ON` | PIX + NVTX markers; downloads WinPixEventRuntime at configure time |
| `DE_ENABLE_FREETYPE` | `ON` | Builds ImGui with FreeType for sharper UI fonts; fetches FreeType at configure time |

Example: `cmake -S . -B ./build -DDE_ENABLE_GPU_MARKERS=OFF`.

### Third-party code

| Dependency | How it arrives |
|------------|----------------|
| Box2D 3.x | vendored, `third_party/box2d` (Sandbox2D) |
| Box3D 0.1.0 | vendored, `third_party/box3d` (`Physics/`) |
| cgltf | vendored, `third_party/cgltf` |
| nlohmann/json | vendored, `third_party/nlohmann` |
| NVTX 3 (headers only) | vendored, `third_party/nvtx` |
| Dear ImGui v1.91.8-docking | `FetchContent` at configure |
| FreeType VER-2-13-3 | `FetchContent` at configure (when `DE_ENABLE_FREETYPE=ON`, default ON) |
| GoogleTest v1.15.2 | `FetchContent` at configure (tests only) |
| WinPixEventRuntime 1.0.240308001 | NuGet download at configure (SHA-256 checked), into `build/_deps/` |
| DirectX 12 Agility SDK 1.620.2 (`Microsoft.Direct3D.D3D12`, `D3D12_SDK_VERSION` 620) | NuGet download at configure (SHA-256 checked), `cmake/AgilitySdk.cmake`. Microsoft DirectX SDK license; `D3D12Core.dll` / `d3d12SDKLayers.dll` are redistributable, headers MIT |
| DXC 1.9.2609.5 (`Microsoft.Direct3D.DXC`, release v1.9.2609) | NuGet download at configure (SHA-256 checked), `cmake/Dxc.cmake`. LLVM (NCSA) + MIT; `dxil.dll` under the Microsoft license, redistributable |
| D3D12 Memory Allocator 3.3.0 (GPUOpen, commit `6009492`) | GitHub archive download at configure (SHA-256 checked), `cmake/D3D12MA.cmake`; built into `DarkRender` (only `Render/GpuAllocator.cpp` includes it). MIT, Copyright (c) 2019-2026 Advanced Micro Devices, Inc. |

Some art folders are gitignored (for example `content/models/BirchTree`, `NaturePack`, `Grass`, `RockMoss`, and some `content/audio/` folders). If a foliage model is missing, the renderer logs a warning and uses a procedural prototype instead.

## Run

```bat
build\bin\Debug\Sandbox.exe
build\bin\Debug\Sandbox.exe -scene:stealth_test.json
build\bin\Debug\Sandbox2D.exe
build\bin\Debug\Editor.exe
build\bin\Debug\VisualDebugger.exe -join 127.0.0.1
```

Editor opens a blank 3D scene: the grid on Y=0 at the origin, with no terrain. File → Open 3D Level still starts on `content/scenes/level.json`. Sandbox builds its 3D level from that file unless `-scene:` names another one.

### Command-line flags

| Flag | Hosts | Effect |
|------|-------|--------|
| `-host [port]` / `-host:<port>` | Sandbox, Sandbox2D, Editor | Host a network session (default port 26160) |
| `-join <ipv4>` / `-join:<ipv4>` | Sandbox, Sandbox2D, Editor | Join a session |
| `-debug [port]` / `-debug:<port>` | Sandbox, Sandbox2D, Editor | Start the debug server (default TCP 26162) |
| `-join <ipv4>` | VisualDebugger | Connect to a host's debug server (default port 26162) |
| `-scene:<file>` | Sandbox, Editor | Load `content/scenes/<file>` instead of `level.json` (colon form only). The Editor opens it on start. |
| `-menu` / `-no-menu` | Sandbox, Sandbox2D | Force the main menu on or off. Env `DE_NO_MENU` also turns it off, and `-host` / `-join` skip it. |
| `-debug-levels` / `-no-debug-levels` | Sandbox | Show or hide the right-side jump-to-level list. Sandbox defaults to on. Env `DE_NO_DEBUG_LEVELS` also hides it. |
| `-splash` / `-no-splash` | all | Force the boot splash on or off |
| `-no-vsync` | all | Present without vsync (timing runs) |
| `-window WxH` | all | Window size, e.g. `-window 2560x1440` |
| `-capture-frame N -camera <preset>` | Sandbox, Editor | Shoot frame N from a camera preset in `content/perf/capture_cameras.json`, write a PNG and timing JSON, then quit. Also `-capture-avg K`, `-capture-out <dir>`, `-capture-stay`. See [docs/perf/README.md](docs/perf/README.md). |

D3D12 diagnostics env vars: `DE_D3D12_DEBUG=0` (debug layer off in Debug), `DE_D3D12_GBV=1` (GPU-based validation, Debug), `DE_D3D12_DRED=0|1` (DRED, on by default in Debug). See [docs/perf/README.md](docs/perf/README.md#d3d12-diagnostics-switches).

### Input and config files

Key, mouse, and gamepad bindings live in JSON, not in code. Each host loads its app file first, then the entity file; a row in the entity file replaces any action of the same name.

| File | Loaded by | Holds |
|------|-----------|-------|
| `content/sandbox.input.json` | Sandbox | App commands: quit, pause, step, saves, lighting, reset, speed, fly camera, Dev Tools, profiler, inventory, journal |
| `content/entities/player.entity.input.json` | Sandbox, Editor (Play) | Player move, look, jump, sprint, crouch, attack, shield, flashlight, weapons 1–3, dodge, pickup (E in Editor Play), takedown, vision modes, camouflage |
| `content/editor.input.json` | Editor | Editor commands: place, delete, type select, panels, debug views, save / load |
| `content/sandbox2d.input.json` | Sandbox2D | App commands: quit, reset, debug, net host / join / disconnect, debug listen |
| `content/entities/player2d.entity.input.json` | Sandbox2D | 2D jump and move |
| `content/entities/<type>.entity.movement.json` | Sandbox, Editor (Play), AI | Walk / run speed, acceleration, gravity, jump values for `player`, `hunter`, `wolf` |
| `content/terrain/lod.json` | Terrain, grass | Terrain chunk LOD multiples and grass LOD distances |

Binding rows name an `action` plus one of `key`, `button`, `axis`, or `mouse`. Optional fields: `scale` (-1 reverses), `threshold` (makes an axis a hold), `app` (`sandbox` or `editor` only), and `mod` (`ctrl`, `shift`, or `alt`; Editor). Key and button names are parsed in `Scene/EntityInput.cpp`. A missing file logs an error and leaves those actions unbound.

### Sandbox controls (keyboard and mouse)

Defaults from the files above:

| Key | Action |
|-----|--------|
| W A S D | Move (fly camera: forward / strafe). Double-tap a direction to dodge. |
| Space | Jump; tap again quickly to double jump. Jump while moving into a wall to climb it. (Fly: climb) |
| Left / Right Shift | Sprint |
| Left / Right Ctrl | Crouch |
| F / left mouse | Attack; hold to charge, release to swing |
| V / right mouse | Shield (hold) |
| 1 / 2 / 3 | Melee / rifle (if in the inventory) / throwable belt |
| G | Pick up the nearest item or throwable, open the nearest loot bag, or carry a body |
| E | Takedown from behind |
| N / H / U / B | Night sight / infrared / ultraviolet / echolocation |
| I | Inventory |
| J | Quest journal |
| C | Camouflage |
| L | Flashlight (when equipped) |
| T | Walk the wiggle demo |
| R | Reset the demo cube |
| F3 / O | Pause gameplay (fly camera while paused) / step one frame while paused |
| = / - | Demo cube spin up / down |
| M | Toggle Dev Tools |
| P | Toggle the Profiler panel |
| F2 | Toggle lighting |
| F5 / F9 | Quick save / quick load |
| Esc | Main menu, or quit |

Gamepad defaults are in the same files (for example A jump, B attack, LT shield, Start pause, Back quit).

### Editor keys

From `content/editor.input.json`: F12 Play, P place, Delete / Backspace delete, 1–8 select the place type (cube, sphere, particle, point light, spot light, player, hunter, wolf), `[` / `]` select previous / next, G grid, F solid ground, Tab snap, F2 / F4 / F8 / F10 particle / animation / HSM / asset panels, F3 2D/3D scene mode, F1 / F6 / F7 / F11 debug fill / lighting / shadows / G-buffer, F5 or Ctrl+S save scene, F9 or Ctrl+O load scene, Shift+F5 / Shift+F9 quick save / load. An archetype `editor.hotkey` selects that brush. Right-mouse look, middle-mouse pan, wheel zoom, left-click picking, and the Shift camera boost are in code.

### Boot splash

Sandbox and Sandbox2D show a two-phase boot splash (engine logo, then host) in all configs. Editor shows it in Release only. VisualDebugger shows it only with `-splash`. First match wins: env `DE_NO_SPLASH` (any non-empty value) → `-no-splash` → `-splash` → `AppConfig.showSplash == false` → `"enabled": false` in `content/loading/engine.json` → on. Per-host overrides live in `content/loading/<host>.json` (`sandbox`, `sandbox2d`, `editor`). Esc, left click, or gamepad Start skips the rest of the current phase. In Release the engine phase can't be skipped.

## Shaders

HLSL lives in `content/shaders/`. Every permutation the engine compiles is listed in `content/shaders/shaders.manifest` (`<file> <entry> <vs|ps|cs> [NAME=VALUE,...]`, defines sorted by name). Call sites use `compileShaderFromContent(path, entry, ShaderStage::Pixel, blob, defines)`; the backend is chosen once per process (`Render/ShaderCompile.*`):

- **DXC at runtime** (Debug builds of every exe, Editor and UnitTests in every config): `IDxcCompiler3` at **Shader Model 6.0** (`kShaderModelDxc` in `Render/ShaderKey.h`), HLSL 2021, strict (`-Ges`). Debug adds `-Od -Zi -Qembed_debug` so PIX and Nsight show HLSL source. DXC is loaded from `<exe dir>/dxc/` with `LoadLibrary`. Results are cached in `%LOCALAPPDATA%/DarkEngine6/ShaderCache/`, keyed by source, includes, entry, profile, defines, arguments and compiler version.
- **Baked DXIL** (Release / RelWithDebInfo / MinSizeRel of Sandbox, Sandbox2D and VisualDebugger): the `de_shaders` target runs `dxc.exe` over the manifest at build time into `build/shaders/`, which is copied to `<exe dir>/shaders/`. These builds have no DXC DLLs. A permutation missing from the manifest fails with an error naming it (Debug logs a warning the first time code asks for one).
- **FXC fallback** (`DE_SHADER_FXC=1`): `D3DCompile` at Shader Model 5.1, kept for DXC A/B comparison until the end of render-graph Phase 2.

The boot splash shaders are embedded in the exe as byte arrays generated at build time (`cmake/EmbedShaders.cmake`).

| Environment variable | Effect |
|----------------------|--------|
| `DE_SHADER_FXC=1` | Runtime-compiling exes use FXC instead of DXC |
| `DE_SHADER_BAKED=1` | Load baked DXIL from `<exe dir>/shaders/` even where runtime DXC is available (exercises the Release path in Debug) |
| `DE_SHADER_CACHE=0` | Turns off the DXC shader cache |
| `DE_STARTUP_GATE_FAKE=<check>` | Fakes one startup-check failure to see its message: `os`, `agility`, `fl`, `sm`, `binding`, `barriers`, `driver` |

## Profiling (PIX and Nsight)

With `DE_ENABLE_GPU_MARKERS=ON` (default), `Render/Profile.h` gives you:

- `GpuScope`: a PIX event on the D3D12 command list plus a matching CPU range
- `CpuScope`: a CPU-only range
- `ProfileColor`: one color per pass, shared by PIX and NVTX

The markers are PIX events (WinPixEventRuntime, `USE_PIX` defined so they stay in **Release**) and NVTX ranges (header-only). PIX, Nsight Graphics, and Nsight Systems read the GPU events. NVTX is what Nsight shows on the CPU. Named scopes cover the frame, ECS update, shadows, G-buffer, terrain, grass, opaque meshes / models, projectiles, instance and foliage culls, deferred lighting, local lights, decals, sky, clouds, water, translucent, particles, SSR, GTAO, post (auto exposure, bloom, TAA, motion blur, tonemap), HUD, ImGui, and the debug overlay.

To capture, launch a host (for example `Sandbox.exe`) under PIX on Windows, Nsight Graphics, or Nsight Systems.

| Environment variable | Effect |
|----------------------|--------|
| `DE_STABLE_GPU_POWER=1` | Calls `ID3D12Device::SetStablePowerState(TRUE)` to lock GPU clocks. Requires Windows Developer Mode. |
| `DE_D3D12_DEBUG=0` | Skips the D3D12 debug layer in Debug builds |

Debug builds also skip the debug layer by themselves when they detect a PIX or Nsight capture module. There is no WARP fallback: a machine that fails the startup check (see [Requirements](#requirements)) exits with a message. Unit tests use WARP at feature level 12_1.

### In-engine profiler

Sandbox has a **Profiler** panel (P). It shows fps, frame time, CPU time, and GPU time, plus CPU and GPU milliseconds for each scope. GPU times come from D3D12 timestamp queries and lag the current frame by one or two frames. The timings are collected whether or not `DE_ENABLE_GPU_MARKERS` is on; only the PIX / NVTX events depend on it.

For numbers from another process, run a host with `-debug` and connect `VisualDebugger`. It reads `PerfCounters` frame timings over TCP.

## Testing

```bat
cmake --build build --config Debug --target UnitTests
build\bin\Debug\UnitTests.exe
```

or through CTest:

```bat
ctest --test-dir build -C Debug --output-on-failure
```

Tests live under `UnitTests/<Module>/` (including `UnitTests/Play/`) and use GoogleTest (discovered with `gtest_discover_tests`, label `unit`). GPU tests (GTAO, SSR, terrain erosion, local shadows, profiling, draw lists and instancing, skinned instancing, foliage and grass, cloud layer, shader compile) create a WARP device at feature level 12_1 (`Dark::createWarpTestDevice`) and skip when WARP is not available. `ShaderManifestTests` checks that the manifest parses, covers every `.hlsl`, and that every line compiles with the runtime DXC.

## Layout

```
AI/ Animation/ Assets/ Audio/ Character/ Collision/ Combat/ Core/
Debug/ ECS/ Game/ Gameplay/ Input/ Math/ Media/ Network/ Particles/
Physics/ Render/ Save/ Scene/ Sky/ Sprite/ Terrain/ Water/ Weapons/
Play/             play code shared by Sandbox and Editor Play (attach, camera, motor, spawners, crows, facility, PlaySession, ...)
Scene/SceneWorld.h  one scene simulation per app (terrain, water, foliage, physics, sky)
Ui/               ImGui helpers (Editor / VisualDebugger / Sandbox) + MainMenu
Sandbox/          3D sample. See the shell split below.
Sandbox2D/        2D sample
Editor/           ImGui editor
VisualDebugger/   remote perf / debug viewer
UnitTests/        GoogleTest suite, one folder per module
content/          runtime data and the content database. Top-level Content*.cpp is compiled into DarkEngine. Subfolders are data: shaders, scenes, archetypes, entities, input files, models, terrain, ai, skills, audio, items, quests, movies, ...
third_party/      box2d, box3d, cgltf, nlohmann, nvtx
cmake/            compiler options, layered targets, content copy, Version.h, NuGet packages (WinPixEventRuntime, Agility SDK, DXC), shader bake, de_host_exe
docs/plans/       feature plans and notes
scripts/          format, no-exceptions check, Sourcetrail setup, asset generators
Pref/             Nsight Graphics project file
AGENTS.md         rules for humans and coding agents
```

Design docs live next to the code they describe (for example `Render/DESIGN-*.md`, `Terrain/DESIGN-*.md`, `Water/DESIGN-water-bodies.md`, `Combat/DESIGN-status-effects.md`, `Save/DESIGN-save-load.md`).

The Sandbox shell is split. `SandboxApp` owns the window, the input map, `onUpdate`, the play host, net pawn glue, one `SceneWorld`, one `PlaySession`, dialogue, movie playback, and the save system. `onRender` calls `SandboxFrame::render()`. `SandboxBoot.cpp` defines scene boot (`prepareBoot`, `startScene`, the spawn batches). `onInit` stays in `SandboxApp.cpp`. The Editor owns its own `SceneWorld` and `PlaySession`. Its edit camera, GPU pipelines, stream-authoring list, and terrain-authoring flags stay on `EditorApp`.

| File | Owns |
|------|------|
| `Scene/SceneWorld.h` | Shared scene simulation: camera, sky, lighting settings, terrain, foliage, grass, leaves, wind, physics, and water. Each app owns one |
| `Play/PlaySession.*` | One play session: runtime, facility, combat, guard voice, crows, feedback, pawn entity ids, the scene quest id, and building fixture lists |
| `Water/Water.*` | The lake sheet and stream ribbons (`addRibbon`, cap 16) |
| `Sandbox/SandboxDemo.*` | Origin cube, glTF samples, wiggle actor, skeleton lines, hybrid local lights, health packs placed at boot, the demo-scene flag, the cross mesh, and spin speed |
| `Sandbox/SandboxFrame.*` | The 3D frame: projectile draws, debug overlays, the draw list, skinned instances, `SceneRenderer`, accepted-entity sets, rejected shots, the previous-world map, and the GPU set (foliage, grass, leaves, terrain material, particles, cloud LOD, auto exposure) |
| `Sandbox/SandboxHud.*` | Dev HUD that used to live in `DevToolsPanel.cpp`, plus quest banners, defense text, the journal, the loot target, the upgrade panel, the goal timer, the NPC info overlay, ImGui, and the main menu |
| `Sandbox/SandboxSave.cpp` | Sandbox save host |
| `Sandbox/PathChase.*` | Boot nav helper |
| `Sandbox/SandboxInternal.h` | File-scope helpers shared by those translation units, and the shadow-callback user |

`DevToolsPanel.cpp` is gone. Vision flags are `SandboxApp::SandboxVision`, filled into `PlayHost` from Sandbox only. The Editor frame is still `Editor/EditorRender3D.cpp`. The Editor hard-codes camouflage false.

Dated plans and `DESIGN-*.md` files cite `Sandbox/SandboxApp.cpp` and `Sandbox/DevToolsPanel.cpp` with the line numbers of their own date. This README is the current map. The record of the split is [docs/plans/2026-11-10-review-followup.md](docs/plans/2026-11-10-review-followup.md).

## Conventions

Full rules are in [`AGENTS.md`](AGENTS.md). Short version:

- No C++ exceptions in engine or sample code. Return `bool` / status, log, `DE_ASSERT`. Read JSON fields through `Core/Json.h` helpers rather than nlohmann `value()` / `get<T>()` on unchecked types. `scripts\check-no-exceptions.ps1` scans for violations.
- **C++23**, MSVC-friendly, `.clang-format` (Allman, 4-space). `scripts\format-all.ps1` formats or checks the tree.
- Log with `DE_LOG_INFO` / `WARN` / `ERROR` / `FATAL` from `Core/Log.h`, with a `LogCategory` first (`Core`, `Render`, `Audio`, `Collision`, `AI`, `Input`, `Networking`, `Debug`, `Media`).

## Sourcetrail

1. Generate a compilation database (Ninja; Visual Studio generators do not write one): `scripts\setup-sourcetrail.bat`. It configures `build-sourcetrail/` and writes `compile_commands.json` at the repo root.
2. Open `DarkEngine6.srctrlprj` in Sourcetrail and start indexing. Re-run the script and *Refresh* when sources change.

## Roadmap (planned, not implemented)

- **Shader compiler:** DXC compiles at SM 6.0 today; the move to SM 6.6 and removing the FXC fallback come after the Windows A/B (render-graph plan Phase 2).
- **Scripted behaviors:** Mods today are data only (archetypes, items, statuses, loadouts, upgrades). A new component, status effect, or upgrade verb is still C++.
- Open design drafts in the tree cover more work, for example `Render/DESIGN-pbr-roadmap.md`, `Render/DESIGN-color-management.md`, and `Audio/DESIGN-audio-system.md`. Check each doc's status line before assuming its state.

## License

MIT. See [LICENSE](LICENSE).
