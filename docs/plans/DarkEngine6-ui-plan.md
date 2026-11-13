# DarkEngine6 UI / ImGui Improvement Plan

Build-ready, five-phase plan for Editor, Sandbox, and Sandbox2D. Based on the tree at `/workspace/de6-35cf523-b` (tip `35cf523`). No code is changed by this document; it is an implementation reference.

Each phase is independently shippable. Later phases assume earlier ones have landed, but none require a full rewrite of existing gameplay or render code.

---

## Current baseline (what is already in place)

| Piece | Where | Notes |
|---|---|---|
| Dear ImGui **v1.91.8-docking** | `CMakeLists.txt` 242–271, FetchContent, static lib `imgui_editor` | Sources: `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`, `imgui_demo.cpp`, `imgui_impl_win32.cpp`, `imgui_impl_dx12.cpp`. `IMGUI_DEFINE_MATH_OPERATORS`. **No FreeType.** |
| ImGui hosts | Editor, Sandbox, VisualDebugger | `Sandbox2D` and `DarkEngine` / `UnitTests` do **not** link `imgui_editor`. |
| Theme + palette | `Core/UiPalette.h`, `Ui/ImGuiTheme.cpp` `applyImGuiTheme()` | Navy chrome, per-host accent (`UiAccent::Engine/Editor/Sandbox/Sandbox2D`). `WindowRounding = 6`. `FrameBorderSize = 0` (line 57). `ModalWindowDimBg` already themed (line 118). |
| Fonts | `content/fonts/Roboto-Medium.ttf`, `fa-solid-900.ttf` | Loaded in `Ui/ImGuiHost.cpp` `loadUiFonts()` at **15 px × dpi**, `stb_truetype`, `PixelSnapH = true`. Fall back: `C:\Windows\Fonts\segoeui.ttf`, then ProggyClean. |
| Icons.h | `Ui/Icons.h` | FA6 Free Solid subset. `ICON_FA_CLOUD` is U+f0c2. |
| Glyph atlas | `ImGuiHost.cpp` `iconGlyphRanges()` 54–62 | Hand-written ranges. **U+f0c2 is missing** (gap between `0xf07c` and `0xf0c7`). |
| PNG type icons | `content/ui/icons/*.png` (16 files) | Loaded by `EditorApp::loadTypeIcons()` (`Editor/EditorApp.cpp` 64–86) via `ImGuiHost::addTexture`. Used only on the 48×48 drag preview in `EditorAssetBrowser.cpp` 71–73. |
| MainMenu | `Ui/MainMenu.h/.cpp`, compiled into **DarkEngine** | Non-ImGui. `SpritePipeline` + 8×16 bitmap font `Render/LoadingScreenFont.inl`. Layout constants: `kPanelW = 560`, `kRowH = 56` (`MainMenu.cpp` 78–85). CPU API is covered by `UnitTests/Ui/MainMenuTests.cpp` (including a mouse-hit test at 1280×720). |
| Game HUDs | `Sandbox/DevToolsPanel.cpp`, `Editor/EditorPlay.cpp`, `Play/DialogueHud.h`, `Play/QuestBanner.h` | Inventory / Loot / Upgrades / Journal / `##playhud` / `drawQuestHud()` / `drawPauseOverlay()`. Lots of `IM_COL32` and `GetFontSize() * 1.15..2.6` scaling. There is **no** `QuestHud` file; quest UI lives in Dev Tools + `##playhud`, banners in `QuestBanner.h`. |
| Sprite HUDs | `Render/HealthHud.cpp`, `Render/CrosshairHud.cpp` | Fixed pixel sizes (health bar 280×22, crosshair arm 11 px). Sandbox draws both (`SandboxApp.cpp` 3742, 3752). Editor Play draws only the crosshair (`Editor/EditorRender3D.cpp` 132). |
| Docking / viewports | `ImGuiHost::init()` 167–182 | `docking=true` **also** sets `ImGuiConfigFlags_ViewportsEnable` and forces `WindowRounding = 0`. Editor and VisualDebugger pass `true` (`EditorAppInit.cpp` 175, `DebuggerApp.cpp` 89). Sandbox passes `false` (`SandboxApp.cpp` 2504). |
| DPI | `Core/Window.cpp` `WM_DPICHANGED` 269–286 | Updates `m_dpiScale` and repositions the HWND. **No** `takeDpiChanged()` (unlike `takeSizeChanged()` at `Window.h` 29). `ImGuiHost` reads `window.dpiScale()` only once in `init()` (184–187). |
| SRV heap | `ImGuiHost.cpp` `kSrvHeapSize = 64` (line 32) | Slot 0 = font atlas; 1..63 = `addTexture()`. Slots are not recycled until `shutdown()`. |
| CMake UI split | `cmake/DarkEngineTargets.cmake` 185–196 | `Ui/MainMenu.cpp/.h` is pulled out of `DE_UI_SOURCES` and compiled into `DarkEngine` so Sandbox2D / UnitTests can link it without ImGui. Remaining `Ui/` files are added to Editor, VisualDebugger, and Sandbox only. New `Ui/*.cpp` files are picked up automatically (`de_glob_folder` + `CONFIGURE_DEPENDS`). |

**Constraints to keep:** C++23, no exceptions, Windows / D3D12, MIT. Do not pull ImGui into `DarkEngine` or `UnitTests`. Prefer FetchContent over a new package manager (the repo has no `vcpkg.json`).

---

## Phase 1 — Quick visual fixes

### Goal and payoff

Fix the broken polish that is already paid for: Cloud Volume tofu, unused PNG type icons, square floating windows in Editor/Debugger, and a few theme tokens that make inputs and tabs look unfinished. One short PR, no new dependencies, visible in every ImGui host.

### Files to touch

- `Ui/ImGuiHost.h`, `Ui/ImGuiHost.cpp` — glyph ranges; split docking from viewports.
- `Ui/Icons.h` — no new glyphs required unless we add some; the generator should read this file.
- `Ui/ImGuiTheme.cpp` — `FrameBorderSize`, hover delays, `SeparatorTextPadding`, `TabBarOverlineSize`.
- `Editor/EditorUi.cpp` — Scene tree (`Begin("Scene")` at 464): draw PNG (or FA fallback) next to each row.
- `Editor/EditorAssetBrowser.cpp` — Place list and folder/file rows (currently FA text in `drawPrimitive` / folder loop, ~348–420). Reuse existing `iconForType()` (81–112) as the FA fallback.
- Optional content: add the five missing type PNGs under `content/ui/icons/` (`water.png`, `cloud_volume.png`, `stream.png`, `spawner.png`, `movie.png`). `file.png` and `folder.png` already exist and are unused.
- No CMake changes.

### Implementation details and gotchas

1. **Generate glyph ranges from `Icons.h`.** Replace the hand-written `iconGlyphRanges()` table (`ImGuiHost.cpp` 54–62). Decode every `ICON_FA_*` UTF-8 string in `Icons.h` to a codepoint, sort, and merge adjacent values into `ImWchar` inclusive pairs terminated by `0`. That automatically includes `ICON_FA_CLOUD` (U+f0c2) and any future icon. Keep the helper in an anonymous namespace next to `loadUiFonts()`.

2. **Do not let `docking` imply viewports.** Today `ImGuiHost::init(..., bool docking, ...)` (`ImGuiHost.h` 18–19, `ImGuiHost.cpp` 167–182) sets both `DockingEnable` and `ViewportsEnable`, then zeroes `WindowRounding`. Change it so:
   - `docking=true` enables only `ImGuiConfigFlags_DockingEnable`.
   - Viewports stay **off by default**. If you keep a knob, add `bool viewports = false` after `docking` so existing call sites stay source-compatible.
   - Delete (or gate) the `WindowRounding = 0` / opaque `WindowBg` override so `applyImGuiTheme()`'s 6 px rounding and `drawImGuiDecorations()` shadows actually show on floating Editor/Debugger windows.
   - Leave the `m_viewports` render/shutdown path (`render()` 274–278, `shutdown()` 233–234) in place for a later explicit opt-in. Multi-viewport DX12 is already wired; it is just a poor default because it fights rounded chrome.

   Call sites today: Editor `init(..., "editor_imgui.ini", true, UiAccent::Editor)` (`EditorAppInit.cpp` 175), VisualDebugger `init(..., "debugger_imgui.ini", true, UiAccent::Engine)` (`DebuggerApp.cpp` 89), Sandbox `init(..., "sandbox_imgui.ini", false, UiAccent::Sandbox)` (`SandboxApp.cpp` 2504). After this change, Editor and Debugger keep docking and gain rounding; Sandbox is unchanged.

3. **Wire PNG icons into Scene and Assets.** `loadTypeIcons()` already walks every `SceneObjectType` and resolves `ui/icons/<toString(type)>.png` (`Scene/SceneTypes.h` 99–123). It is called right after `m_imgui.init` (`EditorAppInit.cpp` 179). `typeIconId()` returns `0` when missing.
   - Scene tree (`EditorUi.cpp` 469–501): if `typeIconId(so.type) != 0`, `ImGui::Image(..., ImVec2(iconPx, iconPx))` + `SameLine()` before `Selectable`. Otherwise prefix with `iconForType()` (move that helper to a shared header, or duplicate the one-line call). Keep the `##%u` id suffix so labels stay unique.
   - Assets Place rows and model/folder rows: same pattern, ~16–18 px in the list, keep the 48 px image on the drag preview.
   - Missing PNGs (Water, CloudVolume, Stream, Spawner, Movie) will keep the existing `DE_LOG_WARN` from `loadTypeIcons()` until art is added. FA fallback must work in the meantime — especially Cloud Volume, once the glyph is in the atlas.
   - `file.png` / `folder.png` can be loaded as two extra `addTexture` slots for the Assets file list, or left as FA glyphs. Either is fine.

4. **Theme polish in `applyImGuiTheme()`** (`ImGuiTheme.cpp` 39–59), still *before* `ScaleAllSizes` in `ImGuiHost::init`:
   - `style->FrameBorderSize = 1.0f;` — contrast on inputs against `kRaised` / `frameRest`.
   - `style->HoverDelayShort = 0.20f;` and `style->HoverDelayNormal = 0.50f;` (ImGui 1.91 `ImGuiStyle` fields). Stops tooltips flashing on the View menu and Inspector sliders. Hovered flags that want a delay should use `ImGuiHoveredFlags_DelayShort` / `DelayNormal`.
   - `style->SeparatorTextPadding = ImVec2(20.0f, 3.0f);` — prepares Phase 4 section headers.
   - `style->TabBarOverlineSize = 2.0f;` — matches the title-bar accent strip already drawn in `drawImGuiDecorations()` (161–167).

5. **SRV budget.** `kSrvHeapSize = 64`, slot 0 is the font atlas, slots are never recycled (`ImGuiHost.cpp` 32, 115–119, 27). Current load: up to 19 type icons (`SceneObjectType::Count` is 20 including the sentinel). Plus 2 for file/folder is still fine. Do **not** start loading per-asset glTF thumbnails in this phase. If that comes later, raise the heap or add a slot recycler; 64 will not survive a content browser full of previews.

### Verification

- **Editor:** Create → Cloud Volume shows a real cloud glyph, not tofu. Scene tree and Assets Place list show type PNGs (cube, lights, pawns, etc.) and FA fallbacks for the five types without art. Floating windows (IBL, SSAO, SSR, Sky — `EditorUi.cpp` 318–391, 601) have 6 px corners and the umbra/accent from `drawImGuiDecorations()`. Text fields have a 1 px border. Tooltips on Fly speed / Grid do not appear on a 1-frame hover. Docking still works; tearing a window out of the OS frame should *not* happen unless viewports were explicitly enabled.
- **Sandbox:** Visual change is theme-only (already `docking=false`, rounding already on). Confirm Dev Tools inputs pick up the frame border and calmer tooltips.
- **Sandbox2D:** No ImGui — should be bit-identical. No CMake or `MainMenu` changes in this phase.
- **VisualDebugger:** Same rounding/theme win as Editor (`DebuggerApp.cpp` 89).
- Log: `ImGuiHost: ready (docking=1 viewports=0)` in Editor/Debugger.

---

## Phase 2 — Typography and FreeType

### Goal and payoff

Replace the single 15 px `stb_truetype` Roboto-Medium (snapped, then faked-larger with `GetFontSize() * 1.15..2.6`) with a sharp FreeType atlas and a 3-font kit: body, heading, mono. Headings, banners, paths, and profiler numbers stop looking crunchy and flat. No layout rewrite yet — just font handles and call-site swaps.

### Files to touch

- `CMakeLists.txt` — FetchContent FreeType; add `misc/freetype/imgui_freetype.cpp` to `imgui_editor`; `IMGUI_ENABLE_FREETYPE`.
- `Ui/ImGuiHost.h`, `Ui/ImGuiHost.cpp` — `UiFonts` struct, load three faces, `PixelSnapH = false`, expose `fonts()`.
- `Ui/ImGuiTheme.h` (optional) — `pushBodyFont()` / `pushHeadingFont()` / `pushMonoFont()` helpers if you want them next to `toImVec4()`.
- `content/fonts/` — add `Roboto-Bold.ttf` and a monospace face; update `LICENSE.txt`.
- Call sites that scale the 15 px bitmap (swap to `PushFont(fonts().heading)` or `.mono` instead of `fs * N`):
  - `Sandbox/DevToolsPanel.cpp`: `drawQuestHud()` 202–251 (`fs * 1.2`, `1.1`, `2.0`), `drawAlarm()` 302 (`* 2.2`), `drawGuardSpeech()` 478 (`* 1.15`), plus `* 1.3`, `* 1.6`, `* 2.6` at 853 / 883 / 938, and defense text at 954 (`parry ? 2.2 : 1.7`).
  - `Editor/EditorPlay.cpp` `drawPlayHud()` 1085 (`parry ? 2.2 : 1.7`).
  - `Play/DialogueHud.h` 74 (`fs * 1.15`).
- Do **not** add ImGui includes to any `Play/*.cpp` (they compile into `DarkEngine`; see the warning at the top of `DialogueHud.h`).

### Implementation details and gotchas

1. **FreeType via FetchContent, not vcpkg.** Mirror the existing ImGui block (`CMakeLists.txt` 242–271). Suggested tag: [freetype/freetype](https://github.com/freetype/freetype) `VER-2-13-3`. FreeType is C, so it does not introduce C++ exceptions. Build it as a static lib with the same `/MD` runtime already set globally (`CMakeLists.txt` 10). Disable extra HarfBuzz/PNG/Zlib unless you already have them — the stock ImGui FreeType path does not need them. Link `freetype` **PUBLIC** onto `imgui_editor`.

2. **Compile `imgui_freetype.cpp` and define `IMGUI_ENABLE_FREETYPE` PUBLIC** on `imgui_editor` so every TU that includes `<imgui.h>` sees the FreeType atlas builder. Add `${imgui_SOURCE_DIR}/misc/freetype` to the public include path. This is the documented ImGui 1.91 hook; no need for a custom `imconfig.h` unless you prefer one.

3. **`UiFonts` on `ImGuiHost`:**

   ```cpp
   struct UiFonts {
       ImFont* body    = nullptr; // Roboto-Medium, 15 px * dpi
       ImFont* heading = nullptr; // Roboto-Bold,   20 px * dpi
       ImFont* mono    = nullptr; // Cousine (or Roboto Mono), 14 px * dpi
   };
   const UiFonts& fonts() const;
   ```

   - Load body first and leave it as `io.FontDefault`.
   - Merge `fa-solid-900.ttf` into **body** (and optionally heading) with the Phase 1 generated ranges, at `size * 0.88f`, `MergeMode = true`. Do not merge icons into mono.
   - Set `PixelSnapH = false` on every `ImFontConfig`. With FreeType, `OversampleH/V` are ignored; drop them or leave them, it does not matter.
   - Fallbacks: body → `segoeui.ttf`; heading → body (log a warning if Bold is missing); mono → `C:\Windows\Fonts\consola.ttf`, then body.
   - Store the unscaled px sizes (15 / 20 / 14) as named constants. Phase 5 will multiply them on atlas rebuild.

4. **Vendor fonts (Apache 2.0 / OFL).** Only `Roboto-Medium.ttf` and `fa-solid-900.ttf` exist today (`content/fonts/LICENSE.txt`). Add:
   - `Roboto-Bold.ttf` from [googlefonts/roboto](https://github.com/googlefonts/roboto), same Apache 2.0 as Medium (already credited as bundled with Dear ImGui's `misc/fonts`).
   - `Cousine-Regular.ttf` (SIL OFL) from ImGui's own `misc/fonts/Cousine-Regular.ttf` after FetchContent, copied into `content/fonts/` (or a CMake POST_BUILD copy). Update `LICENSE.txt`. Do not assume the ImGui source tree is present at runtime.

5. **Replace scale hacks, don't invent a fourth size.** `PushFont(heading)` at 20 px is the replacement for `fs * 1.2` through `~2.2`. For the oversized ALARM / quest-banner title / PARRY flash, still use the heading font (optionally `AddText(heading, heading->FontSize * 1.1f, ...)` — that scales a *hinted* outline, not a 15 px bitmap). Paths, entity IDs, skill numbers, and the status-bar FPS go through `PushFont(mono)`.

6. **Atlas slot 0.** `ImGui_ImplDX12_InitInfo.LegacySingleSrvCpu/GpuDescriptor` points at heap slot 0 (`ImGuiHost.cpp` 212–213). FreeType still uploads the atlas there. Do not reset `nextSrvSlot` when experimenting with a rebuild in this phase (full live rebuild is Phase 5).

7. **Optional CMake switch.** `option(DE_ENABLE_FREETYPE "Build ImGui with FreeType (sharper UI fonts)" ON)` is useful if a machine cannot fetch FreeType, but the default should be ON. When OFF, keep the current stb path and a single body font so the rest of the engine still links.

### Verification

- **Editor:** Menu bar, Scene names, Inspector values, and Material paths are crisp at 100% and 150% DPI (launch on a high-DPI monitor, or set a custom scaling factor in Windows). Heading-sized Play defense text (`EditorPlay.cpp` 1085) is no longer pixelated. Status-bar FPS (`EditorSceneFile.cpp` 838–842) in mono is obviously tabular.
- **Sandbox:** Quest title, ALARM, dialogue speaker line, and pause-adjacent banners use the heading face. Dev Tools numbers / IDs use mono. No tofu in FA icons (Cloud Volume path is Editor-only, but icon glyphs in Dev Tools buttons such as `ICON_FA_FORWARD_STEP` at `drawPauseOverlay`'s sibling Step button, line 1213, must still render).
- **Sandbox2D:** Unchanged (no ImGui, no FreeType link).
- Log: `ImGuiHost: UI font body 15.0px + heading 20.0px + mono 14.0px + icons` (sizes × dpi). Confirm there is no unexpected jump in `ImGuiHost` init time (FreeType atlas build is a bit slower than stb; once per boot is fine).

---

## Phase 3 — Game menus and in-game HUD unification

### Goal and payoff

The biggest visual jump. `MainMenu` and the pause banner stop looking like a different product from the Editor. Inventory, Loot, Upgrades, Journal, Editor `##playhud`, Dialogue, and Quest chrome all sit on `UiPalette`, Roboto, icons, and rounded cards. The CPU menu API and Sandbox2D's non-ImGui path stay intact.

### Files to touch

- **Keep in DarkEngine (no ImGui include):** `Ui/MainMenu.h`, `Ui/MainMenu.cpp` — state, `update()`, `pollResult()`, and the existing `SpritePipeline` `draw()` / `create()` / `shutdown()`.
- **New, ImGui-only (lands in `DE_UI_SOURCES`, auto-linked to Editor / Sandbox / VisualDebugger):** `Ui/ImGuiGameChrome.h` + `Ui/ImGuiGameChrome.cpp` (name flexible; `Ui/GameHudWidgets` is equally fine). Contents:
  - `drawMainMenuImGui(MainMenu&)` — visual replacement for `MainMenu::draw()` when an `ImGuiHost` is up.
  - Shared card / modal / section helpers used by Inventory, Loot, Upgrades, Journal, pause, play HUD, dialogue choices, quest tracker.
- `Sandbox/SandboxApp.cpp` — when `m_menu.visible()`, draw the ImGui menu instead of (or in addition to, gated) `m_menu.draw()`; today the visible-menu branch only pumps an empty ImGui frame (3802–3805).
- `Sandbox/DevToolsPanel.cpp` — `drawPauseOverlay()` (1185–1193), `drawInventory()` (117), `drawLoot()` (1002), `drawUpgrades()` (332), `drawJournal()` (386), `drawQuestHud()` (195), `drawAlarm()` / banners.
- `Editor/EditorPlay.cpp` — `drawPlayHud()` `##playhud` (1072–1246), `drawLoot()` (1248), `drawUpgrades()` (787), `drawPlayDialogue()` (1054).
- `Play/DialogueHud.h` — restyle the subtitle plate and choice window (`Begin` at 109) onto the shared card helper and `UiPalette` (today: raw `IM_COL32(10,14,12,200)` and `IM_COL32(235,235,210,255)`).
- `cmake/DarkEngineTargets.cmake` 185–187 — update the comment; do **not** move `MainMenu.cpp` into `DE_UI_SOURCES`.
- `UnitTests/Ui/MainMenuTests.cpp` — should keep passing with **no** API break. The mouse test (83–101) is tied to the sprite layout (`kRowH = 56` etc.); if ImGui hosts stop calling `MainMenu::update()` for mouse, keep `update()` working for Sandbox2D and the tests.

### Implementation details and gotchas

1. **Split renderer from state — do not pull ImGui into DarkEngine.** This is the load-bearing constraint (`DarkEngineTargets.cmake` 185–187, `DialogueHud.h` 3–5). `MainMenu::update(const Input&, viewW, viewH)` / `pollResult()` / `show()` / `hide()` / `entries()` stay as they are. `drawMainMenuImGui()` is a free function in the ImGui-only TU that reads that state and, on click, calls `setSelected()` / the same confirm path `update()` uses (`confirmSelection()` is currently private — add a narrow public `confirm()` or a friend, or have the ImGui drawer set a pending result through a small new method).

2. **ImGui menu look.** Centered modal card, no OS title bar, accent strip, Roboto heading for `m_title`, body + muted subtitle per `MainMenuEntry`, FA icons by `MainMenuKind` (scene / built-in / quit). Use `ImGuiCol_ModalWindowDimBg` (already `kVoid` at 0.62 in `applyImGuiTheme()` line 118). Size roughly the current 560 px panel, but let ImGui DPI scaling do the work. Keyboard / gamepad: enable `ImGuiConfigFlags_NavEnableGamepad` in `ImGuiHost::init` (keyboard nav is already on, line 166). Prefer ImGui nav on the Selectables so D-pad / A / B still feel native; keep `MainMenu::update()` as a fallback so existing Input bindings (arrows, WASD, Enter, Esc) continue to work even if focus isn't on the window yet.

3. **Avoid double-consuming input.** If both `update()` mouse hit-tests and ImGui `Selectable` run in the same frame, a click can confirm twice or select the wrong row. In ImGui hosts: let ImGui own the mouse while the menu is visible (`wantCaptureMouse()` is already consulted in Sandbox), and let `update()` keep handling keys / gamepad. Sandbox2D keeps full `update()` mouse logic.

4. **`drawPauseOverlay()` → centered modal.** Today it is a single `TextUnformatted` line pinned to the top (`DevToolsPanel.cpp` 1185–1193) with `NoNav`. Replace with a centered card, dimmed backdrop, heading "Paused", body listing fly-cam controls, and a focused Resume button. Drop `ImGuiWindowFlags_NoNav`. Gamepad / keyboard should move between Resume and a Step action (O / the existing Step button at 1213).

5. **Shared game chrome helper.** One `BeginGameCard(name, flags, size)` / `EndGameCard()` that applies `UiPalette::kPanel` background, 6 px rounding, no title bar, optional left accent strip (same idea as `drawImGuiDecorations()`). Inventory, Loot, Upgrades, Journal, and Editor `##playhud` should all use it. Replace one-off `ImVec4(0.94f, 0.80f, 0.43f, 1.0f)` / `IM_COL32(240, 205, 110, …)` with `toImVec4(UiPalette::kWarn)`, `kOk`, `kDanger`, `kTextMuted`. `Play/QuestBanner.h` already packs colors as `0xAABBGGRR` (same as `IM_COL32`); map those to palette constants when restyling `drawQuestHud()` and `##playhud`.

6. **Dialogue and quest tracker.** `DialogueHud.h` is a header-only ImGui helper shared by Sandbox and Editor — restyle in place, still never included from a `Play/*.cpp`. The world-space talk prompt and subtitle can stay as `ImDrawList` text, but fill/text colors should come from `UiPalette` and fonts from `UiFonts`. There is no separate `QuestHud` file; extract a `drawQuestTracker(...)` into `ImGuiGameChrome` and call it from `SandboxApp::drawQuestHud()` and `EditorApp::drawPlayHud()` so the two stop drifting.

7. **Sandbox2D — recommended default: keep the sprite menu.** `Sandbox2D` links only `DarkEngine` + `box2d` (`CMakeLists.txt` 218–222). It already calls `m_menu.create(..., UiAccent::Sandbox2D)`, `update()`, `draw()`, `shutdown()` (`Sandbox2DApp.cpp` 1146–1168, 1270–1279, 1487, 1495). Do **not** link `imgui_editor` in the main Phase 3 slice. Sprite `MainMenu::draw()` remains the 2D path; Phase 5 will DPI-scale its constants. Visual parity with Sandbox is an optional **Phase 3b**: `target_sources(Sandbox2D PRIVATE ${DE_UI_SOURCES})`, `target_link_libraries(Sandbox2D PRIVATE imgui_editor)`, stand up an `ImGuiHost` with `UiAccent::Sandbox2D`, and call `drawMainMenuImGui`. That is a product decision (ImGui + a 64-slot SRV heap + font atlas on a currently lean 2D template) and must not block 3a.

8. **Order of draw vs. `MainMenu::draw()` in Sandbox.** After 3a, Sandbox should not also blit the sprite menu on top of the ImGui card. Keep `create()` for Sandbox2D; Sandbox can skip `m_menu.create()` / `m_menu.draw()` once the ImGui path is proven, or leave `create()` harmless and simply not call `draw()`. `isReady()` is used as a GPU-menu gate in a few places (`SandboxApp.cpp` 944, 2496–2498) — if Sandbox stops calling `create()`, those checks need to key off `m_imgui.isReady()` (or a new `MainMenu` flag) so the menu still shows.

### Verification

- **Sandbox:** Boot to the scene picker — Roboto, accent, rounded card, icons, dimmed backdrop. Keyboard, mouse, and gamepad still confirm / quit. `UnitTests` `MainMenu.*` all pass. Escape / Quit still exits. During play: Inventory, Loot, Upgrades, Journal, quest tracker, dialogue choices, and ALARM share palette and heading font. Pause (F3) is a centered modal with nav; fly cam still works; O still steps.
- **Editor:** Play mode (`F12`) `##playhud`, loot, upgrades, and dialogue match the Sandbox cards. Climb / talk prompts remain world-projected.
- **Sandbox2D:** Sprite menu still appears, still accepts the same input, still confirms scenes. No ImGui dependency, no link errors. (If 3b is done: gold-accent ImGui menu, same API.)
- Confirm `DarkEngine` and `UnitTests` still build without ImGui include paths.

---

## Phase 4 — Shared property-grid widgets and Editor / Sandbox chrome

### Goal and payoff

Inspector, lighting, material, 2D Level, and Dev Tools stop looking like default widget stacks. The Editor View menu is split, a thin toolbar holds everyday toggles, lighting panels dock as tabs, the status bar is segmented, and Sandbox Dev Tools becomes a docked, tabbed workspace (docking on, viewports still off).

### Files to touch

- **New:** `Ui/ImGuiWidgets.h`, `Ui/ImGuiWidgets.cpp` (auto-added to Editor / Sandbox / VisualDebugger via `DE_UI_SOURCES`).
- `Editor/EditorUi.cpp` — View menu (103–191), IBL/SSAO/SSR (316–391), `drawSkyPanel()` (601), `drawInspector3D()` (675), `Begin("2D Level")` (537), dock host call (314).
- `Editor/EditorModel.cpp` — `Begin("Material")` (459) and the rest of the material / model editors.
- `Editor/EditorSceneFile.cpp` — `drawStatusBar()` (819–847).
- `Ui/ImGuiTheme.cpp` — `beginPassthruDockSpace()` default layout (171–213): dock a combined Lighting window; leave room for a toolbar.
- `Sandbox/SandboxApp.cpp` 2504 — pass `docking=true` (viewports stay false after Phase 1).
- `Sandbox/DevToolsPanel.cpp` — `drawDevTools()` (1195): turn the long `CollapsingHeader` list (Rendering 1247, IBL 1416, SSAO 1438, SSR 1462, Sky 1494, … Gameplay headers 1721–1970, Network 2016, Debugger 2061) into a `BeginTabBar` (Rendering / Gameplay / Network).

### Implementation details and gotchas

1. **`Ui/ImGuiWidgets` property grid.** A tiny, no-virtuals helper, no exceptions:
   - `BeginPropTable(id)` / `EndPropTable()` — 2-column `BeginTable` with `ImGuiTableFlags_SizingStretchProp` (or `SizingFixedFit` on the label column). Label column uses `kTextMuted`, right- or left-aligned. Widget column calls `SetNextItemWidth(-1)` so sliders fill the row.
   - `PropFloat`, `PropFloat2/3`, `PropSlider`, `PropCheckbox`, `PropCombo`, `PropColor3` / `PropColor4` (square `ColorEdit` swatches, `ImGuiColorEditFlags_NoInputs` on the picker popup is optional; default `ColorEdit3` with full row width is enough).
   - `PropSeparator(text)` → `ImGui::SeparatorText`.
   - Keep the helper header-only *or* a `.cpp`; either is fine. Do not depend on `EditorApp`.

2. **Refactor, don't restyle in place ad hoc.** Walk Inspector, Sky, IBL, SSAO, SSR, Material, 2D Level, and the matching Dev Tools sections and replace `Text` + `Slider` / `DragFloat` / `ColorEdit` / `Checkbox` stacks with the helper. Behavior (ranges, callbacks, `DE_LOG_INFO` on toggles) stays identical. Sky weather buttons (`EditorUi.cpp` 629–648) can stay as a custom row.

3. **Split the View menu** (`EditorUi.cpp` 103–191) into:
   - **Panels** — Particle, Animation, HSM, Dialogue, Audio, Assets, Model Parts, Material, Terrain, Water, Wind.
   - **Viewport** — 2D Scene, Grid, Solid Ground, (toolbar will also expose these).
   - **Rendering** — instancing, indirect, GPU particles, ACES, albedo debug, G-buffer / velocity tiles, Bloom, TAA, Motion Blur, Local Lights, shadows, Cloud Volumes, Decals, decal stats text.

   Move **Play / Stop**, **Save**, **2D/3D**, **Grid**, and **Fly speed** onto a thin toolbar directly under `BeginMainMenuBar()` (ends at line 310). Implement the toolbar as a `NoDocking` / `NoDecoration` viewport sidebar of height `ImGui::GetFrameHeight()`, then pass that height into `beginPassthruDockSpace` as an extra top reserve (today it only subtracts `bottomReservePx` for the status bar, line 171–179, and is called with `ImGui::GetFrameHeight()` at `EditorUi.cpp` 314). Play stays available in the Play menu (298–306) as well.

4. **Dock IBL / SSAO / SSR / Sky.** They are currently free-floating 320–380 px windows and are **not** in the default dock layout (`beginPassthruDockSpace` docks only Scene, Assets, Inspector, Particle System, 2D Level — lines 202–206). Combine them into one "Lighting" window with a `BeginTabBar`, and `DockBuilderDockWindow("Lighting", dockRight)`. Existing `editor_imgui.ini` will *not* pick up a new default layout (`DockBuilderGetNode(dockId) == nullptr` is a first-run-only check, line 191). Either bump a layout version (new dock host name, or a stored ini entry you can invalidate) or document "delete `editor_imgui.ini` once."

5. **Status bar segments** (`drawStatusBar()`, `EditorSceneFile.cpp` 819–847). Today one `Text` concatenates scene name, 2D/3D, object count, place type, net role, and PLAY, with FPS `SameLine`'d to the right. Split into aligned items (left cluster + right FPS/net), still using `UiPalette::kRaised` and `toImVec4`. Put FPS in the Phase 2 mono font. A one-row `BeginTable` or a few `SameLine` groups with `ImGui::GetWindowWidth()` is enough; do not introduce a new window.

6. **Sandbox docking without viewports.** `m_imgui.init(..., false, UiAccent::Sandbox)` becomes `true` for docking. After Phase 1 this will not enable viewports or square off the windows. Optional: a simple `beginPassthruDockSpace` for Sandbox so Dev Tools can dock left and leave the 3D view clear. Group Dev Tools into tabs:
   - **Rendering** — current Rendering, IBL, SSAO, SSR, Sky, Exposure, Cloud LOD, Fog, Water, grass, leaves.
   - **Gameplay** — Jump attack, Skills, Status, Hit reaction, Crouch, Guard awareness, Shield, Rifle, Hunter pack, NPC info, Camera.
   - **Network** — Network + Debugger.

   Use the new property-grid helper inside those tabs. `sandbox_imgui.ini` will start storing dock state; first launch after the change may need a deleted ini, same as Editor.

7. **SRV heap still 64.** Still no per-asset thumbnails.

### Verification

- **Editor:** View menu is three menus, not one dump. Toolbar Play / Save / 2D-3D / Grid / Fly speed work and stay in sync with the menus and F12 / F3. Inspector, Sky, IBL, SSAO, SSR, Material, 2D Level all share aligned label/widget columns and square color swatches. Lighting docks on the right as tabs (after a fresh `editor_imgui.ini` if you invalidate the layout). Status bar: name / mode / counts on the left, FPS on the right, no overflow at 1280 px width. Scene still shows through the passthrough dock center.
- **Sandbox:** Dev Tools docks, splits into three tabs, and no longer forces a single giant floating scroll. Gameplay still simulates. M still toggles the window. No torn-off OS windows.
- **Sandbox2D:** Unchanged.
- **VisualDebugger:** Still docks; no viewport regression from Phase 1.

---

## Phase 5 — Live DPI scaling and polish

### Goal and payoff

Dragging Editor / Sandbox / VisualDebugger between a 100% and a 150%+ monitor rebuilds the font atlas and restyles from an unscaled `ImGuiStyle` instead of leaving blurry or tiny UI. Sprite HUDs (`HealthHud`, `CrosshairHud`) and the Sandbox2D sprite `MainMenu` scale by the same factor.

### Files to touch

- `Core/Window.h`, `Core/Window.cpp` — `takeDpiChanged()` (mirror `takeSizeChanged()`, `Window.h` 28–29, `Window.cpp` 455). Set the flag in the existing `WM_DPICHANGED` handler (269–286). Do not remove the `SetWindowPos` to the suggested RECT.
- `Ui/ImGuiHost.h`, `Ui/ImGuiHost.cpp` — store the unscaled style, current scale, and `syncDpi(Window&)` / `rebuildFonts(float dpiScale)`. Call from `beginFrame()` or from each host once per frame after `pollEvents()`.
- `Ui/ImGuiTheme.cpp` — `applyImGuiTheme()` must stay the unscaled source of truth (comment at `ImGuiTheme.h` 15 already says "Call before Style.ScaleAllSizes").
- `Render/HealthHud.h/.cpp` — take a `float dpiScale` on `draw()` (or read it from the caller). Scale `barW = 280`, `barH = 22`, `pad = 28`, `border = 3` (`HealthHud.cpp` 72–75).
- `Render/CrosshairHud.h/.cpp` — scale `arm = 11`, `thick = 2`, `gap = 5`, slot size `7`, offsets (`CrosshairHud.cpp` 81–107).
- Call sites: `SandboxApp.cpp` 3742 and 3752; `Editor/EditorRender3D.cpp` 132. Pass `window().dpiScale()`.
- `Ui/MainMenu.cpp` — multiply `kPanelW` / `kRowH` / gaps / typography scales by `dpiScale` in `computeLayout()` and `draw()` so Sandbox2D (and any leftover sprite path) tracks the OS scale. **This will change the `MainMenu.MouseClickSelectsRow` hit point** if tests pass `1280×720` at scale ≠ 1. Keep unit tests at implicit scale 1.0 (default `update(..., viewW, viewH)` with no dpi argument, or default `dpiScale = 1.0f`).
- Hosts: `Editor` / `Sandbox` / `VisualDebugger` already poll the window every frame; they only need to call the new ImGui sync. `Sandbox2D` needs no ImGui path, but should pass dpi into `MainMenu` if you add that argument.

### Implementation details and gotchas

1. **`Window::takeDpiChanged()`.** Same shape as `takeSizeChanged()`: a sticky bool cleared by the caller. `WM_DPICHANGED` already writes `m_dpiScale`. Do not rebuild fonts inside `wndProc` — the D3D12 device and ImGui frame may be mid-flight. Handle it on the game loop, next to the `Application.cpp` 411 / 596 resize path.

2. **Rebuild from an unscaled base style.** In `ImGuiHost::init`, after `applyImGuiTheme(accent, &style)` and *before* `ScaleAllSizes`, copy `ImGuiStyle` into `m_baseStyle`. On a DPI change:
   - `ImGui::GetStyle() = m_baseStyle;`
   - `GetStyle().ScaleAllSizes(dpiScale);`
   - Rebuild the font atlas at `body 15 * dpi`, `heading 20 * dpi`, `mono 14 * dpi` (and icon merge size).
   - Do **not** call `ScaleAllSizes` on an already-scaled style (that compounds).

3. **Font atlas vs. `addTexture` slots.** Slot 0 is the font atlas; slots 1+ are Editor type icons (`nextSrvSlot` starts at 1). Rebuild must overwrite **only slot 0**. Do not recreate the descriptor heap and do not reset `nextSrvSlot` — that would invalidate every `m_typeIconIds[]` until `loadTypeIcons()` is run again (and `addTexture` cannot recycle). With the legacy single-SRV DX12 init (`ImGuiHost.cpp` 212–213), invalidate/recreate the ImGui DX12 font texture into the existing slot 0 handles, then `ImGui_ImplDX12_CreateDeviceObjects` as needed. Verify in PIX or a debugger that type icons still render after a DPI change.

4. **`addTexture` textures themselves are not DPI-dependent** (they are already bitmaps). List-row icon sizes (`ImVec2(iconPx, iconPx)`) should be `16 * dpi` (or `ImGui::GetFrameHeight() - padding`) so they track the new style scale.

5. **Sprite HUDs are in physical pixels, Y-up.** `HealthHud` / `CrosshairHud` already take `viewW/viewH` from `renderer().width()/height()`, which are physical pixels. Scaling the *constants* by `dpiScale` is what we want (a 280 px bar at 100% becomes 420 px at 150%). Do not also multiply by `viewW / 1920` unless you deliberately want resolution-based layout; DPI is the request here.

6. **MainMenu sprite path.** `computeLayout()` uses raw pixel constants and is what `UnitTests/Ui/MainMenuTests.cpp` MouseClickSelectsRow depends on (click at 640,370 for 1280×720). Default `dpiScale = 1.0f` keeps those tests stable. Sandbox2D should pass `window().dpiScale()`.

7. **First-frame safety.** If `takeDpiChanged()` fires during `ImGuiHost::init`, ignore it (init already used the current scale). If it fires mid-`beginFrame`, defer until the next frame before `NewFrame`.

### Verification

- **Editor and VisualDebugger:** Start on a 100% display, drag onto a 150% (or 200%) display. Fonts stay sharp (not a stretched 15 px bitmap). Rounding, toolbar, and status bar scale. Scene / Assets type icons still appear (heap slot 0 rebuild did not drop slots 1+). Drag back to 100% and confirm the UI shrinks correctly (no compounded `ScaleAllSizes`).
- **Sandbox:** Same ImGui check. Health bar and crosshair grow with DPI and stay anchored (health: lower-right with 28 px pad; crosshair: screen center, slot row under the reticle).
- **Sandbox2D:** Sprite main menu panel/rows scale with DPI; at 100% the 1280×720 unit-test click point is unchanged. No ImGui init.
- **UnitTests:** `MainMenu.*` and `UiPalette` tests still pass.
- Log a single line on rebuild: `ImGuiHost: dpi scale 1.00 -> 1.50, fonts rebuilt`.

---

## Suggested sequence and sizing

| Phase | Depends on | Approx. shape | Risk |
|---|---|---|---|
| 1 Quick fixes | — | 1 short PR | Low. Viewports default change is the only behavioral switch (Editor/Debugger lose OS-torn windows, gain rounding). |
| 2 FreeType + 3 fonts | 1 (glyph generator) | 1 PR + two font files | Medium. First new third-party dep in a while; keep the `DE_ENABLE_FREETYPE` escape hatch. |
| 3 Game chrome + MainMenu ImGui | 2 (heading font, palette) | 1–2 PRs (3a Sandbox/Editor, optional 3b Sandbox2D ImGui) | Medium. Input ownership on the menu is the subtle part. Keep the DarkEngine / UnitTests split sacred. |
| 4 Property grid + chrome | 1–2 | 1 PR, mechanically large (many panels, behavior unchanged) | Low–medium. Dock layout requires an ini invalidation once. |
| 5 Live DPI | 2 (UiFonts, base style) | 1 PR | Medium. Atlas rebuild must not drop `addTexture` slots. Unit tests must stay at scale 1. |

Phase 1 can land immediately, including on a machine that cannot compile HLSL, because it is almost all CPU / ImGui. Phases 2–5 need a Windows / D3D12 worker (`darkengine6-pc` once it is up) for visual sign-off. Nothing in this plan requires a GitHub PR until you ask for one.

---

## Out of scope (on purpose)

- Per-asset glTF thumbnails in the Assets browser (blows the 64-slot SRV heap).
- A full design-system rewrite, custom ImGui branch, or replacing SpritePipeline HUDs (`HealthHud` / `CrosshairHud`) with ImGui.
- Forcing ImGui into `DarkEngine`, `UnitTests`, or (in 3a) `Sandbox2D`.
- Changing gameplay, save/load, or network behavior behind these panels.
