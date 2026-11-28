# Integrating RmlUi into DarkEngine6 — work-through course

| Field | Value |
|-------|--------|
| **Audience** | DE6 / DX12 engine programmers (C++23, MSVC, no exceptions) |
| **Format** | Work-through course + checkpoints |
| **Game** | Crisper — single-player stealth/action; player-facing menus, HUD, settings, transitions, gamepad nav, localization |
| **Cross-links** | [/workspace/text-font-systems-paper.md](/workspace/text-font-systems-paper.md) (shaping, atlas, draw-UI-after-TAA) |
| **Slug** | `rmlui-de6` |
| **Verified against** | RmlUi **6.3** (`ba95ffe`, tag `6.3`, released 22 Aug 2026) |

---

## 0. Bottom line (read this first)

**Use RmlUi 6.3 (MIT) for Crisper's player-facing UI. Keep Dear ImGui for the editor and debug tools. Do not reskin ImGui into the game HUD.**

Recommended DE6 default stack:

1. **Library:** RmlUi 6.3, static link (`-DBUILD_SHARED_LIBS=OFF`), C++17 minimum (fine under DE6's C++23). Target `RmlUi::Core` (or `RmlUi::RmlUi` if you also want the debugger). Default font engine: **FreeType** (`-DRMLUI_FONT_ENGINE=freetype`).
2. **No exceptions / no language RTTI:** RmlUi 6.3 always uses its *custom* RTTI. Language RTTI is auto-detected (`RMLUI_HAS_RTTI`) and used only for debug asserts and type-name logging. The core library itself has no `throw` in `Source/` or public headers (third-party container headers can; see Module 2). CI builds with `-fno-exceptions -fno-rtti`. On MSVC, pass `/EHs- /GR-` (and typically `/D_HAS_EXCEPTIONS=0`) on both the RmlUi target and DE6. There is no CMake option named `RMLUI_CUSTOM_RTTI` anymore — it was removed in 6.3.
3. **Renderer:** implement a DE6 `Rml::RenderInterface` that uses **DE6's device, queue, and command list**. An official, fully featured Direct3D 12 backend shipped in 6.3 (`Win32_DX12`, `SDL_DX12`, `GLFW_DX12`; `Backends/RmlUi_Renderer_DX12.*`). It is a good reference for PSOs, layers, filters, and shaders. It is **not** a drop-in for an existing engine: the public constructor takes a window handle and creates its own device, queues, and swapchain. `Set_UserRenderTarget` / `Set_UserDepthStencil` / `Set_UserFramebufferIndex` exist, but I did not find a documented "pass your `ID3D12Device`" constructor. Start with the *basic* RenderInterface functions (geometry, textures, scissor). Add clip-mask, transforms, layers, filters, and shaders when menus actually need them.
4. **Player UI features:** RCSS + `@keyframes` / `transition` for animated menus; `tab-index` + `nav: auto` (and `nav-up` / `nav-right` / `nav-down` / `nav-left`) for gamepad spatial navigation; data models for HUD values; `SystemInterface::TranslateString` for localization. Default FreeType does **not** shape complex scripts or do real RTL joining. HarfBuzz lives in the **`harfbuzz` sample** (`Samples/basic/harfbuzz`, `-DRMLUI_HARFBUZZ_SAMPLE=ON`), not as a core `RMLUI_FONT_ENGINE` option. Copy that sample's `FontEngineInterfaceHarfBuzz` if you need Arabic or other shaped scripts.
5. **Coexistence with ImGui:** two UIs, two descriptor heaps (or two persistent ranges of one heap). Render order: game world (after TAA) → RmlUi (player HUD / menus) → ImGui (editor / debug) on top. Input: when the ImGui overlay wants the mouse or keyboard, it wins; otherwise RmlUi; leftover goes to gameplay. Do not put ImGui's 1.92 font/texture descriptors in a heap that is reset every frame.

**Weekend goal:** RmlUi draws one styled, gamepad-navigable menu in DE6, with ImGui still able to open on top and neither UI stealing the other's input or descriptors.

---

## Module 1 — Problem framing: game UI vs tools UI

Crisper needs screens that *layout themselves* when language, resolution, or focus changes: pause / settings, inventory or loadout, stealth HUD (suspicion, nearby-awareness, prompts), controller focus rings, and short animated transitions. That is a retained-mode, stylesheet problem.

Dear ImGui is already the right tool for DE6's editor, debug overlays, and in-engine inspectors. Immediate-mode widgets are weak at CSS-like reflow, animated transitions, and controller spatial navigation unless you build those systems yourself. A reskin can look good for a dense, menu-heavy game (that is a real path; it is just more work than it first appears). For Crisper, spend that time on an RmlUi renderer and markup, not on reinventing layout inside ImGui.

### Vocabulary

| Term | Meaning here |
|------|----------------|
| **RML** | RmlUi's HTML-like markup. Documents are trees of `Rml::Element` / `Rml::ElementDocument`. |
| **RCSS** | RmlUi's CSS-like style language. Not a full browser engine; see the [property index](https://mikke89.github.io/RmlUiDoc/pages/rcss/property_index.html) for differences. |
| **Context** | `Rml::Context` — one UI "window": documents, focus, hover, update, render. DE6 wants one player context. |
| **RenderInterface** | Your D3D12 backend. RmlUi compiles geometry and asks you to draw it. |
| **Retained mode** | You load documents and bind data; RmlUi dirty-checks and relayouts. Opposite of ImGui's per-frame widget calls. |
| **dp-ratio** | `Context::SetDensityIndependentPixelRatio` — scales the `dp` unit and sprite natural size for HUD DPI / resolution. |

### Comparison (player-facing game UI)

| | **RmlUi 6.3** | **Reskinned Dear ImGui** | **Clay** | **Nuklear** | **NoesisGUI** |
|---|---|---|---|---|---|
| License | MIT | MIT | zlib/libpng | MIT or public domain (dual) | Commercial ([licensing](https://www.noesisengine.com/licensing.php); an indie tier exists) |
| Mode | Retained, HTML/CSS-like | Immediate | Immediate *layout only* — you draw | Immediate, ImGui-like | Retained, XAML / WPF-like |
| Animations / transitions | First-class RCSS `animation` / `transition` | You build them | No | You build them | First-class |
| Gamepad / spatial nav | `tab-index`, `nav`, `:focus-visible` | Possible, a lot of custom work | You build it | Limited / custom | First-class |
| Localization reflow | Layout engine + `TranslateString`; shaping optional | Manual; text-length changes hurt | Manual | Manual | Strong |
| D3D12 | Official sample backend in 6.3 (owns its device) | Official backend; 1.91.6+ needs a command queue + SRV alloc callbacks | You write the draw | You write the draw | Official integrations, not OSS |
| Fits Crisper player UI | **Yes — default** | Only if you accept building layout/nav/animation on top | Only for a tiny custom HUD | Same class of tradeoff as ImGui | Excellent if you pay and accept XAML |
| Fits DE6 editor / debug | No (keep ImGui) | **Yes — keep it** | No | Possible, worse ecosystem | Overkill |

### Shipped games and products (only what I could verify)

Named in the official [RmlUi 6.3 README gallery](https://github.com/mikke89/RmlUi/blob/6.3/readme.md):

- [The Thing: Remastered](https://nightdivestudios.com/the-thing-remastered/) (Nightdive) — "User interface made with RmlUi." Also mentioned in the [6.1 release notes](https://github.com/mikke89/RmlUi/releases/tag/6.1).
- [Killing Time: Resurrected](https://nightdivestudios.com/killing-time-resurrected/) (Nightdive) — same, plus the 6.1 notes.
- [ROSE Online](https://www.roseonlinegame.com/) — official README, and the game's own [16 Oct 2026 patch notes](https://www.roseonlinegame.com/news?page=1) announce a "full RmlUI migration."
- [Unvanquished](https://unvanquished.net/) — "Menus and HUD in RmlUi."
- [vkQuake + RmlUi](https://github.com/bradenleague/vkQuake-RmlUi) — a fork that replaces Quake's menu and HUD.
- [TruckersMP](https://truckersmp.com/) — chat box and player panel; a TruckersMP developer also posted the integration in [discussion #186](https://github.com/mikke89/RmlUi/discussions/186).

The same gallery lists non-game apps (LichtFeld Studio, alt:V installer, and others). I am not treating those as shipped games.

### Checkpoint 1

Be able to say, in one sentence, why Crisper's pause menu is an RmlUi document and why the DE6 material editor stays ImGui.

---

## Module 2 — Version, license, and a no-exceptions MSVC build

### What you are integrating

- **Version:** 6.3. `project(RmlUi VERSION "6.3")` in the repo `CMakeLists.txt`. `Rml::GetVersion()` returns `RMLUI_VERSION`.
- **License:** MIT (`LICENSE.txt`). Copyright 2008–2014 CodePoint Ltd, Shift Technology Ltd, and contributors; 2019–2026 The RmlUi Team and contributors.
- **Third-party in core:** bundled containers (MIT), optional. The official DX12 backend additionally vendors [D3D12MA](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator) (MIT, AMD) and OffsetAllocator (MIT) under `Backends/RmlUi_DirectX/`.
- **Language:** C++17 minimum (breaking change in 6.3). DE6's C++23 is a superset.
- **Dependencies:** FreeType when `RMLUI_FONT_ENGINE=freetype` (the default). CMake warns that **FreeType 2.11.0 + MSVC can cause issues**; use 2.11.1 or newer. Lua, Lottie (`rlottie`), SVG (`lunasvg`), and HarfBuzz are optional and off by default.

### CMake for DE6

Preferred: add RmlUi as a subdirectory or consume an install via `find_package`. The 6.2 CMake rewrite explicitly supports add-subdirectory, in-source use, install, and prebuilt Windows binaries. When used via `add_subdirectory`, RmlUi skips installing its targets into your prefix.

```cmake
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "" FORCE)
set(RMLUI_THIRDPARTY_CONTAINERS ON CACHE BOOL "" FORCE)  # see exceptions note below
add_subdirectory(third_party/RmlUi)

target_link_libraries(DarkEngine6 PRIVATE RmlUi::Core)
# During bring-up, also: RmlUi::Debugger
```

From a CMake client that consumed an install or build tree:

```cmake
find_package(RmlUi REQUIRED)
target_link_libraries(my_application PRIVATE RmlUi::RmlUi)
# or RmlUi::Core + RmlUi::Debugger
# Point CMake at the tree with -DRmlUi_ROOT=<path>
```

Useful options (defaults as of 6.3):

| Option | Default | Notes |
|--------|---------|--------|
| `BUILD_SHARED_LIBS` | **ON** | Prefer **OFF** for DE6. If you link without the exported CMake targets, define `RMLUI_STATIC_LIB`. |
| `RMLUI_FONT_ENGINE` | `freetype` | Other legal value: `none` (you must then install a `FontEngineInterface`). |
| `RMLUI_SAMPLES` | OFF | Turn on locally to run `invaders`, `animation`, `data_binding`, `harfbuzz`. |
| `RMLUI_BACKEND` | `auto` | Sample backends only. `Win32_DX12` is on the list. You will not ship the sample backend as-is. |
| `RMLUI_COMPILER_OPTIONS` | ON | MSVC: `/W4 /w44062 /permissive-`, `/MP` under Visual Studio. Does **not** set exception or RTTI flags. |
| `RMLUI_THIRDPARTY_CONTAINERS` | ON | robin_hood / itlib. Faster, but those headers contain `throw` paths. |
| `RMLUI_HARFBUZZ_SAMPLE` | OFF | Builds the HarfBuzz font-engine sample. Requires FreeType + HarfBuzz. |
| `RMLUI_TRACY_PROFILING` | OFF | Optional. Leave off until you want it. |
| `RMLUI_CUSTOM_CONFIGURATION` | OFF | Only if you replace allocators / containers (e.g. EASTL). |

vcpkg (`vcpkg install rmlui`) and Conan work; the Conan option `enable_rtti_and_exceptions` still documents a relationship to the *removed* `RMLUI_CUSTOM_RTTI` flag, so treat that Conan option as stale relative to 6.3 and configure compiler flags yourself.

### Exceptions and RTTI, precisely

- **6.3 changelog:** custom RTTI is always enabled. Language RTTI is detected automatically. The CMake option `RMLUI_CUSTOM_RTTI` was **removed**.
- **Detection:** `Include/RmlUi/Core/Platform.h` sets `RMLUI_HAS_RTTI` from `__has_feature(cxx_rtti)`, `__GXX_RTTI`, or `_CPPRTTI`. Without it, `rmlui_static_cast` skips the `dynamic_cast` assert, and `rmlui_type_name` returns `"(type name unavailable)"`.
- **Does the library throw?** In discussion [#769](https://github.com/mikke89/RmlUi/discussions/769) (May 2025) the maintainer said they do not use exceptions in the library. I confirmed there is no `throw` in `Source/` or first-party public headers of 6.3. Bundled `robin_hood.h` and `itlib/flat_map.hpp` *do* contain `throw` (overflow / `at()` out of range). CI includes a job with `-DCMAKE_CXX_FLAGS="-fno-exceptions -fno-rtti"` (Linux, `SDL_GL2` backend, samples-all preset).
- **MSVC:** I did **not** run an MSVC `/EHs- /GR-` build in this session. The portable path is: compile RmlUi and DE6 with the same no-exception / no-RTTI flags; if a third-party container `throw` path fails to compile, either set `RMLUI_THIRDPARTY_CONTAINERS=OFF` or keep the containers and ensure those paths are never taken (not a guarantee — prefer OFF if the compile fails).
- Do not enable C++ exceptions in DE6 "just for RmlUi." You should not need to.

### Exercise 2 — build and version

1. Vendor RmlUi 6.3 (or add it as a submodule pinned at tag `6.3`).
2. Configure a static, no-samples, FreeType build with your no-exception flags.
3. Link `RmlUi::Core`, call `Rml::GetVersion()`, and log it. Expected string is the 6.3 version macro, i.e. `6.3`.
4. Optionally build with `-DRMLUI_SAMPLES=ON -DRMLUI_BACKEND=Win32_DX12` *outside* DE6 and run the `invaders` and `animation` samples so you have a visual baseline.

### Checkpoint 2

You can point at a built `rmlui_core` static lib, you know the MIT license obligations, and you know that 6.3 has no `RMLUI_CUSTOM_RTTI` knob.

---

## Module 3 — Interfaces, lifetime, and init order

Official order is in the [initialization and main loop](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/main_loop.html) doc. Interfaces are **non-owning** and must stay alive until after `Rml::Shutdown()`.

```cpp
// DE6-style: no exceptions. Every failure is a bool / early return.
bool Ui_Startup(De6RmlRenderInterface* render, De6RmlSystemInterface* system,
                De6RmlFileInterface* files, int width, int height)
{
    if (!render || !system || !files)
        return false;

    Rml::SetRenderInterface(render);
    Rml::SetSystemInterface(system);
    Rml::SetFileInterface(files);
    // Optional, before Initialise():
    // Rml::SetFontEngineInterface(&g_harfbuzz_engine);
    // Rml::SetTextInputHandler(&g_ime_handler);

    if (!Rml::Initialise())
        return false;

    Rml::Context* ctx = Rml::CreateContext("player", Rml::Vector2i{width, height});
    if (!ctx)
    {
        Rml::Shutdown();
        return false;
    }

    Rml::Debugger::Initialise(ctx); // bring-up only; strip from ship builds if you want

    if (!Rml::LoadFontFace("ui/fonts/YourSans-Regular.ttf"))
    {
        Rml::Shutdown();
        return false;
    }
    // Optional fallback face for missing glyphs (e.g. CJK / symbols):
    // Rml::LoadFontFace("ui/fonts/NotoSans-Regular.ttf", /*fallback_face=*/true);

    Rml::ElementDocument* doc = ctx->LoadDocument("ui/menus/pause.rml");
    if (!doc)
    {
        Rml::Shutdown();
        return false;
    }
    doc->Show();
    return true;
}

void Ui_Shutdown()
{
    Rml::Shutdown();
    // Only now destroy render / system / file / font interfaces.
}
```

There is no default font. Load at least one face before any document that contains text. Faces can also be declared in RCSS with `@font-face` (added in 6.3; it wraps `Rml::LoadFontFace`).

`CreateContext` can take a per-context `RenderInterface*`. The default is the one passed to `SetRenderInterface`. Keep a single player context named something stable (`"player"`).

### SystemInterface

Declared in `Include/RmlUi/Core/SystemInterface.h`. All methods have defaults.

| Method | Default / what DE6 should do |
|--------|------------------------------|
| `GetElapsedTime()` | `std::chrono::steady_clock` since first call. Prefer hooking DE6's clock so animations and the game share a time base. |
| `TranslateString(translated, input)` | Pass-through, returns `0`. Override for localization (Module 9). |
| `JoinPath(...)` | Usually leave the default. |
| `LogMessage(type, message)` | Forward to DE6's log. Return `true` to continue; `false` breaks into a debugger. |
| `SetMouseCursor(name)` | Map RCSS `cursor` plus built-in names during autoscroll. Optional if you draw your own cursor. |
| `SetClipboardText` / `GetClipboardText` | Default is in-process only. Hook the Win32 clipboard for settings / text fields. Text is UTF-8. |
| `ActivateKeyboard` / `DeactivateKeyboard` | Virtual keyboard / IME caret hint. Relevant if you ever ship touch or a heavy IME path. |

Enable cursor callbacks per context with `Context::EnableMouseCursor(true)`.

### FileInterface

Declared in `Include/RmlUi/Core/FileInterface.h`. The default uses the C file API and accepts absolute paths or paths relative to the working directory. **DE6 should implement this** and route RML, RCSS, images, and fonts through the engine VFS (same logical paths you will later hot-reload).

Required: `Open`, `Close`, `Read`, `Seek`, `Tell`. Optional overrides: `Length`, `LoadFile`.

### Font engine

Default: built-in FreeType engine (`RMLUI_FONT_ENGINE=freetype`). That path rasterizes glyphs, does simple kerning, and honors `letter-spacing`. It does not run a real OpenType shaper (see Module 9 and the text/font paper).

Font *effects* are an RCSS feature, not a separate C++ install: `font-effect: outline(2px black);` and the built-in types `glow`, `outline`, `shadow`, `blur`. They are inherited. Override with `font-effect: none` on children that should not inherit. Effects generate extra glyph textures via `GenerateTexture`.

If you replace the font engine, implement `Rml::FontEngineInterface` and call `SetFontEngineInterface` *before* `Initialise()`. The HarfBuzz sample is the reference for that.

### Checkpoint 3

You can recite the lifetime rule: set interfaces → `Initialise` → create context → load fonts → load documents → … → `Shutdown` → destroy interfaces. You have a FileInterface pointing at `ui/`.

---

## Module 4 — RenderInterface and D3D12

This is the real integration cost.

### Feature table (from the official docs)

The [render interface](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces/render.html) groups functions into features. Properties silently degrade if you skip the later ones.

| Feature | Functions (6.3) | Required by |
|---------|-----------------|-------------|
| **Basic (required)** | `CompileGeometry`, `RenderGeometry`, `ReleaseGeometry`, `LoadTexture`, `GenerateTexture`, `ReleaseTexture`, `EnableScissorRegion`, `SetScissorRegion` | Everything: boxes, images, text, basic decorators |
| **Clip mask** | `EnableClipMask`, `RenderToClipMask` | `transform` / `perspective`; `border-radius` + overflow; `box-shadow` |
| **Transforms** | `SetTransform(const Matrix4f*)` — `nullptr` means identity | `transform`, `perspective` |
| **Layers** | `PushLayer`, `CompositeLayers`, `PopLayer` | `filter`, `backdrop-filter`, `mask-image`, `box-shadow` |
| **Render textures** | `SaveLayerAsTexture` | `box-shadow` |
| **Mask images** | `SaveLayerAsMaskImage` | `mask-image` |
| **Filters** | `CompileFilter`, `ReleaseFilter` (applied in `CompositeLayers`) | `filter`, `backdrop-filter`, blurred `box-shadow` |
| **Shaders** | `CompileShader`, `RenderShader`, `ReleaseShader` | `shader`, `linear-gradient`, `radial-gradient`, `conic-gradient`, and `repeating-` variants |

**Weekend scope:** implement Basic + `SetTransform` + scissor correctly. Menus, HUD text, image / ninepatch / tiled decorators, and `horizontal-gradient` / `vertical-gradient` work with only that. Add clip-mask when you start rotating or rounding clipped panels. Add layers/filters/shaders when you want blur, box-shadow, backdrop-filter, or CSS-like gradients.

### Rendering conventions (do not improvise these)

From the official render-interface docs, verified against `Include/RmlUi/Core/Vertex.h`:

- Vertex positions are in **pixels**. Document origin is the **top-left** of the window.
- Indices are **counter-clockwise** triangles.
- **Generated** textures have their origin at the **bottom-left** (GL convention). File textures follow whatever you loaded; be consistent in the shader.
- Vertex color is `ColourbPremultiplied` — 8-bit RGBA, **sRGB, premultiplied alpha**. Generated textures are also premul sRGB.
- Multiply sampled texture color by the vertex color.
- Enable alpha blending. For premultiplied sources the correct blend is typically **`Src=ONE`, `Dest=INV_SRC_ALPHA`** (plus the matching alpha blend). Using `SRC_ALPHA` / `INV_SRC_ALPHA` on already-premultiplied data darkens fringes.
- `SetScissorRegion` is in **window coordinates**, regardless of the active transform.
- `CompileGeometry`: the `Span` vertex and index data stay valid and immutable until `ReleaseGeometry` for that handle. You may keep pointers or copy into a GPU buffer; do not assume the span dies at the end of the call.

`Rml::Vertex`:

```cpp
struct Vertex {
    Vector2f position;               // pixels
    ColourbPremultiplied colour;     // premul sRGB RGBA8
    Vector2f tex_coord;
};
```

### Official Direct3D 12 backend (verified present)

Shipped in 6.3 as a "fully-featured" renderer (changelog; contributed in PR #648, with later work in #975):

- Backends: `RmlUi_Backend_Win32_DX12.cpp`, `RmlUi_Backend_SDL_DX12.cpp`, `RmlUi_Backend_GLFW_DX12.cpp`
- Renderer: `Backends/RmlUi_Renderer_DX12.h` / `.cpp` (~700 / ~10,086 lines)
- Implements the full interface, including layers, filters, and shaders.

I opened `CompileFilter` / `CompileShader` in that renderer. Filter names it handles: `opacity`, `blur`, `drop-shadow`, `brightness`, `contrast`, `invert`, `grayscale`, `sepia`, `hue-rotate`, `saturate`. Shader names: `linear-gradient`, `radial-gradient`, `conic-gradient`, `shader`.

Resource policy inside that sample renderer (defaults in `RmlUi_Include_DirectX_12.h`):

| Macro | Default | Role |
|-------|---------|------|
| `RMLUI_RENDER_BACKEND_FIELD_SWAPCHAIN_BACKBUFFER_COUNT` | 3 | Frames in flight; constant buffers and deferred deletes are per-frame |
| `RMLUI_RENDER_BACKEND_FIELD_VIDEOMEMORY_FOR_BUFFER_ALLOCATION` | 2 MiB | Vertex / index / CBV pool chunk |
| `RMLUI_RENDER_BACKEND_FIELD_VIDEOMEMORY_FOR_TEXTURE_ALLOCATION` | 4 MiB | Texture pool chunk |
| `RMLUI_RENDER_BACKEND_FIELD_DESCRIPTORAMOUNT_FOR_SRV_CBV_UAV` | 128 | Size of the renderer's own CBV_SRV_UAV heap |
| `RMLUI_RENDER_BACKEND_FIELD_PREALLOCATED_CONSTANTBUFFERS` | 512 | CBV budget |
| `RMLUI_RENDER_BACKEND_FIELD_STAGING_BUFFER_SIZE` | 8 MiB | Upload staging |

Geometry and texture *releases* are deferred until a fence value from the allocating frame has passed — do the same in DE6 or you will get use-after-free on the GPU.

`BeginFrame` / `EndFrame` / `SetViewport` wrap RmlUi's `Context::Render()`. `NumPrograms = 23` root signatures / PSOs in the header.

**Limitation for DE6:** `RenderInterface_DX12(void* p_window_handle, const Backend::RmlRendererSettings& settings)` initializes a device (`D3D12CreateDevice`, feature level 11_0), a command queue, a swapchain, allocators (D3D12MA), and its own descriptor heaps. That is correct for the samples and wrong as a drop-in into an engine that already owns those objects. Treat this backend as the **reference implementation** to read, and as a source of HLSL / PSO ideas, not as a library you construct with a HWND and forget.

### A DE6-shaped D3D12 implementation

Own these in the engine, not inside RmlUi:

- One **persistent** CBV_SRV_UAV heap (or a reserved range) for RmlUi textures and font atlases. This heap is *not* reset each frame. See Module 5 for why ImGui needs the same treatment independently.
- An upload ring (or the official backend's staging-buffer idea) for `CompileGeometry` and `GenerateTexture` / `LoadTexture`.
- A small PSO set: untextured triangles, textured triangles, (later) clip-mask via stencil, (later) filter / gradient programs.
- A root signature with a transform CBV and one SRV + sampler. Column-major is RmlUi's default (`RMLUI_MATRIX_ROW_MAJOR` is OFF). If you flip that option, every client must define the same macro.

Conceptual root signature and shaders (basic path only):

```text
Root signature (graphics):
  [0] CBV b0  : float4x4 transform   // projection * (SetTransform or I) * translation
  [1] SRV t0  : texture              // or a bindless index if you already have one
  static sampler s0 : linear clamp (and a point sampler later, if you want crisp pixels)
```

```hlsl
// RmlUi.hlsl — basic premul path. Compile with DXC, vs_6_0 / ps_6_0.
cbuffer RmlTransform : register(b0) {
    float4x4 g_Transform; // column-major; vertex in pixel units, top-left origin
};

struct VSIn {
    float2 position  : POSITION;
    float4 color     : COLOR;    // already premultiplied, 0–1
    float2 texcoord  : TEXCOORD0;
};
struct VSOut {
    float4 position : SV_Position;
    float4 color    : COLOR;
    float2 texcoord : TEXCOORD0;
};

VSOut VSMain(VSIn v)
{
    VSOut o;
    o.position = mul(g_Transform, float4(v.position, 0.0, 1.0));
    o.color = v.color;
    o.texcoord = v.texcoord; // generated textures may need a V flip on the CPU or here
    return o;
}

Texture2D    g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

float4 PSTextured(VSOut i) : SV_Target
{
    float4 tex = g_Texture.Sample(g_Sampler, i.texcoord);
    return tex * i.color; // docs: multiply fetched color by vertex color
}

float4 PSUntextured(VSOut i) : SV_Target
{
    return i.color;
}
```

Projection for a `width × height` context, top-left origin, Y down:

\[
P = \begin{pmatrix}
2/w & 0 & 0 & -1 \\
0 & -2/h & 0 & 1 \\
0 & 0 & 1 & 0 \\
0 & 0 & 0 & 1
\end{pmatrix}
\]

(If your clip-space Y is already flipped, adjust the signs; the invariant is that RmlUi pixel `(0,0)` is the top-left of the context and `(w,h)` is the bottom-right.)

`RenderGeometry(geometry, translation, texture)` should build a 2D translation matrix, combine it with the last `SetTransform` (or identity) and \(P\), bind the texture handle (zero means untextured), and draw the compiled index range. `TextureHandle` is an application-defined integer; zero is "no texture."

`LoadTexture` receives a path already joined by the file interface (document-relative). Decode to RGBA, **premultiply** on the CPU if the file is straight alpha, upload as `R8G8B8A8_UNORM_SRGB` (or the DE6 UI format, but stay in sRGB). `GenerateTexture` is already premul RGBA8.

Scissor: `RSSetScissorRects` in window pixels. Disable by setting a full-viewport rect, not by forgetting to re-enable.

### Filters later

When you add `CompileFilter`, the official GL3/DX12 backends are the contract: `opacity` (`value`), `blur` (`sigma`), `drop-shadow` (`sigma`, `color`, `offset`), and the color-matrix group (`brightness`, `contrast`, `invert`, `grayscale`, `sepia`, `hue-rotate`, `saturate`). Layers are offscreen render targets; `CompositeLayers` samples a source layer into a destination with an optional filter chain. MSAA on layer targets, no MSAA on postprocess targets, matches the official `RenderLayerStack` comments.

### Exercise 4

Implement the eight basic functions plus `SetTransform`. Clear to a debug color, `LoadDocumentFromMemory` a 200×80 pink `<div>`, and confirm it composites over the game (after TAA). Then load a PNG through `LoadTexture` via an `<img>`.

### Checkpoint 4

You know which RenderInterface functions are required this weekend, that the official DX12 backend exists and is feature-complete but owns its device, and that RmlUi colors are premul sRGB.

---

## Module 5 — One-frame walk, render order, and ImGui coexistence

Official loop (abridged from the docs): submit input → update game → dirty any bound data → `context->Update()` → render game → begin UI render state → `context->Render()` → present. After `Update()`, do not mutate elements or submit input until after `Render()`.

### One frame in DE6

```text
input devices
  ├─ if ImGui overlay visible and (WantCaptureKeyboard or WantCaptureMouse):
  │     feed those events to ImGui only
  └─ else:
        feed to Rml::Context::Process*
        if ProcessKey* / ProcessMouseWheel returned true (still propagating)
           and not Context::IsMouseInteracting():
             feed leftover to gameplay

game simulation
HUD / menu data → DataModelHandle::DirtyVariable(...)   // Module 8
context->Update()     // layout, animations, data views, deferred document destroy

game world render
  … TAA / upscale …          // UI after TAA (see text-font paper)
begin RmlUi state: PSO, persistent SRV heap, scissor, blend premul
  render->BeginFrame(width, height)   // your wrapper
  context->Render()
  render->EndFrame()
begin ImGui state: ImGui's own heap / allocators
  ImGui_ImplDX12_NewFrame / RenderDrawData   // editor + debug
present
```

`Context::GetNextUpdateDelay()` supports on-demand rendering: `0` means "an animation is running, render now"; infinity means "wait for input." A 60 Hz game loop can ignore this and always call `Update` + `Render`. It is useful for editor-idle or a paused frontend.

`Context::SetDimensions` and `SetDensityIndependentPixelRatio` belong in the resize / DPI path, *before* `Update`.

### Input routing details

Return values (from the 6.3 headers, and from `EventDispatcher` returning `event->IsPropagating()`):

| Call | `true` means | `false` means |
|------|----------------|---------------|
| `ProcessKeyDown` / `ProcessKeyUp` / `ProcessTextInput` / `ProcessMouseWheel(Vector2f, …)` | Event was **not** consumed; it is still propagating | Consumed (`StopPropagation` / default action ate it) |
| `ProcessMouseMove` / `ButtonDown` / `ButtonUp` / `ProcessMouseLeave` | Mouse is **not** interacting with any element | Hovering or holding an element (see `IsMouseInteracting`) |

The header comment's parenthetical on "consumed" is easy to misread. Trust the first clause and the implementation: **`true` = not consumed / still propagating.** If `ProcessKeyDown` returns `false`, do not also apply that key to the player character.

`IsMouseInteracting()` is true if the mouse hovers or has activated an element, irrespective of background and opacity. Use RCSS `pointer-events: none` on full-screen passthrough HUD roots so a stealth HUD does not steal clicks from the game. A modal pause document (`ElementDocument::Show(ModalFlag::Modal, …)`) should take all UI input.

Recommended policy:

| Mode | Keyboard / gamepad | Mouse |
|------|--------------------|-------|
| Gameplay, HUD only | Game first for actions; RmlUi gets only what the HUD asks for (or RmlUi first if a HUD control is focused) | If `IsMouseInteracting()`, RmlUi only; else game |
| Pause / settings / inventory | RmlUi only (modal document) | RmlUi only |
| Editor / debug ImGui visible | ImGui if `WantCaptureKeyboard`, else the row above | ImGui if `WantCaptureMouse`, else the row above |

There is no `ProcessGamepad` API. See Module 6.

### Render order

Draw player UI after TAA (and after any temporal upscale) on an unjittered projection, or glyphs crawl and soften. ImGui debug should be on top of RmlUi so inspectors remain usable over a menu. If you ever offer an in-game ImGui console, keep it on the same top layer.

Do not share a command list state blindly: after `context->Render()`, RmlUi's interface may have left a scissor, stencil clip-mask, or bound PSO. Your `EndFrame` must restore or fully rebind what the ImGui backend and the present path need.

### Descriptor heaps — keep them separate

This is the easy way to get a hard-to-debug GPU hang.

- **RmlUi (official DX12 backend):** owns a CBV_SRV_UAV heap of **128** descriptors by default and an offset allocator. Font atlases and document textures live there for the life of the handle, not for one frame.
- **Dear ImGui 1.91.6+:** `ImGui_ImplDX12_InitInfo` requires a `CommandQueue` plus `SrvDescriptorAllocFn` / `SrvDescriptorFreeFn` (your previous ImGui research). **1.92 dynamic fonts** (`RendererHasTextures`) can need several shader-resource descriptors that stay valid for the life of the app. They cannot live in a heap that is reset each frame (ImGui issue #8511).

DE6 rule:

1. Persistent heap A (or range A) — RmlUi SRVs / CBVs.
2. Persistent heap B (or range B) — ImGui font/texture SRVs, allocated/freed via the 1.91.6 callbacks.
3. A per-frame scratch heap is fine for *other* engine work, not for either UI's long-lived SRVs.
4. Bind the correct heap before each UI's render. If you use a single GPU-visible heap, reserve disjoint ranges and never recycle an RmlUi or ImGui descriptor until that UI releases it.

### Exercise 5

With a pause menu open and the ImGui debugger (or a demo window) toggled on a key, confirm: (1) click on ImGui does not click menu buttons, (2) click on the menu does not click through to the game, (3) click on empty world (menu closed, `pointer-events: none` HUD) reaches gameplay, (4) PIX/debug layer does not complain about descriptor heaps during the RmlUi → ImGui handoff.

### Checkpoint 5

You can walk one frame out loud, including which API return value means "consumed," and you have two persistent descriptor ranges.

---

## Module 6 — Input mapping, gamepad, and spatial navigation

RmlUi consumes keyboard-like identifiers, not XInput directly. `Include/RmlUi/Core/Input.h` lists `Input::KeyIdentifier` (`KI_UP`, `KI_DOWN`, `KI_LEFT`, `KI_RIGHT`, `KI_RETURN`, `KI_ESCAPE`, `KI_TAB`, …) and `KeyModifier` bit flags. Custom keys occupy `KI_FIRST_CUSTOM_KEY` … `KI_LAST_CUSTOM_KEY` (250).

Map a typical controller once, in DE6 input code:

| Gamepad | RmlUi |
|---------|--------|
| D-pad and left stick (with a deadzone + repeat timer) | `KI_UP` / `DOWN` / `LEFT` / `RIGHT` via `ProcessKeyDown` / `ProcessKeyUp` |
| South button (A / Cross) | `KI_RETURN` (activate) |
| East button (B / Circle) | `KI_ESCAPE` (cancel / close) — also handle in a document `keydown` listener if you want "B closes the menu" |
| Start | toggle pause document `Show` / `Hide` |
| Shoulder buttons | optional: `KI_TAB` / Shift+Tab, or `nav` to specific ids |
| Mouse / right stick | `ProcessMouseMove` if you support a virtual cursor; otherwise leave mouse alone in gamepad mode |

Stick repeat is your job: send `ProcessKeyDown` on crossing the deadzone, then repeated downs at a sensible interval, and `ProcessKeyUp` on return to center. RmlUi will not do this for you.

### RCSS navigation (official user-interface docs)

Only **tabbable** elements are navigation sources and targets. Make a control tabbable with `tab-index: auto`. `tab-index: none` (the default for most elements) leaves it out of the tab order.

```css
body { nav: auto; }           /* recommended whenever the document uses spatial nav */
button, .row-item, input, select {
    tab-index: auto;
    nav: auto;                /* or nav-up / nav-right / nav-down / nav-left */
}
button:focus-visible {
    decorator: image( focus-ring );
    /* or a border / outline-equivalent you already style */
}
```

`nav-up`, `nav-right`, `nav-down`, `nav-left` values:

| Value | Behavior |
|-------|----------|
| `none` | No automatic move in that direction |
| `auto` | Nearest tabbable element in that screen direction. **Restricted to the same scroll container.** |
| `tree-order` | **New in 6.3.** Nearest tabbable in document tree order (forward / backward). Crosses scroll containers. Does **not** wrap to the root (tabbing can). |
| `#id` | Jump to that element |

Shorthand `nav`: `none`, `auto`, `horizontal`, `vertical`, `tree-order`, or a four-edge box (`nav: tree-order none` → vertical tree-order, horizontal off).

Always set `nav: auto` on `body` so focus can be recovered after a mouse click or a `blur`. 6.3 also introduced `ScrollAlignment::Adaptive` (do not scroll if the target is already visible; otherwise center it) and `ScrollFlag` on `ElementDocument::Show` to control whether showing a document scrolls the focused element into view.

`:focus` is set on the focused element and, unlike CSS, **propagates to ancestors**. `:focus-visible` is set when focus came from a navigation action — use it for the gamepad focus ring so a mouse click does not necessarily paint the same ring.

`focus: none` prevents an element (e.g. a disabled button) from taking focus and also blocks click events originating from user input.

`Show(ModalFlag::Modal, FocusFlag::Auto)` focuses the first `autofocus` tab element, or the document, and blocks other documents from receiving focus — the right default for pause / settings.

### Exercise 6

Build a two-column settings list. Confirm D-pad moves in screen space (`nav: auto`), that a vertical list inside a scroll box does not jump to a control outside the box, and that `nav-down: #apply` on the last item jumps to the Apply button. Style `:focus-visible` only.

### Checkpoint 6

You can explain why `nav: auto` on `body` matters, and you have a stick-repeat timer in engine code (not in RCSS).

---

## Module 7 — RCSS, motion, decorators, and sprites

Write layout and look in RCSS, not in C++. That is the point of choosing RmlUi over a reskinned ImGui.

### Motion

[Animations, transitions, and transforms](https://mikke89.github.io/RmlUiDoc/pages/rcss/animations_transitions_transforms.html):

- Most property types animate, including numbers, colors, keywords, transforms, decorators, and filters. **Box shadows cannot currently be animated.**
- `animation: 2s cubic-in-out infinite alternate my-progress-bar;` plus `@keyframes`. Duration is required and must appear before delay. Tweens are `name-in` / `name-out` / `name-in-out` with names `back`, `bounce`, `circular`, `cubic`, `elastic`, `exponential`, `linear`, `quadratic`, `quartic`, `quintic`, `sine`. Default tween: `linear-in-out`. This is *not* CSS `ease-in-out` naming.
- Animations write the element's local style. Do not mix inline `style=""` attributes and animations on the same element.
- **Transitions in RCSS only run when a class or pseudo-class is added or removed** — a documented difference from CSS. `transition: padding-left background-color transform 1.6s elastic-out;` then change `:hover` or `element->SetClass("open", true)`.
- Transforms: `transform`, `transform-origin`, `perspective`, `perspective-origin`, with the usual CSS function set (`translate`, `scale`, `rotate`, `matrix`, `*3d`, …). **Transforms do not affect clipping.** `overflow: hidden` will not clip transformed children unless they also overflow untransformed. Force clipping with `clip: always` plus `overflow: hidden`. Clip-mask support in your renderer is required for transformed clipping and rounded overflow.

C++ can start animations too (see the C++ animation API in the docs) if a transition must be triggered from gameplay rather than a class toggle.

### Decorators and sprites

Built-in decorator types: `image`, `tiled-horizontal`, `tiled-vertical`, `tiled-box`, `ninepatch`, `horizontal-gradient`, `vertical-gradient`, `text`, plus shader-backed `linear-gradient` / `radial-gradient` / `conic-gradient` (and `repeating-` variants) and `shader`. Declare them in stylesheets, not inline, for performance.

```css
@spritesheet hud-icons {
    src: ui/hud/icons.png;
    resolution: 1x;
    icon-stealth: 0px 0px 32px 32px;
    icon-alert:  32px 0px 32px 32px;
}
.prompt-icon { decorator: image( icon-stealth ); }
```

Sprite rectangles are `x y width height` in `px`, origin top-left of the image. Names are global; later sheets override. Pair with `@media (min-resolution: 2x)` and a `resolution: 2x` sheet so `dp-ratio` swaps in a retina atlas. `<img sprite="icon-stealth"/>` also works.

`@decorator` exists when a shorthand is not enough (the invaders `starfield` example in the docs).

6.3 adds CSS-like **custom properties**: `--accent: #c44;  color: var(--accent);` with cascading, inheritance, animation of properties that *use* variables (not of the custom property itself), and fallbacks. Useful for a light/dark or "alert vs stealth" theme, especially with media queries.

### Layout habits for localization

- Prefer flex (`display: flex`, `flex-direction`, grow/shrink) and `min-width` over hard pixel widths for any string that will be translated.
- Use `dp` units plus `SetDensityIndependentPixelRatio` so a 1080p HUD and a 4K HUD share one stylesheet.
- `box-sizing: border-box` when mixing percentages and borders.
- `pointer-events: none` on non-interactive HUD chrome.

### Exercise 7

Animate a pause panel: `transform: translateY(-20px)` + `opacity: 0` by default, `.open` transitions to identity and opacity 1 over `0.2s cubic-out`. Confirm the motion is driven by `SetClass("open", true)` after `Show()`, not by a per-frame C++ lerp of `SetProperty`.

### Checkpoint 7

You know the transition trigger rule (class / pseudo-class only) and that transforms will not clip unless you force them and implement clip-mask.

---

## Module 8 — Data binding for the HUD

Do not `GetElementById` + `SetInnerRML` every frame for suspicion, health, or ammo. That path exists (the official main-loop sample uses it) and is fine for rare updates. The HUD should be a **data model**.

Docs: [data binding](https://mikke89.github.io/RmlUiDoc/pages/data_bindings.html). Sample: `Samples/basic/data_binding`.

```cpp
struct StealthHud {
    float suspicion = 0.f;   // 0–1
    int   nearby_alerts = 0;
    bool  hidden = true;
    Rml::String prompt;
};

StealthHud g_hud;

bool Ui_BindHud(Rml::Context* ctx)
{
    Rml::DataModelConstructor ctor = ctx->CreateDataModel("hud");
    if (!ctor)
        return false;
    ctor.Bind("suspicion", &g_hud.suspicion);
    ctor.Bind("nearby_alerts", &g_hud.nearby_alerts);
    ctor.Bind("hidden", &g_hud.hidden);
    ctor.Bind("prompt", &g_hud.prompt);
    return true;
}

// After gameplay writes g_hud, before context->Update():
void Ui_DirtyHud(Rml::Context* ctx)
{
    if (Rml::DataModelConstructor ctor = ctx->GetDataModel("hud"))
    {
        Rml::DataModelHandle handle = ctor.GetModelHandle();
        handle.DirtyVariable("suspicion");
        handle.DirtyVariable("nearby_alerts");
        handle.DirtyVariable("hidden");
        handle.DirtyVariable("prompt");
        // or handle.DirtyAllVariables();
    }
}
```

```html
<div data-model="hud" id="stealth-hud" style="pointer-events: none;">
    <div class="meter" data-class-hot="suspicion > 0.7">
        <div class="meter-fill" data-attr-style-width="suspicion * 100 + '%' "></div>
    </div>
    <p data-if="hidden">Hidden</p>
    <p data-if="nearby_alerts > 0">{{nearby_alerts}} nearby</p>
    <p data-if="prompt != ''">{{prompt}}</p>
</div>
```

Useful attributes (from the docs and the official sample):

| Attribute | Role |
|-----------|------|
| `data-model="name"` | Attach a subtree to a model created with `CreateDataModel` |
| `{{expr}}` | View: interpolate a data expression into text |
| `data-if` / `data-visible` | Conditional include / visibility |
| `data-for="item : list"` | Repeat. `it_index` is the index |
| `data-value` | Two-way value (inputs, `<select>`) |
| `data-attr-foo` / `data-attrif-foo` | Bind or conditionally set an attribute |
| `data-class-foo="expr"` / `data-style-foo` | Toggle a class or style property |
| `data-event-click="handler"` | Controller: call a bound event callback |

Bind callbacks with `BindEventCallback`, structs with `RegisterStruct` + member registration, arrays with `RegisterArray` (`size()` and `begin()` required — `std::vector` is fine), and transform functions with `RegisterTransformFunc` (the pipe `|` operator in expressions).

**Limitations to respect (official docs):**

- Do not add or remove elements yourself inside a model (especially inside `data-for`) — undefined, can crash.
- Only **top-level** variables can be dirtied. You cannot dirty `invaders[3].health`; dirty `invaders` (unchanged sub-values are skipped inside views).
- Adding `data-*` attributes after the element is already attached has no effect.
- `const` objects / member functions, and members only inherited from a base, cannot be registered.
- `<tabset>`, `<select>` (historically), and similar special elements can fight `data-for`. For `<select>`, bind `data-value` on the select to change the selection.
- Types may need re-registration across DLL boundaries.

`CreateDataModel(name, register, allow_missing_variables=false)`: if the third argument is `true`, views can refer to variables bound later (they show defaults until bind + dirty). Default is strict.

`RemoveDataModel` invalidates all handles and constructors for that model.

### Exercise 8

Drive a suspicion meter and a "Hidden / Detected" label from a C++ struct with no `GetElementById` in the frame loop. Confirm that forgetting `DirtyVariable` leaves the view stale (so you remember to dirty).

### Checkpoint 8

HUD numbers flow model → view. Menu buttons can call `BindEventCallback` instead of raw `EventListener` objects, unless you already have a listener layer you like.

---

## Module 9 — Localization, direction, and shaping

### Encoding and `TranslateString`

All RML and procedural strings are **UTF-8**. Every raw text node read from RML is passed through `SystemInterface::TranslateString`.

```cpp
int De6RmlSystemInterface::TranslateString(Rml::String& translated,
                                          const Rml::String& input) override
{
    // If you replace tokens recursively yourself, return 0 to stop RmlUi from
    // calling you again. If you return > 0, RmlUi recurses into the result.
    if (const char* found = Loc_Find(input.c_str()))  // e.g. "[PAUSE_TITLE]"
    {
        translated = found;
        return 1;
    }
    translated = input;
    return 0;
}
```

Translated text may itself contain RML (the docs use this to substitute a controller-button `<img>` for a token). Watch for infinite recursion if you return a positive count and your output still contains tokens.

This is a *token* hook, not a full ICU formatter. Plural rules, gender, and concatenation of translated fragments stay in your loc pipeline; feed already-formatted UTF-8 to RmlUi, or keep a slightly smarter translator that can replace multiple tokens in one string (the official loc docs mention that need).

For language changes at runtime: swap the string table, then reload documents (or reload text by closing and `LoadDocument` again). `ReloadStyleSheet` does not re-run translation of existing text nodes in a way you should rely on for a full language switch — I did not find a dedicated "retranslate live document" API. Treat language change as a document reload. Label that as the conservative, verified path.

### Layout for variable-length text

Test every menu in a long language (German compound nouns are the usual stress) and in a short one. Flex, wrapping, and min/max widths belong in RCSS. Ellipsis and single-line clipping are layout problems; the text/font paper covers cluster-safe ellipsis if you later grow a custom text element. Built-in RmlUi text wraps; do not assume a 120 px English button still fits.

`lang` and `dir` RML attributes exist (added with text-shaping support in the font-engine interface; `Style::Direction` is `Auto`, `Ltr`, `Rtl`). They are passed through `TextShapingContext` (`language`, `text_direction`, `font_kerning`, `letter_spacing`) into `GetStringWidth` / `GenerateString`.

### What the default font engine will and will not do

Default FreeType: left-to-right runs, kerning, `letter-spacing`, fallback faces for missing glyphs. It will **not** correctly join Arabic, apply many OpenType ligatures, or implement Unicode bidirectional reordering the way a browser does. This was confirmed historically in issue [#211](https://github.com/mikke89/RmlUi/issues/211) and is why HarfBuzz was added as a sample, not as a quiet improvement of the default engine.

### HarfBuzz — sample, not a core font-engine option

As of 6.3:

- CMake `RMLUI_FONT_ENGINE` is only `freetype` or `none`.
- `-DRMLUI_HARFBUZZ_SAMPLE=ON` builds `Samples/basic/harfbuzz`, which contains a complete `FontEngineInterfaceHarfBuzz` (`FontEngineInterfaceHarfBuzz.cpp`, `FontFaceHandleHarfBuzz.*`, `FreeTypeInterface.*`, language tables).
- The 6.x changelog documents `lang` / `dir`, a HarfBuzz sample with RTL, later fallback-font support, HarfBuzz-provided kerning (instead of FreeType), and fixes for unsupported glyph clusters / emoji.
- In August 2024 the maintainer closed #211: *initial* HarfBuzz support lives in a sample; the intent is to move it into core as an optional component later. **That move is not in 6.3.**

If Crisper ships Arabic or other complex scripts, copy (or vendor) that sample engine, call `SetFontEngineInterface` before `Initialise()`, and test editing / IME in RTL yourself. I did **not** fully verify that the sample's text-input caret and IME path are production-complete for RTL editors. Display of shaped Arabic in the sample is the verified claim.

For English-only plus some symbols, default FreeType + a fallback face is enough. Plan the hook so a shaper can be dropped in.

### Exercise 9

Replace the pause title with `[PAUSE_TITLE]`, implement `TranslateString`, and load a fake German table with a much longer string. Confirm the layout still holds. Optionally run the official `harfbuzz` sample and look at the Arabic screenshot path in the changelog — do not claim DE6 supports RTL until that engine is wired and tested.

### Checkpoint 9

You know `TranslateString` is recursive if it returns > 0, and you will not tell the team that "RmlUi has HarfBuzz" without saying *sample font engine, not the default*.

---

## Module 10 — Hot reload

`ElementDocument::ReloadStyleSheet()` reloads `<style>` tags and **external** stylesheets from the document's original source URL. It does **not** reload inline `style` attributes. The source URL used to load the document must still resolve to a valid RML file (so `LoadDocument("ui/menus/pause.rml")` plus a real FileInterface works; a nameless `LoadDocumentFromMemory` without a useful `source_url` will not).

For RML structure changes (new elements, changed `data-model` bindings, new documents):

1. Remember the current document source path and any needed classes / data.
2. `document->Close()` or `context->UnloadDocument(document)` — **destruction is deferred until the next `Context::Update()`**. Do not use the pointer after that `Update`.
3. `LoadDocument` again, re-show, restore focus if you can.

A simple DE6 path: watch `ui/**/*.rml` and `ui/**/*.rcss` (your usual file-watch). On RCSS-only changes, call `ReloadStyleSheet` on loaded documents. On RML changes, reload the document. Always after the watch event, before or at the start of the next frame's `Update`, not from a raw watcher thread.

`Rml::ReleaseTexture(source)` can drop a texture by path after an image file changes; the next layout will `LoadTexture` again. `ReleaseFontResources()` invalidates all `FontFaceHandle`s and should be used carefully (docs: all existing handles become invalid).

The `load_document` sample is the official "minimal backend + load a file" reference, not a hot-reload demo.

### Exercise 10

With the game running, edit a button color in RCSS, hit a debug key (or file watch), and see the change without restarting. Then add a button in the RML and confirm a full document reload is required.

### Checkpoint 10

You know the difference between `ReloadStyleSheet` and a full document reload, and that `UnloadDocument` does not destroy on the spot.

---

## Module 11 — Performance, debugger, and the explode list

### Performance notes (only what the sources support)

- `Context::Update` relayouts dirty documents and ticks animations. `ElementDocument::UpdateDocument()` forces a layout so you can query sizes immediately; the header says it has a performance penalty — call it only when you must read layout before the next `Context::Update`.
- Prefer stylesheet decorators to inline ones (official decorator docs).
- Data views update when variables are dirty; dirtying a top-level array rebuilds the views that care, not the whole context by hand.
- Official DX12 backend: pool allocations, deferred GPU frees on fences, a 128-descriptor heap, 2 MiB buffer chunks / 4 MiB texture chunks as *sample defaults*, not as a measured DE6 budget. Size yours from PIX, not from these numbers.
- `GetNextUpdateDelay` can skip work when idle; a 60 Hz action game can ignore it.
- `ReleaseTextures` / `ReleaseCompiledGeometry` / `ReleaseFontResources` / `ReleaseRenderManagers` exist for level transitions and shutdown. Font release invalidates faces.
- I am **not** publishing "RmlUi costs X microseconds on a 1080p HUD." I did not measure DE6.

### Debugger

`#include <RmlUi/Debugger.h>`, link `RmlUi::Debugger`, `Rml::Debugger::Initialise(context)`, toggle with `Rml::Debugger::SetVisible`. Use it during bring-up. The debugger loads an embedded face through `LoadFontFace(memory, …)` with family `rmlui-debugger-font`.

### Explode list

1. **Reskinning ImGui "for now"** and shipping it as the game UI — you will rebuild layout, nav, and transitions anyway.
2. **Dropping `RenderInterface_DX12` onto DE6's HWND** — it wants to create a device and swapchain.
3. **Premul vs straight alpha** — wrong blend state makes dark halos on glyphs and decorative PNGs.
4. **Y-down pixels vs Y-up clip space** — a wrong projection puts the whole UI off-screen or flipped.
5. **Scissor in transformed space** — the docs require window coordinates.
6. **Resetting the SRV heap every frame** — breaks RmlUi atlases and ImGui 1.92 fonts. Separate persistent heaps or ranges.
7. **Feeding input to the game when `ProcessKeyDown` returned `false`** — "consumed" is `false`.
8. **HUD root without `pointer-events: none`** — an invisible full-screen div eats the mouse.
9. **Forgetting `nav: auto` on `body`** — focus is lost after a click and the pad appears dead.
10. **No stick repeat / no deadzone** — analog input either never moves or chatters.
11. **Expecting CSS transitions on arbitrary property writes** — RCSS transitions hook class / pseudo-class changes.
12. **Animating a box-shadow** — not supported.
13. **Assuming `overflow: hidden` clips transformed children** — it does not, unless you force clip and implement clip-mask.
14. **`GetElementById` every frame instead of a data model** — works until the HUD grows.
15. **Mutating the tree inside `data-for`** — officially undefined, can crash.
16. **Dirtying `foo.bar` instead of `foo`** — only top-level names dirty.
17. **Adding `data-*` after attach** — no effect.
18. **No font loaded before the first document** — there is no default font.
19. **`LoadFontFace` from memory, then freeing the bytes** — the span must remain valid until `Shutdown`.
20. **`TranslateString` returning 1 with a result that still matches a token** — infinite recursion.
21. **"We support Arabic" because `dir="rtl"` exists** — not with the default font engine.
22. **Using `ReloadStyleSheet` after `LoadDocumentFromMemory` with a dummy URL** — no source to reload.
23. **Holding an `ElementDocument*` after `Close` / `UnloadDocument` past the next `Update`** — deferred destroy.
24. **Destroying the RenderInterface before `Shutdown`** — lifetime is documented and debug-checked in related cases.
25. **Drawing RmlUi before TAA** — crawling, blurry text.
26. **Sharing one descriptor heap between ImGui and RmlUi without ranges** — overwritten SRVs, black UI, or device removal.
27. **Enabling C++ exceptions in DE6 because "UI libraries throw"** — 6.3 core does not; fix flags / containers instead.
28. **FreeType 2.11.0 on MSVC** — upstream CMake warning; upgrade.
29. **Building with `BUILD_SHARED_LIBS=ON` (the default) and forgetting DLL copy / `RMLUI_STATIC_LIB`** — link or runtime surprises.
30. **Skipping the included visual tests** (`BUILD_TESTING`, `rmlui_visual_tests`) when writing a custom renderer — the docs recommend comparing against a built-in backend.

---

## Weekend milestone and later work

**Weekend (basic path on screen):**

1. Static 6.3 build, no exceptions flags, `Rml::GetVersion()`.
2. System + File interfaces, init/shutdown lifetime.
3. Thin D3D12 `RenderInterface` (basic + transform + scissor) on DE6's device and command list, persistent SRV range.
4. One pause document, `Show(ModalFlag::Modal)`, keyboard / gamepad nav, `:focus-visible`.
5. Input policy so ImGui-on-top and gameplay still work.
6. `TranslateString` wired to a placeholder table.

**Next:**

- Data-bound stealth HUD with `pointer-events: none`.
- Settings document + RCSS transitions.
- `ReloadStyleSheet` and document reload from a file watch.
- Clip-mask, then layers/filters if design asks for blur or box-shadow.
- HarfBuzz sample engine only if a target language needs it.
- `Rml::Debugger` in developer builds.

No CodingBot / engine PR is implied by this course. Implement in DE6 only when you decide to.

---

## Sources opened

Primary, all consulted for this writeup:

- RmlUi 6.3 tree at tag `6.3` (`ba95ffe`): https://github.com/mikke89/RmlUi/tree/6.3
- [LICENSE.txt](https://github.com/mikke89/RmlUi/blob/6.3/LICENSE.txt), [readme.md gallery](https://github.com/mikke89/RmlUi/blob/6.3/readme.md), [changelog.md](https://github.com/mikke89/RmlUi/blob/6.3/changelog.md)
- [6.3 release notes](https://github.com/mikke89/RmlUi/releases/tag/6.3), [6.1 release notes](https://github.com/mikke89/RmlUi/releases/tag/6.1) (Nightdive titles)
- CMake: `CMakeLists.txt`, `CMake/OptionsLists.cmake`, `CMake/Utilities.cmake`, `CMake/Dependencies.cmake`, `CMake/DependenciesForTests.cmake`, `.github/workflows/build.yml`
- Headers: `RenderInterface.h`, `SystemInterface.h`, `FileInterface.h`, `FontEngineInterface.h`, `TextShapingContext.h`, `Context.h`, `Core.h`, `ElementDocument.h`, `DataModelHandle.h`, `Input.h`, `Vertex.h`, `Platform.h`, `Traits.h`, `StyleTypes.h`
- Official DX12 backend: `Backends/RmlUi_Renderer_DX12.h`, `RmlUi_Renderer_DX12.cpp` (filter/shader names, `Set_User*`, constructor / `Initialize_Device`), `RmlUi_Include_DirectX_12.h`, `RmlUi_Backend_Win32_DX12.cpp`, `Backends/RmlUi_DirectX/LICENSE.txt`
- Event return value: `Source/Core/EventDispatcher.cpp`, `Context.cpp` (`ProcessKeyDown`)
- Default `GetElapsedTime` / `TranslateString`: `Source/Core/SystemInterface.cpp`
- HarfBuzz sample tree: `Samples/basic/harfbuzz/`
- Docs: [Building with CMake](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/building_with_cmake.html), [Integrating](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/integrating.html), [Main loop](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/main_loop.html), [Render interface](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces/render.html), [System interface](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces/system.html), [Localisation](https://mikke89.github.io/RmlUiDoc/pages/localisation.html), [User interface / nav](https://mikke89.github.io/RmlUiDoc/pages/rcss/user_interface.html), [Animations](https://mikke89.github.io/RmlUiDoc/pages/rcss/animations_transitions_transforms.html), [Decorators](https://mikke89.github.io/RmlUiDoc/pages/rcss/decorators.html), [Sprite sheets](https://mikke89.github.io/RmlUiDoc/pages/rcss/sprite_sheets.html), [Font effects](https://mikke89.github.io/RmlUiDoc/pages/rcss/font_effects.html), [Data binding](https://mikke89.github.io/RmlUiDoc/pages/data_bindings.html), [Loading fonts](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/fonts.html)
- Exceptions discussion: https://github.com/mikke89/RmlUi/discussions/769
- Bidirectional / HarfBuzz issue: https://github.com/mikke89/RmlUi/issues/211
- Third-party integrations: https://github.com/mikke89/RmlUi/discussions/186
- ROSE Online news (RmlUI migration): https://www.roseonlinegame.com/news?page=1
- Comparison licenses: [Clay LICENSE](https://github.com/nicbarker/clay/blob/main/LICENSE.md), [Nuklear LICENSE](https://github.com/Immediate-Mode-UI/Nuklear/blob/HEAD/LICENSE), [Noesis licensing](https://www.noesisengine.com/licensing.php)

No GDC talk is cited. I did not find a free public copy of a GDC "RmlUi in AAA" (or similar) talk and did not include Vault-only material.

### Claims I could not verify (or only partially)

- An MSVC `/EHs- /GR-` build was **not** run in this session. Linux CI with `-fno-exceptions -fno-rtti` was confirmed in `build.yml`.
- Whether `Set_UserRenderTarget` / `Set_UserDepthStencil` is enough to drive the official DX12 renderer inside an engine that already owns the device and swapchain — **not verified**. The constructor still creates a device.
- A first-class, in-core optional HarfBuzz font engine — **not in 6.3**; sample only. RTL *editing* / IME quality of that sample was not fully verified.
- A live "retranslate this already-loaded document" API — not found; language switch should reload documents unless you later find a better hook.
- Frame-time cost of a Crisper-sized HUD on DE6 hardware — not measured.
- Noesis indie pricing — a paid indie tier exists; I am not pinning a number.
