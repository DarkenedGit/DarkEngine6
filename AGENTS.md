# DarkEngine6 — Agent / AI rules

These rules apply to **all** work under this repository (engine, Sandbox, Editor, UnitTests helpers you write). Follow them without being asked again.

## Editor and Sandbox are wrappers

The Editor and Sandbox are shells around DarkEngine. The engine owns the behavior. The apps start it, fill a host, and present it.

Anything both apps do is implemented once, in the engine, and called from both. That includes play, combat, dialogue, AI, physics, scene load, and scene-object setup. `PlaySession` and `DialogueController` are the pattern: a host struct of non-owning pointers, filled by the app, with copy and move deleted. The engine does not own the world.

When the same function, tick, query, load path, or rule shows up in `Sandbox/` and `Editor/`, move that body into the engine in the same change. Leave a call in each app. Do not add another copy, and do not leave the old copy beside the new one.

| Kind of code | Where it lives |
|--------------|----------------|
| Simulation and rules both apps run | Engine (`Play/`, `Scene/`, `Gameplay/`, and the other engine folders) |
| Window, input bindings, and that app's presented frame | The app |
| ImGui panels and HUD drawing | The exes. DarkEngine and UnitTests do not link Dear ImGui. A shared HUD is a header included only by the exes, as `Play/DialogueHud.h` is. The state it shows stays in the engine |
| Authoring: gizmos, inspectors, asset browsers, graph editors, file dialogs | `Editor/` |
| Sandbox demo content: the demo cube, the glTF demo, health packs placed at Sandbox boot | `Sandbox/` |

Each app still submits its own render frame. Shared simulation is an input to that frame. Do not write a second simulation so the frames can differ.

Before finishing a task that touches either app, check the other app for the same behavior. One implementation.

## Error handling: no C++ exceptions

**Do not use C++ exceptions in DarkEngine, Sandbox, or Editor.**

### Forbidden in engine, Sandbox, and Editor code

- `try` / `catch` / `throw`
- `std::exception`, `std::runtime_error`, `std::invalid_argument`, or any exception type
- Designing APIs that “fail by throwing”
- Catch-all handlers in `main` / `WinMain` as the primary error path

`noexcept` on copy/move/assignment is **allowed** (it is not exception-based control flow).

### Required style instead

Prefer explicit, return-based failure:

| Situation | Prefer |
|-----------|--------|
| Operation can fail | `bool` return, or a small result/status type |
| Need a reason | out-param error string / enum, or log + `false` |
| D3D12 / Win32 HRESULT | check `FAILED(hr)`, log with `DE_LOG_ERROR(LogCategory::Render, ...)`, return `false` / abort init |
| Programmer bug | `DE_ASSERT` (debug break), not throw |
| Unrecoverable startup | log `DE_LOG_FATAL`, return non-zero from `WinMain` / skip frame / early `return` |

Examples:

```cpp
// Good
bool Renderer::initD3D12(Window& window)
{
    if (!window.nativeHandle())
    {
        DE_LOG_ERROR(LogCategory::Render, "Renderer: window has no HWND");
        return false;
    }
    // ...
    return true;
}

// Bad — do not write
throw std::runtime_error("Renderer: window has no HWND");
```

```cpp
// Good — Sandbox / WinMain
int WINAPI WinMain(...)
{
    DE::AppConfig cfg{};
    SandboxApp app{cfg};
    if (!app.initOk())  // or whatever success flag you use
    {
        DE_LOG_FATAL("Failed to start");
        return 1;
    }
    app.run();
    return 0;
}

// Bad — do not write
try { ... } catch (const std::exception& e) { ... }
```

### Formatting
- Prefer compact function signatures that fit on one line (see project `.clang-format`).

### Scope

| Area | Policy |
|------|--------|
| `AI/`, `Assets/`, `Audio/`, `Collision/`, `Core/`, `ECS/`, `Gameplay/`, `Geometry/`, `Math/`, `Network/`, `Render/`, `Shaders/`, `Play/`, `Sandbox/`, `Editor/`, `Weapons/` | **No exceptions** |
| `UnitTests/` | Prefer no exceptions in *our* test code; GoogleTest may use them internally — do not wrap production APIs in try/catch to “make tests work” |
| `build/_deps/` (GoogleTest, etc.) | Third-party; do not edit to remove exceptions |

### When refactoring existing code

If you touch a function that currently `throw`s, convert it to return-based errors in the same change when practical. Do not introduce **new** throws or try/catch blocks.

### Code review self-check (before finishing a task)

Search your diff for: `\btry\b`, `\bcatch\b`, `\bthrow\b`, `std::runtime_error`, `std::exception`.  
If any appear under engine, Sandbox, or Editor paths, remove them.

## Other standing conventions

- C++23, MSVC-friendly; project uses `.clang-format` (Allman braces, 4-space indent).
- Engine library target: `DarkEngine`. Shells: `Sandbox` and `Editor`. Tests: `UnitTests`. Shared behavior goes in the engine, not into both shells.
- Logging: `DE_LOG_INFO` / `WARN` / `ERROR` / `FATAL` from `Core/Log.h`. Pass `LogCategory` as the first argument (`Render`, `Audio`, `Collision`, `AI`, `Input`, `Networking`) so subsystems can be filtered with `Log::setCategoryEnabled`. Omit the category to log under `Core`. Error/Fatal still emit when a category is disabled.
- Do not reformat the whole tree unless asked; keep diffs focused.
- Build (Debug): `cmake --build build --config Debug`
- Tests: `cmake --build build --config Debug --target UnitTests` then `build\bin\Debug\UnitTests.exe`
