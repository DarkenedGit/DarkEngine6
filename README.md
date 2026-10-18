# DarkEngine6

A home C++ game engine, the sixth version of DarkEngine. Windows only for now: **C++23**, Direct3D 12, XAudio2, XInput.

This engine is still in progress. This README covers only what is in the tree today. Design notes in the tree (`*/DESIGN-*.md`) each give their own status (Draft, Proposed, Approved, Accepted, Implemented). A design doc does not mean the feature has shipped.

## Targets

| Target | Kind | Role |
|--------|------|------|
| `DarkFoundation` | static lib | `Math`, `Collision`, `ECS`, foundation `Core` (`Log`, `Paths`, `ContentRoots`, `UUID`) |
| `DarkAssets` | static lib | `Assets/*` (WIC image decode, glTF via cgltf) |
| `DarkRender` | static lib | `Render/*` + D3D12 / DXGI / `d3dcompiler`; PIX + NVTX markers when `DE_ENABLE_GPU_MARKERS` is on |
| `DarkNet` | static lib | `Network/*` (`ws2_32`) |
| `DarkGameplay` | static lib | `Weapons/*`, `Gameplay/*`, `Combat/*` |
| `DarkEngine` | static lib | Umbrella: `AI`, `Animation`, `Audio`, `Character`, `Debug`, `Input`, `Particles`, `Physics`, `Play`, `Save`, `Scene`, `Sky`, `Sprite`, `Terrain`, `Water`, plus `Application` / `Window` / `MemoryTracker` and `Ui/MainMenu`. PUBLIC-links every layer; PRIVATE-links Box3D |
| `Sandbox` | exe | 3D sample game (ImGui Dev Tools) |
| `Sandbox2D` | exe | Side-scrolling 2D sample (Box2D) |
| `Editor` | exe | ImGui editor (Win32 + DX12) with Play-in-editor |
| `VisualDebugger` | exe | Connects to a running host over TCP for performance and debug data |
| `UnitTests` | exe | GoogleTest suite (built when `DE_BUILD_TESTS` is on) |

Hosts link only `DarkEngine`. Each engine `.cpp` compiles in exactly one target. One exception to the folder rule: `Render/GrassPipeline.cpp` is compiled into `DarkEngine` instead of `DarkRender`, because it calls terrain code and `DarkRender` cannot link back into `DarkEngine`. The layer graph is documented at the top of `cmake/DarkEngineTargets.cmake`.

Runtime HLSL lives in `content/shaders/`. It shows up in the IDE but is compiled at runtime, not at build time (see [Shaders](#shaders)).

## What is in the tree

- **Core**: `Application`, Win32 `Window`, logging (`DE_LOG_*` + `LogCategory`), paths and content roots, UUID, memory tracker. Configure generates `Core/Version.h` (git describe / commit when available).
- **ECS**: `World` with generation-packed `EntityID` (slot + generation; `NULL_ENTITY` is 0), O(1) `alive()`, safe emplace over existing entities.
- **Render**: a D3D12 renderer with two scene paths: `HybridDeferred` (G-buffer, fullscreen lighting, forward transparents) and `SwapChainForward` (`-forward`). It uses reverse-Z depth (`GREATER` compare). `SceneRenderer` is shared by Sandbox and Editor. Passes and systems in `Render/`:
  - Cascaded directional shadows (up to 3 cascades, `ShadowSystem`) and local-light shadows (`LocalShadowSystem`). Static and model shadow casters are culled against each shadow view's frustum.
  - Local lights drawn as light volumes (`LocalLightVolumePipeline`)
  - Deferred decals as oriented boxes (`DecalPipeline`, `DecalPool`)
  - GTAO, SSR (half-res, see `Render/DESIGN-reflections.md`), fog, IBL bake (`IblBake`, `GpuIbl`)
  - Sky with a distant cloud layer (`Sky/CloudLayer`, `content/shaders/CloudLayer.hlsli`, see `Render/DESIGN-cloud-layer.md`), volumetric clouds (`CloudVolumePipeline`; the cloud volume in the default scene is disabled), water (`WaterPipeline`)
  - Terrain, terrain foliage instancing with alpha-cutout shadows (`FoliagePipeline`), GPU terrain erosion bake (`TerrainErosionPipeline`)
  - Procedural grass (`GrassPipeline`): blade LODs, per-tile Perlin wind, and bending around the player (see Terrain below and `Terrain/DESIGN-procedural-grass.md`)
  - Static and skinned meshes, sprites, lines, particles, camouflage
  - Post: auto exposure, bloom, TAA, motion blur, tonemap
  - Loading screen / boot splash, HUD (crosshair, health), debug overlay (G-buffer tiles)
- **Draw submission** (`DrawList`, `RenderItem`, `InstancedMeshPipeline`, `SkinnedInstanceBuffer`, `InstanceCull`):
  - Static opaque meshes and models are batched and drawn instanced. Batches sort by population, mesh, alpha mask, then material (`BatchKey`).
  - Batches can be drawn with `ExecuteIndirect`. A compute pass (`InstanceCull.hlsl`) frustum-culls static instances per view (camera and shadow views) and writes the indirect arguments.
  - Skinned characters draw instanced: bone palettes are uploaded per frame and skinning runs in the instanced vertex shaders (`SkinnedInstance.hlsli`). Skinned parts have their own GPU cull (`SkinnedInstanceCull`).
  - Terrain foliage is culled on the GPU (`FoliageCull.hlsl`). Grass is culled per 8 m tile on the CPU.
  - Dev Tools and the Editor View menu have **Instanced static meshes** and **Indirect static draws** toggles (both on by default). Wireframe and point fill stay on the immediate path.
- **Pass order**: `Render/PassTable.*` lists the expected pass order for Sandbox and Editor, deferred and forward. Hosts check the passes they actually played against it and log once on a mismatch. The deferred order is: shadows, local shadows, G-buffer, decals, GTAO, SSR, deferred lighting, local light volumes, sky, SSR capture, water, clouds, camouflage (Sandbox) or editor grid and light gizmos (Editor), translucent, particles, post, overlay.
- **Assets**: `AssetManager` / handles, texture cache, glTF load via cgltf (`GltfLoader`), animated glTF.
- **Animation**: skeletons, clips, sampler, anim graph (`*.anim.json`), locomotion, anim notifies. Locomotion clip rate follows planar speed. Procedural layers add a directional hit flinch and a shield guard pose.
- **AI**: hierarchical state machines loaded from JSON (`content/ai/*.hsm.json`), attack patterns, pathfinding, walkability, sight, wolf approach / flanking. An awareness meter (`AI/Awareness`) moves enemies through Calm, Suspicious, Investigating, Alert, and Searching from sight, motion, noise, and proximity. Per-type movement speeds come from `content/entities/*.entity.movement.json`.
- **Play** (`Play/`): code shared by Sandbox play and Editor Play-in-editor: character attach for players and NPCs, character sound banks, play look and shoulder camera, motor input, physics ground height, terrain streaming, impact decals, weapon target queries, the player flashlight, crows, and spawners.
  - **Spawners**: placed level objects that spawn hunters, wolves, or crows (up to 16 slots). `Maintain` replaces dead units; `Waves` starts the next wave on a timer or when the current wave is dead. Spawned units are not saved; Stop Play removes them.
  - **Crows**: flock, perch in trees, forage on the ground, and flee from living hunters and the player with an alarm call. Dead crows fall to the ground.
- **Physics**: Box3D wrapper (`third_party/box3d`, v0.1.0). Collision shapes are cooked from primitives, model `_col*` parts, or `*.collision.json` sidecars. Per-model `*.physics.json` settings. Surfaces load from `content/physics/surfaces.json`. Foliage trunks and rocks block the player. See `Physics/README.md` and `Collision/DESIGN-box3d.md`.
- **Collision**: collision query code in `Collision/` (separate from the Box3D wrapper in `Physics/`).
- **Character**: player motor (walk, sprint, crouch, swim, jump, double jump, dodge, wall climb, ledge hang and climb-up, wall jump), health, hit reactions with directional knockback, shield, stealth, and six skills (`Shoot`, `Swim`, `Run`, `Jump`, `Hear`, `See`) with use-based XP (`content/skills/*.json`, `Character/DESIGN-skills.md`). Ground movement accelerates between stop, walk, and run; wall climbing and hanging use a stamina pool. Speeds, acceleration, gravity, and jump values load from `content/entities/<type>.entity.movement.json`.
- **Combat / Gameplay / Weapons**: `CombatSystem` (damage resolve, status effects, DoT ticks), status catalog (`Poison`, `Bleed`, `Ignite`, `Chill`, `Shock`, `Stun`), jump attacks, charged attacks, knockdown. Armor pieces (`Combat/ArmorPieces`) absorb hits and break off. `PlayerModeComponent` adds God and Reaper dev switches (both off by default, not saved). See `Combat/DESIGN-status-effects.md`.
  - **Inventory** (`Gameplay/Inventory`, `Gameplay/Item`): a 20-slot bag plus Head, Chest, and Light equip slots. Items come from a built-in table (sword, rifle, medkit, helmet, chest plate, flashlight). Weapon switching needs the weapon in the bag, and the flashlight works while it is equipped. Items on the ground show a label and are picked up with G.
  - **Loot**: dead hunters drop a loot bag with their items and armor. G opens the nearest bag so you can take items.
- **Terrain**: height and splat maps, LOD, `TerrainGrid` streaming ring, procedural Generate with CPU erosion and GPU erosion, World Engine / World Creator map import (`Terrain/WorldEngineMap`, sample under `content/terrain/HurricaneRidge`), and foliage spawned from splat weights and saved per tile (DEFL sidecar). See `Terrain/DESIGN-*.md`.
  - **Procedural grass** (`Terrain/GrassField`, `GrassMesh`, `GrassWind`, `GrassBend`): blade tiles spawned around a point, four blade LODs, per-tile Perlin wind, and blades that bend around the player. Parameters (height, flexibility, density, wind, seed) are saved in the scene's `terrain.grass` block and can be changed in Sandbox Dev Tools under **Procedural grass**.
  - **LOD distances**: `content/terrain/lod.json` holds the terrain chunk LOD multiples and the grass LOD band distances (promote / demote, create / evict radius, fade). If the file is missing, built-in values are used and a warning is logged.
- **Water**: water planes, multiple finite lakes (`WaterBody`), swells, and streams (`WaterStream`: ribbon meshes along a path; the Editor can trace one downhill to the lake). Streams are visual only; you cannot swim in them. `Water/DESIGN-water-bodies.md` has its own status line.
- **Scene**: JSON scene files under `content/scenes/` (`level.json`, `level2d.json`, `parkour_test.json`, `stealth_test.json`) that hold entities, terrain, grass, atmosphere, water, spawners, and exposure settings. Entity templates live in `content/entities/*.entity.json` (`EntityMaster`).
- **Save**: quick and manual saves through `SaveSystem` with atomic file writes. Saves go to `%LOCALAPPDATA%\DarkEngine6\Saves`. See `Save/DESIGN-save-load.md`.
- **Audio**: XAudio2, WAV clips, sound emitters.
- **Input**: keyboard, mouse, XInput gamepad, `ActionMap` bindings. Bindings load from JSON (see [Input files](#input-and-config-files)).
- **Network**: UDP sockets, packets, reliability, replication, `NetworkSystem`, a fake transport for tests. The default game port is **26160** and UDP beacon discovery uses **26161**. `NetworkSystem` does not draw. Hosts attach `MeshComponent` (Editor also adds `EditorObjectComponent`) in `onNetSpawn` (see `Network/Replication.h`).
- **Debug**: `DebugServer` / `DebugClient` over TCP (default port **26162**) and `PerfCounters` (per-slot frame timing plus a 240-frame history) for VisualDebugger.
- **Editor**: scene editing on ECS, translate gizmo, asset browser (F10), drag-and-drop placement, terrain Generate and World Engine import, foliage density panel, water bodies and a **Water** window (add lakes, swells, streams), spawner placement, cloud layer settings, local-light authoring, physics inspector, animation / HSM / particle editors, audio preview panel, network host/join, and Play-in-editor (F12). Procedural grass and water draw in edit mode as well as in Play.
- **UI**: Dear ImGui **v1.91.8-docking** (fetched at configure time) plus shared `Ui/` styles for Editor, VisualDebugger, and Sandbox Dev Tools. `Ui/MainMenu` is a non-ImGui scene picker compiled into `DarkEngine`.

## Requirements

What the build files require:

- Windows (Win32, D3D12, XAudio2, XInput, Winsock)
- CMake **3.22+**
- An MSVC toolchain with **C++23** support. Flags include `/W4 /WX /permissive-`, so warnings fail the build.
- Git (Dear ImGui and GoogleTest are fetched with `FetchContent` on first configure)
- Network access on first configure when `DE_ENABLE_GPU_MARKERS` is on (default). CMake downloads the WinPixEventRuntime NuGet package (see [Profiling](#profiling-pix-and-nsight)).

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

Executables land in `build/bin/<Config>/`. On each build, `content/` is copied next to `Sandbox.exe`, `Sandbox2D.exe`, `Editor.exe`, and `UnitTests.exe`. `WinPixEventRuntime.dll` is copied next to every executable when GPU markers are on.

### CMake options

| Option | Default | Effect |
|--------|---------|--------|
| `DE_ENABLE_ASSERTS` | `ON` | Enables `DE_ASSERT` |
| `DE_BUILD_TESTS` | `ON` | Builds `UnitTests` and registers CTest tests |
| `DE_ENABLE_GPU_MARKERS` | `ON` | PIX + NVTX markers; downloads WinPixEventRuntime at configure time |

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
| GoogleTest v1.15.2 | `FetchContent` at configure (tests only) |
| WinPixEventRuntime 1.0.240308001 | NuGet download at configure (SHA-256 checked), into `build/_deps/` |

Some art folders are gitignored (for example `content/models/BirchTree`, `NaturePack`, `Grass`, `RockMoss`, and some `content/audio/` folders). If a foliage model is missing, the renderer logs a warning and uses a procedural prototype instead.

## Run

```bat
build\bin\Debug\Sandbox.exe
build\bin\Debug\Sandbox.exe -scene:stealth_test.json
build\bin\Debug\Sandbox2D.exe
build\bin\Debug\Editor.exe
build\bin\Debug\VisualDebugger.exe -join 127.0.0.1
```

Editor opens `content/scenes/level.json` by default. Sandbox builds its 3D level from the same file unless `-scene:` names another one.

### Command-line flags

| Flag | Hosts | Effect |
|------|-------|--------|
| `-host [port]` / `-host:<port>` | Sandbox, Sandbox2D, Editor | Host a network session (default port 26160) |
| `-join <ipv4>` / `-join:<ipv4>` | Sandbox, Sandbox2D, Editor | Join a session |
| `-debug [port]` / `-debug:<port>` | Sandbox, Sandbox2D, Editor | Start the debug server (default TCP 26162) |
| `-join <ipv4>` | VisualDebugger | Connect to a host's debug server (default port 26162) |
| `-scene:<file>` | Sandbox | Load `content/scenes/<file>` instead of `level.json` (colon form only) |
| `-forward` | Sandbox, Editor | Use the forward swap-chain path instead of hybrid deferred |
| `-menu` / `-no-menu` | Sandbox, Sandbox2D | Force the scene-picker main menu on or off. Env `DE_NO_MENU` also turns it off, and `-host` / `-join` skip it. |
| `-splash` / `-no-splash` | all | Force the boot splash on or off |

### Input and config files

Key, mouse, and gamepad bindings live in JSON, not in code. Each host loads its app file first, then the entity file; a row in the entity file replaces any action of the same name.

| File | Loaded by | Holds |
|------|-----------|-------|
| `content/sandbox.input.json` | Sandbox | App commands: quit, pause, step, saves, lighting, reset, speed, fly camera, Dev Tools, profiler, inventory |
| `content/entities/player.entity.input.json` | Sandbox, Editor (Play) | Player move, look, jump, sprint, crouch, attack, shield, flashlight, weapons, dodge, pickup, camouflage |
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
| 1 / 2 | Melee / rifle (if in the inventory) |
| G | Pick up the nearest item, or open the nearest loot bag |
| I | Inventory |
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

From `content/editor.input.json`: F12 Play, P place, Delete / Backspace delete, 1–8 select the place type (cube, sphere, particle, point light, spot light, player, hunter, wolf), `[` / `]` select previous / next, G grid, F solid ground, Tab snap, F2 / F4 / F8 / F10 particle / animation / HSM / asset panels, F3 2D/3D scene mode, F1 / F6 / F7 / F11 debug fill / lighting / shadows / G-buffer, F5 or Ctrl+S save scene, F9 or Ctrl+O load scene, Shift+F5 / Shift+F9 quick save / load. Right-mouse look, middle-mouse pan, wheel zoom, left-click picking, and the Shift camera boost are in code.

### Boot splash

Sandbox and Sandbox2D show a two-phase boot splash (engine logo, then host) in all configs. Editor shows it in Release only. VisualDebugger shows it only with `-splash`. First match wins: env `DE_NO_SPLASH` (any non-empty value) → `-no-splash` → `-splash` → `AppConfig.showSplash == false` → `"enabled": false` in `content/loading/engine.json` → on. Per-host overrides live in `content/loading/<host>.json` (`sandbox`, `sandbox2d`, `editor`). Esc, left click, or gamepad Start skips the rest of the current phase. In Release the engine phase can't be skipped.

## Shaders

HLSL is compiled at runtime from `content/shaders/` with **FXC** (`D3DCompile`, linked from `d3dcompiler`) at **Shader Model 5.0** (`vs_5_0`, `ps_5_0`, `cs_5_0`). See `Render/ShaderCompile.*`. Debug builds add `D3DCOMPILE_DEBUG` and skip optimization. Includes resolve relative to the shader file. Nothing is precompiled at build time, and DXC is not used. Compute passes (terrain erosion, instance cull, foliage cull) are also `cs_5_0`.

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

Debug builds also skip the debug layer by themselves when they detect a PIX or Nsight capture module. If no D3D12 hardware adapter is found, the renderer falls back to WARP.

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

Tests live under `UnitTests/<Module>/` (including `UnitTests/Play/`) and use GoogleTest (discovered with `gtest_discover_tests`, label `unit`). GPU tests (GTAO, SSR, terrain erosion, local shadows, profiling, draw lists and instancing, skinned instancing, foliage and grass, cloud layer, shader compile) create a D3D12 device on hardware or WARP, and skip when neither is available.

## Layout

```
AI/ Animation/ Assets/ Audio/ Character/ Collision/ Combat/ Core/
Debug/ ECS/ Gameplay/ Input/ Math/ Network/ Particles/ Physics/
Render/ Save/ Scene/ Sky/ Sprite/ Terrain/ Water/ Weapons/
Play/             play code shared by Sandbox and Editor Play (attach, camera, motor, spawners, crows, ...)
Ui/               ImGui helpers (Editor / VisualDebugger / Sandbox) + MainMenu
Sandbox/          3D sample
Sandbox2D/        2D sample
Editor/           ImGui editor
VisualDebugger/   remote perf / debug viewer
UnitTests/        GoogleTest suite, one folder per module
content/          runtime data: shaders, scenes, entities, input files, models, terrain, ai, skills, audio, ...
third_party/      box2d, box3d, cgltf, nlohmann, nvtx
cmake/            compiler options, layered targets, content copy, Version.h, WinPixEventRuntime
docs/plans/       feature plans and notes
scripts/          format, no-exceptions check, Sourcetrail setup, asset generators
Pref/             Nsight Graphics project file
AGENTS.md         rules for humans and coding agents
```

Design docs live next to the code they describe (for example `Render/DESIGN-*.md`, `Terrain/DESIGN-*.md`, `Water/DESIGN-water-bodies.md`, `Combat/DESIGN-status-effects.md`, `Save/DESIGN-save-load.md`).

## Conventions

Full rules are in [`AGENTS.md`](AGENTS.md). Short version:

- No C++ exceptions in engine or sample code. Return `bool` / status, log, `DE_ASSERT`. `scripts\check-no-exceptions.ps1` scans for violations.
- **C++23**, MSVC-friendly, `.clang-format` (Allman, 4-space). `scripts\format-all.ps1` formats or checks the tree.
- Log with `DE_LOG_INFO` / `WARN` / `ERROR` / `FATAL` from `Core/Log.h`, with a `LogCategory` first.

## Sourcetrail

1. Generate a compilation database (Ninja; Visual Studio generators do not write one): `scripts\setup-sourcetrail.bat`. It configures `build-sourcetrail/` and writes `compile_commands.json` at the repo root.
2. Open `DarkEngine6.srctrlprj` in Sourcetrail and start indexing. Re-run the script and *Refresh* when sources change.

## Roadmap (planned, not implemented)

- **Shader compiler:** move from FXC / SM 5.0 to DXC / SM 6.x. Today all shaders go through `D3DCompile`.
- Open design drafts in the tree cover more work, for example `Render/DESIGN-pbr-roadmap.md`, `Render/DESIGN-color-management.md`, and `Audio/DESIGN-audio-system.md`. Check each doc's status line before assuming its state.

## License

MIT. See [LICENSE](LICENSE).
