# DarkEngine6

Home C++ game engine. Sixth version of DarkEngine. Windows-only for now: C++20, D3D12, XAudio2, XInput.

This is a working engine-in-progress, not a finished product. The README describes what is actually in the tree.

## What you get

| Target | Kind | Role |
|--------|------|------|
| `DarkEngine` | static lib | Engine |
| `Sandbox` | exe | 3D sample |
| `Sandbox2D` | exe | Side-scrolling 2D sample (Box2D) |
| `Editor` | exe | ImGui editor (Win32 + DX12), including a particle panel and a Network host/join menu |
| `UnitTests` | exe | GoogleTest suite |

Engine folders compiled into `DarkEngine`:

`AI`, `Assets`, `Audio`, `Character`, `Collision`, `Core`, `ECS`, `Geometry`, `Input`, `Math`, `Network`, `Particles`, `Render`, `Scene`, `Sky`, `Sprite`, `Terrain`, `Water`.

Runtime HLSL lives in `content/shaders/` (the `Shaders/` source folder is not the runtime shader tree).

### Stack, from the code

- **Core** — `Application`, Win32 `Window`, logging (`DE_LOG_*` + `LogCategory`), paths, UUID.
- **Render** — D3D12 renderer, 2D/3D cameras, mesh/sprite/line/particle/terrain/water/sky/shadow pipelines, shader compile, debug overlay.
- **Audio** — XAudio2 system, WAV clips.
- **Input** — XInput (linked from CMake).
- **ECS** — small `World` / `Entity` / component set.
- **Network** — UDP sockets (`ws2_32`), packets, reliability, replication types, `NetworkSystem`, a fake transport for tests. Recent work: UDP beacon discovery on port 26161, Sandbox2D host/join, Editor Network menu.
- **Character** — humanoid body / physiology headers (not a finished gameplay character controller).
- **Sandbox2D** — links Box2D from `third_party/box2d`.
- **Editor** — Dear ImGui v1.91.8 (fetched at configure time).

## Requirements

- Windows
- CMake 3.22+
- A C++20 MSVC toolchain (Visual Studio generator is the usual path)
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

## Layout

```
AI/ Assets/ Audio/ Character/ Collision/ Core/ ECS/
Geometry/ Input/ Math/ Network/ Particles/ Render/
Scene/ Sky/ Sprite/ Terrain/ Water/
Sandbox/          3D sample
Sandbox2D/        2D sample
Editor/           ImGui editor
UnitTests/
content/          runtime data + HLSL
third_party/      Box2D (and other vendored deps)
cmake/            compiler options, content copy
scripts/          Sourcetrail helper, etc.
AGENTS.md         rules for humans and coding agents
```

## Conventions

Standing rules for anyone (or any agent) touching this repo are in [`AGENTS.md`](AGENTS.md). Short version:

- No C++ exceptions in engine or Sandbox code. Return `bool` / status, log, `DE_ASSERT`.
- C++20, MSVC-friendly, `.clang-format` (Allman, 4-space).
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
