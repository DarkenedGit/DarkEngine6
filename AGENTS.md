# DarkEngine6 — Agent / AI rules

These rules apply to **all** work under this repository (engine, Sandbox, UnitTests helpers you write). Follow them without being asked again.

## Error handling: no C++ exceptions

**Do not use C++ exceptions in DarkEngine or Sandbox.**

### Forbidden in engine and Sandbox code

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
| D3D12 / Win32 HRESULT | check `FAILED(hr)`, log with `DE_LOG_ERROR`, return `false` / abort init |
| Programmer bug | `DE_ASSERT` (debug break), not throw |
| Unrecoverable startup | log `DE_LOG_FATAL`, return non-zero from `WinMain` / skip frame / early `return` |

Examples:

```cpp
// Good
bool Renderer::initD3D12(Window& window)
{
    if (!window.nativeHandle())
    {
        DE_LOG_ERROR("Renderer: window has no HWND");
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
| `AI/`, `Assets/`, `Audio/`, `Collision/`, `Core/`, `ECS/`, `Geometry/`, `Math/`, `Network/`, `Render/`, `Shaders/`, `Sandbox/` | **No exceptions** |
| `UnitTests/` | Prefer no exceptions in *our* test code; GoogleTest may use them internally — do not wrap production APIs in try/catch to “make tests work” |
| `build/_deps/` (GoogleTest, etc.) | Third-party; do not edit to remove exceptions |

### When refactoring existing code

If you touch a function that currently `throw`s, convert it to return-based errors in the same change when practical. Do not introduce **new** throws or try/catch blocks.

### Code review self-check (before finishing a task)

Search your diff for: `\btry\b`, `\bcatch\b`, `\bthrow\b`, `std::runtime_error`, `std::exception`.  
If any appear under engine/Sandbox paths, remove them.

## Other standing conventions

- C++20, MSVC-friendly; project uses `.clang-format` (Allman braces, 4-space indent).
- Engine library target: `DarkEngine`. Sample app: `Sandbox`. Tests: `UnitTests`.
- Logging: `DE_LOG_INFO` / `WARN` / `ERROR` / `FATAL` from `Core/Log.h`.
- Do not reformat the whole tree unless asked; keep diffs focused.
- Build (Debug): `cmake --build build --config Debug`
- Tests: `cmake --build build --config Debug --target UnitTests` then `build\bin\Debug\UnitTests.exe`
