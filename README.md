# DarkEngine6

Home C++ game engine. Sixth version of DarkEngine. Windows-only for now: **C++23**, D3D12, XAudio2, XInput.

This is a working engine-in-progress, not a finished product. The README describes what is actually in the tree.

## What you get

| Target | Kind | Role |
|--------|------|------|
| `DarkEngine` | static lib | Engine |
| `Sandbox` | exe | 3D sample (ImGui Dev Tools, networked spawn/draw) |
| `Sandbox2D` | exe | Side-scrolling 2D sample (Box2D) |
| `Editor` | exe | ImGui editor (Win32 + DX12), particle panel, Network host/join, mirrors networked objects for draw |
| `UnitTests` | exe | GoogleTest suite |
| `VisualDebugger` | exe | Performance and debugging; connect to Sandbox |

Engine folders compiled into `DarkEngine`:

`AI`, `Assets`, `Audio`, `Character`, `Collision`, `Core`, `Debug`, `ECS`, `Geometry`, `Input`, `Math`, `Network`, `Particles`, `Render`, `Scene`, `Sky`, `Sprite`, `Terrain`, `Water`.

Shared `Ui/` (ImGui helpers / styles) is linked into `Editor`, `VisualDebugger`, and `Sandbox`.

Runtime HLSL lives in `content/shaders/` (the `Shaders/` source folder is not the runtime shader tree).

Configure generates `Core/Version.h` (git describe / commit when available).

### Stack, from the code

- **Core** — `Application`, Win32 `Window`, logging (`DE_LOG_*` + `LogCategory`), paths, UUID, generated `Version.h`.
- **Render** — D3D12 renderer, 2D/3D cameras, mesh/sprite/line/particle/terrain/water/sky/shadow pipelines, shader compile, debug overlay.
- **Audio** — XAudio2 system, WAV clips.
- **Input** — XInput (linked from CMake).
- **ECS** — `World` / generation-packed `EntityID` (slot + generation; `NULL_ENTITY` is 0), components, O(1) `alive()`, safe emplace over existing entities.
- **Assets** — `AssetManager` / handles, texture cache, **glTF** load via cgltf (`GltfLoader`).
- **Network** — UDP sockets (`ws2_32`), packets, reliability, replication types, `NetworkSystem`, fake transport for tests. UDP beacon discovery on port **26161**. Sandbox2D host/join; Editor Network menu.
  - **Draw contract:** `NetworkSystem` does not draw. Sandbox attaches a `MeshComponent` (or equivalent) in `onNetSpawn` / `spawnOwnedPawn`. Editor must call `mirrorNetworkedObjects()` (or equivalent) so networked entities appear in the local draw list before render. See `Network/Replication.h`.
- **Debug** — engine-side debug helpers compiled into `DarkEngine`.
- **Character** — humanoid body / physiology headers (not a finished gameplay character controller).
- **Sandbox2D** — links Box2D from `third_party/box2d`.
- **Editor / Sandbox UI** — Dear ImGui **v1.91.8-docking** (fetched at configure time) plus shared `Ui/` styles.

## Requirements

- Windows
- CMake 3.22+
- A **C++23** MSVC toolchain (Visual Studio generator is the usual path)
- Git (ImGui is fetched on first configure)

## Build

Configure (or double-click `BuildProjectFiles.bat`):

```bat
cmake -S . -B ./build
```

Build and run tests (Debug):

```bat
cmake --build build --config Debug
cmake --build build --config Debug --target UnitTests
build\bin\Debug\UnitTests.exe
```

Executables land in `build/bin/<Config>/`. `content/` is copied next to `Sandbox.exe`, `Sandbox2D.exe`, and `Editor.exe` on each build.

CMake options:

- `DE_ENABLE_ASSERTS` (default ON)
- `DE_BUILD_TESTS` (default ON)

## Boot splash

Sandbox and Sandbox2D show a two-phase boot splash (engine logo, then host) in all configs. Editor shows it in **Release** only (`_DEBUG` starts with splash off). VisualDebugger stays off unless you pass `-splash`.

| Control | Effect |
|---------|--------|
| `-splash` | Force splash on (beats Editor Debug / VisualDebugger `showSplash = false`) |
| `-no-splash` | Force splash off |
| env `DE_NO_SPLASH` (any non-empty value) | Force splash off (CI) |
| `content/loading/engine.json` `"enabled": false` | Off unless `-splash` |
| `AppConfig.showSplash` | Host default; `false` wins unless `-splash` |

First match wins: `DE_NO_SPLASH` → `-no-splash` → `-splash` → `showSplash == false` → JSON `enabled == false` → on.

Skip (Escape, left click, gamepad Start) zeros remaining dwell of the **current** phase. It does not abort `onInit` or quit the process.

- **Debug:** skip is always allowed for engine and host (`skipOnKey` cannot disable this).
- **Release:** engine dwell is unskippable; host is skippable (`skipOnKey`, default true).

Quit during splash is Alt-F4 / window close, not Escape.

JSON overlay: `content/loading/engine.json` then `content/loading/<hostId>.json` (`sandbox`, `sandbox2d`, `editor`). `engine.json` keeps `host.image` empty so hosts do not share art. `"reducedMotion": true` uses a bar plus a 2 s opacity breathe (no ring) and skips the 0.2 s fade-out to gameplay. `"animation": "none"` uses a frozen full-opacity bar (no ring, no breathe); fade-out still runs.

## Layout

```
AI/ Assets/ Audio/ Character/ Collision/ Core/ Debug/ ECS/
Geometry/ Input/ Math/ Network/ Particles/ Render/
Scene/ Sky/ Sprite/ Terrain/ Water/
Ui/               shared ImGui helpers (Editor, VisualDebugger, Sandbox)
Sandbox/          3D sample
Sandbox2D/        2D sample
Editor/           ImGui editor
UnitTests/
VisualDebugger/
content/          runtime data + HLSL
third_party/      Box2D (and other vendored deps)
cmake/            compiler options, content copy, Version.h.in
docs/             plans / notes
scripts/          Sourcetrail helper, etc.
AGENTS.md         rules for humans and coding agents
```

## Conventions

Standing rules for anyone (or any agent) touching this repo are in [`AGENTS.md`](AGENTS.md). Short version:

- No C++ exceptions in engine or Sandbox code. Return `bool` / status, log, `DE_ASSERT`.
- **C++23**, MSVC-friendly, `.clang-format` (Allman, 4-space).
- Log with `DE_LOG_INFO` / `WARN` / `ERROR` / `FATAL` from `Core/Log.h`.

## Sourcetrail (code graph / coupling explorer)

Sourcetrail indexes C++ so you can browse class relationships, includes, and call edges.

1. **One-time / when sources change** — generate a Clang compilation database (Ninja; VS generators do not emit one):

   ```bat
   scripts\setup-sourcetrail.bat
   ```

   This configures `build-sourcetrail/`, writes `compile_commands.json` at the repo root, and optionally `compile_commands.engine.json` (engine TUs without gtest/imgui).

2. **Open the project** — double-click `DarkEngine6.srctrlprj` or *File → Open Project* in Sourcetrail.

3. **Index** — choose *Start*. First index takes a few minutes. Later use *Refresh* after re-running the setup script.

Sourcetrail is installed via `winget install CoatiSoftware.Sourcetrail` if missing. Database files (`*.srctrldb`) are gitignored.

## License

MIT. See [LICENSE](LICENSE).
