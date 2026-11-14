# DarkEngine6 UI / ImGui review at `84fa6f5`

- **Repo:** https://github.com/DarkenedGit/DarkEngine6
- **Tip reviewed:** `84fa6f5d95298f97bc6424555eed554dee0de9ef`, "UI all converted to imgui". Committed Thu Nov 12, 2026 at 10:03 PM PST.
- **Baseline:** `35cf523`, the tree the UI plan was written against. Saved at `/workspace/de6-35cf523-b`.
- **Commits in range (4):**
  - `6dc789f` "Bug fix and refactoring", Nov 11 at 9:28 PM PST
  - `fe0415d` "Menu sounds and code refactoring", Nov 11 at 10:44 PM PST
  - `91aca11` "UI Refactor", Nov 12 at 10:00 PM PST
  - `84fa6f5` "UI all converted to imgui", Nov 12 at 10:03 PM PST
- **Size:** 106 files changed.
- **UI-relevant files:**
  - `Ui/*`, `Editor/EditorUi.cpp`, `Editor/EditorPlay.cpp`, `Editor/EditorSceneFile.cpp`, `Editor/EditorAssetBrowser.cpp`, `Editor/EditorTypeIcon.h`
  - `Sandbox/SandboxHud.cpp` (this was `DevToolsPanel.cpp`), `Sandbox/SandboxFrame.cpp`, `Sandbox/SandboxApp.cpp`
  - `Sandbox2D/Sandbox2DApp.cpp`, `Play/DialogueHud.h`, `Render/HealthHud.cpp`, `Render/CrosshairHud.cpp`
  - `Core/Window.*`, `CMakeLists.txt`, `content/fonts/*`, `UnitTests/Ui/MainMenuTests.cpp`
- **Method:** a static review only. I read the code and diffed it against the baseline. Nothing was built or run, since there's no MSVC or D3D12 here.
- **Dear ImGui:** the build pins `v1.91.8-docking`. I checked the ImGui behaviour claims below against the sources of that tag: `imgui.cpp`, `imgui_draw.cpp`, `backends/imgui_impl_dx12.cpp` and `backends/imgui_impl_win32.cpp`.
- **Repo state:** I didn't modify the repo, open a PR or post any comment.

Link base: `https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/`

---

## 1. Status of each phase against the plan

| Phase | Status | Summary |
|---|---|---|
| 1 Quick fixes | **Partial.** The code is done, but the main visual goal isn't met. | The glyph ranges, PNG icons and theme tokens are done. The API now separates docking from viewports, but the Editor and VisualDebugger turn viewports back on, so their floating windows are still square. |
| 2 FreeType + 3 fonts | **Done** | FreeType comes in through FetchContent behind a `DE_ENABLE_FREETYPE` switch. It loads Roboto-Medium, Roboto-Bold and Cousine, `PixelSnapH` is off, and almost all the font-scale hacks are gone. |
| 3a Game chrome + ImGui MainMenu | **Done, with bugs** | `Ui/ImGuiGameChrome` is in, and the Sandbox menu, pause card, Inventory, Loot, Upgrades, Journal, `##playhud`, Dialogue and Quest plate all use it. The input handoff has a P1 and two P2 bugs (§2). |
| 3b Sandbox2D on ImGui | **Not started** (optional) | Sandbox2D keeps the sprite menu and now has DPI scaling and sounds. It doesn't link ImGui, which matches the plan. |
| 4 Property grid + chrome | **Partial** | `Ui/ImGuiWidgets` is in and has about 230 call sites. The Editor menus are split, and the toolbar, docked Lighting tabs and Dev Tools tabs are done. Not done: the Sandbox still has `docking=false`, Terrain and Particle panels still use raw widgets, and the status bar is still one truncated string. |
| 5 Live DPI | **Mostly done** | `takeDpiChanged`, the base style, the atlas rebuild that keeps slot 0, and DPI scaling of the sprite HUDs and MainMenu are in. Fixed pixel sizes in ImGui code still don't scale. |

### Phase 1: evidence

- **Glyph ranges come from `Icons.h`:**
  - An X-macro list, `DE_UI_ICON_STRINGS`, is at [Ui/Icons.h#L36](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/Icons.h#L36).
  - It is decoded, sorted and merged in [Ui/ImGuiHost.cpp#L95](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiHost.cpp#L95).
  - `ICON_FA_CLOUD` (U+F0C2) is now included. **Done.**
  - Remaining risk: a new `#define` that nobody adds to the X-list is still silently skipped. A `static_assert` or a unit test that counts the defines would close that.
- **Docking is separate from viewports:**
  - `init(..., bool viewports = false)` is at [Ui/ImGuiHost.h#L33](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiHost.h#L33) and [Ui/ImGuiHost.cpp#L282-L298](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiHost.cpp#L282-L298).
  - But **both hosts opt back in:** [Editor/EditorAppInit.cpp#L175](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorAppInit.cpp#L175) and [VisualDebugger/DebuggerApp.cpp#L89](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/VisualDebugger/DebuggerApp.cpp#L89) pass `viewports=true`.
  - So `WindowRounding = 0` and an opaque `WindowBg` are still forced ([ImGuiHost.cpp#L294-L298](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiHost.cpp#L294-L298)).
  - The Editor and Debugger therefore still draw square windows. The exception is game cards, which push their own rounding.
  - This may be deliberate if Travis wants torn-off OS windows, but the visual change the plan aimed for didn't happen.
- **PNG type icons** are used in the Scene tree ([Editor/EditorUi.cpp#L547](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorUi.cpp#L547) and L563–566). They are also used in Assets:
  - the primitive rows ([EditorAssetBrowser.cpp#L317](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorAssetBrowser.cpp#L317))
  - folders ([#L371](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorAssetBrowser.cpp#L371))
  - files ([#L389](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorAssetBrowser.cpp#L389))
- **Icon helpers:** `iconForType` moved into the shared [Editor/EditorTypeIcon.h](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorTypeIcon.h). `file.png` and `folder.png` are now loaded ([Editor/EditorApp.cpp#L86-L98](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorApp.cpp#L86-L98)). **Done.**
  - The five optional PNGs are still missing: water, cloud_volume, stream, spawner and movie. Those rows fall back to Font Awesome and log five "missing UI icon" warnings at every startup.
  - The heap uses 18 of 64 slots.
- **Theme tokens:** `FrameBorderSize=1`, the hover delays, `SeparatorTextPadding` and `TabBarOverlineSize=2` are set at [Ui/ImGuiTheme.cpp#L57-L61](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiTheme.cpp#L57-L61). Tooltips use `SetItemTooltip` and `DelayShort`. **Done.**

### Phase 2: evidence

- **CMake:**
  - `option(DE_ENABLE_FREETYPE ... ON)` is at [CMakeLists.txt#L243](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/CMakeLists.txt#L243).
  - The FreeType `VER-2-13-3` FetchContent block (zlib, bzip2, png, harfbuzz and brotli disabled) is at [#L275-L292](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/CMakeLists.txt#L275-L292). It adds `imgui_freetype.cpp`, sets a PUBLIC `IMGUI_ENABLE_FREETYPE` and links `freetype` PUBLIC.
- **Build checks that pass:**
  - FreeType 2.13.3 declares `cmake_minimum_required(VERSION 3.0...3.5)`, so CMake 4.x accepts it.
  - Its target exports its include directories PUBLIC.
  - The global `/MD` runtime (`CMAKE_MSVC_RUNTIME_LIBRARY`, L10) applies to it.
  - `/W4 /WX` only applies to the four exe targets through `de_set_compiler_options`, so neither `imgui_editor` nor FreeType gets `/WX`.
  - **I found nothing that would break the build.**
- **Fonts:**
  - `UiFonts {body, heading, mono}` is defined at [Ui/ImGuiHost.h#L17-L26](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiHost.h#L17-L26) and loaded at [ImGuiHost.cpp#L164-L214](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiHost.cpp#L164-L214).
  - `content/fonts/Roboto-Bold.ttf`, `Cousine-Regular.ttf` and the LICENSE update are added. Under FreeType, `PixelSnapH=false`.
- **Font-scale hacks:** the 11 `GetFontSize()*k` sites are down to 2. The remaining ones are `font->FontSize * 1.1f` in [SandboxHud.cpp#L257](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox/SandboxHud.cpp#L257) and [#L895](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox/SandboxHud.cpp#L895).
- **Design gaps:**
  - The heading and mono faces only load `#ifdef IMGUI_ENABLE_FREETYPE` ([L184-L204](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiHost.cpp#L184-L204)). With `DE_ENABLE_FREETYPE=OFF`, every heading collapses to body size. stb_truetype could load Roboto-Bold and Cousine just as well.
  - Mono has no icons merged in.

### Phase 3a: evidence

- **Shared chrome:** [Ui/ImGuiGameChrome.h](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiGameChrome.h) and `.cpp` contain:
  - `BeginGameCard`/`EndGameCard` ([L24-L46](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiGameChrome.cpp#L24-L46))
  - `drawMainMenuImGui` ([L48-L132](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiGameChrome.cpp#L48-L132))
  - the shared quest lines, quest plate and objective diamond, which removes the Editor and Sandbox copies
- **MainMenu input API:** the menu keeps its state and its sprite path in DarkEngine. A new `moveTo`/`confirm`/`quit` API was added for ImGui hosts ([Ui/MainMenu.cpp#L218-L242](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/MainMenu.cpp#L218-L242)).
- **Sandbox wiring:**
  - When ImGui is ready, the Sandbox skips the sprite `update()` ([SandboxApp.cpp#L682-L686](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox/SandboxApp.cpp#L682-L686)), so the mouse doesn't trigger both menus.
  - It draws the ImGui menu on the frames before the scene starts and on in-game frames ([SandboxFrame.cpp#L326-L334](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox/SandboxFrame.cpp#L326-L334), [#L1444-L1447](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox/SandboxFrame.cpp#L1444-L1447)).
- **Cards in use:**
  - **Pause card:** [SandboxHud.cpp#L1147-L1176](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox/SandboxHud.cpp#L1147-L1176)
  - **Inventory, Upgrades, Journal and Loot:** [L135](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox/SandboxHud.cpp#L135), [L290](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox/SandboxHud.cpp#L290), [L344](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox/SandboxHud.cpp#L344), [L975](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox/SandboxHud.cpp#L975)
  - **Editor `##playhud`, Upgrades and Loot:** [EditorPlay.cpp#L1102](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorPlay.cpp#L1102), [L795](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorPlay.cpp#L795), [L1237](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorPlay.cpp#L1237)
  - **Dialogue:** [Play/DialogueHud.h#L115](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Play/DialogueHud.h#L115)
- **ImGui stays out of DarkEngine and UnitTests:**
  - `DialogueHud.h` is only included by the Editor and Sandbox exes.
  - Nothing under `UnitTests/` refers to ImGui.
  - `cmake/DarkEngineTargets.cmake` documents the split.
- **Hardcoded colours:** `IM_COL32` literals that aren't black went from 65 to 17. The rest go through `toImU32(UiPalette::…)`.
- **Problems:** see §2 (P1-1, P2-1, P2-2, P2-3).

### Phase 3b

Not started, as planned. Sandbox2D gained `m_menu.loadSounds` and `setDpiScale(window().dpiScale())` ([Sandbox2DApp.cpp#L1248](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox2D/Sandbox2DApp.cpp#L1248), [#L1464](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox2D/Sandbox2DApp.cpp#L1464)), and it doesn't link ImGui.

### Phase 4: evidence

- **`Ui/ImGuiWidgets`:** the code is at [Ui/ImGuiWidgets.cpp](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiWidgets.cpp). It provides a 2-column `BeginTable` row, a muted label, `SetNextItemWidth(-1)` and `SeparatorText`.
- **Adoption:**

  | File | Prop calls | Raw widgets left |
  |---|---|---|
  | EditorUi.cpp | 87 | 6 |
  | SandboxHud.cpp | 123 | 10 |
  | EditorModel.cpp | 8 | 0 |
  | EditorPhysics.cpp | 12 | 0 |
  | **EditorTerrain.cpp** | **0** | **40** |
  | **ParticleEditorPanel.cpp** | **0** | **35** |
  | VisualDebugger | none | not counted |

- **Editor chrome:**
  - `View` is split into Panels, Viewport and Rendering ([EditorUi.cpp#L105-L201](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorUi.cpp#L105-L201)).
  - There is a new toolbar with Play, Save, 2D/3D, Grid and Fly ([#L315-L356](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorUi.cpp#L315-L356)).
  - A docked "Lighting" window has IBL, SSAO, SSR and Sky tabs ([#L362-L454](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorUi.cpp#L362-L454)).
  - The dock ID was bumped to `EditorDockV2`, and Lighting is docked right ([Ui/ImGuiTheme.cpp#L198-L213](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiTheme.cpp#L198-L213)). Users no longer have to delete their ini file.
- **Status bar** ([EditorSceneFile.cpp#L819-L866](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorSceneFile.cpp#L819-L866)): the FPS is now right-aligned in the mono font and the left side gets an ellipsis. It is still one concatenated string, not aligned segments, so this item is **partial**.
- **Sandbox chrome:**
  - Dev Tools now has Rendering, Gameplay and Network tabs ([SandboxHud.cpp#L1227-L2062](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox/SandboxHud.cpp#L1227-L2062)). **Done.**
  - Sandbox docking is still `false` ([SandboxApp.cpp#L1906](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox/SandboxApp.cpp#L1906)). **Not done.**

### Phase 5: evidence

- **Window:** `Window::takeDpiChanged()` and `m_dpiChanged` are set in `WM_DPICHANGED` ([Core/Window.cpp#L272-L273](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Core/Window.cpp#L272-L273), [#L463-L468](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Core/Window.cpp#L463-L468)).
- **ImGuiHost:**
  - `baseStyle` is captured before scaling ([ImGuiHost.cpp#L300-L307](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiHost.cpp#L300-L307)).
  - `syncDpi()` runs in `beginFrame()` before `ImGui_ImplDX12_NewFrame`. It restores the base style, scales once, calls `waitForGpu`, clears and reloads the fonts, then calls `InvalidateDeviceObjects`/`CreateDeviceObjects` ([#L376-L412](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiHost.cpp#L376-L412)).
  - **The style isn't double-scaled.** `ScaleAllSizes` always starts from `baseStyle`, and nothing else in the repo calls `ScaleAllSizes` or `FontGlobalScale`.
- **SRV slots:**
  - A custom `SrvDescriptorAllocFn`/`FreeFn` pins the font to slot 0 ([#L334-L339](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiHost.cpp#L334-L339)).
  - In the 1.91.8 DX12 backend, `AllocFn` is only called from `Init()`, and `InvalidateDeviceObjects` calls `FreeFn`. The no-op free therefore keeps slot 0 valid across rebuilds, and the `addTexture` slots 1 and up are untouched.
  - The reasoning in the code comment checks out against the backend source. **Correct.**
- **Sprite HUDs:**
  - `HealthHud` and `CrosshairHud` scale by DPI, and the health bar is clamped to the view ([HealthHud.cpp#L72-L86](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Render/HealthHud.cpp#L72-L86)).
  - `MainMenu::computeLayout` uses `m_dpiScale`, which defaults to 1 ([MainMenu.cpp#L295-L345](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/MainMenu.cpp#L295-L345)).
  - Tests `DpiScaleOneKeepsMouseHit` and `DpiScaleEnlargesRows` were added ([MainMenuTests.cpp#L103](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/UnitTests/Ui/MainMenuTests.cpp#L103), [#L122](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/UnitTests/Ui/MainMenuTests.cpp#L122)).
- **Gap:** fixed pixel sizes in ImGui code don't scale, because `ScaleAllSizes` doesn't touch them. Examples:
  - the menu width of 560 ([ImGuiGameChrome.cpp#L61](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiGameChrome.cpp#L61))
  - the Dialogue card at 420 wide, offset 220 and 150 ([DialogueHud.h#L113](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Play/DialogueHud.h#L113))
  - Dev Tools at 460×720, Inventory at 380×420 and the Lighting window at 420×560
  - the profiler columns of 72 px, the 4 px card accent strip and the card rounding of 6
  - At 150 % these windows look small next to the 22.5 px text.

---

## 2. Confirmed bugs in the new UI code

"Confirmed" means I traced the code path in this tree and checked the ImGui behaviour in the v1.91.8 sources. A run on Windows still needs to show each one.

### P1-1: Pressing Esc (or gamepad Back) in-game quits the Sandbox instead of opening the pause menu (regression)

1. **The main loop order** is `pollEvents → onUpdate → onRender` ([Core/Application.cpp#L591-L645](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Core/Application.cpp#L591-L645)).
2. **In `onUpdate`,** the `"quit"` action (Escape or Back, from `content/sandbox.input.json`) calls `m_hud.m_menu.show()` and returns ([SandboxApp.cpp#L888-L905](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox/SandboxApp.cpp#L888-L905)). `showMainMenu` is on by default in `Sandbox/main.cpp`.
3. **In `onRender` of the same frame,** `drawMainMenuImGui` runs ([SandboxFrame.cpp#L1444-L1447](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox/SandboxFrame.cpp#L1444-L1447)).
   - ImGui's `NewFrame` processed the same `WM_KEYDOWN VK_ESCAPE` that was queued during `pollEvents`, so `IsKeyPressed(ImGuiKey_Escape)` is true on this frame.
   - The card calls `SetNextWindowFocus()` and is newly appearing, so `Begin` focuses it and `IsWindowFocused(RootAndChildWindows)` is true.
   - Then `menu.quit()` runs ([ImGuiGameChrome.cpp#L125-L129](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiGameChrome.cpp#L125-L129)).
4. **On the next `onUpdate`,** `pollResult()` returns `Quit` and the game calls `requestQuit()` ([SandboxApp.cpp#L724-L727](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox/SandboxApp.cpp#L724-L727)).

- **Gamepad Back** does the same thing. The Win32 backend feeds XInput every frame no matter what `NavEnableGamepad` is set to, and `ImGuiKey_GamepadBack` is also in that `quit` check.
- **At 35cf523** the sprite menu only read input in the next frame's `update()`, when the key edge had already cleared. So one Esc paused the game, and a second Esc quit it.
- **Fix:** don't react to Esc, B or Back on the frame the card appears (`if (!ImGui::IsWindowAppearing() && ...)`). Better still, have the Sandbox record the frame it called `show()` and ignore the cancel keys on that frame.
- **Design suggestion:** when the scene has already started, Esc or B in the pause menu should `hide()` (resume), not quit.

### P2-1: Opening the ImGui main menu resets the preselected row to 0

- **Code:** [ImGuiGameChrome.cpp#L62](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiGameChrome.cpp#L62) and [#L104-L105](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiGameChrome.cpp#L104-L105).
- **Why:**
  - When the window appears, `Begin` calls `FocusWindow` and then `NavInitWindow` (imgui.cpp v1.91.8, around L8127–8130).
  - `NavInitWindow` requests a default focus, and with no `SetItemDefaultFocus()` that goes to the first submitted item.
  - On the next frame `NavId == row 0`, and `IsItemFocused() && !selected` calls `menu.moveTo(0)`.
- **Effect:**
  - The scene that `selectById(sceneFileArg())` picked ([SandboxApp.cpp#L657](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox/SandboxApp.cpp#L657)) is overwritten with "Play".
  - The pause menu also forgets the last row and plays a move sound when it opens.
- **Fix:** call `ImGui::SetItemDefaultFocus()` straight after the `Selectable` of the selected row. Only sync from `IsItemFocused` after the window has existed for a frame, or drive the selection explicitly as described in P2-2.

### P2-2: In the ImGui menu, Enter and Space do nothing until an arrow key is pressed, and WASD no longer moves the selection

- **Enter, Space and gamepad A:**
  - In 1.91.8, nav activation requires `g.NavCursorVisible` (imgui.cpp around L13676).
  - An init request that isn't from a move leaves the cursor hidden. It only becomes visible after a directional key, through `ConfigNavCursorVisibleAuto`.
  - So the first Enter after the menu opens doesn't confirm.
  - The sprite menu used to confirm the highlighted row on Enter, Space or A right away ([MainMenu.cpp#L372](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/MainMenu.cpp#L372)).
- **W/S:**
  - The Sandbox no longer calls `MainMenu::update()` when ImGui is ready, and ImGui's keyboard nav only uses the arrow keys.
  - W/S are no longer handled, yet the default hint still says "Arrows / WASD    Enter    Esc" ([ImGuiGameChrome.cpp#L120](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiGameChrome.cpp#L120)).
  - Hovering with the mouse also no longer moves the highlight.
- **Fix (pick one):**
  - When the menu appears, call `ImGui::SetNavCursorVisible(true)` (the API exists in 1.91.8) together with `SetItemDefaultFocus`.
  - Or handle Up, Down, W, S, Enter, Space, A and B yourself with `ImGui::IsKeyPressed` while the card is focused, calling `menu.moveTo` or `menu.confirm`, and keep ImGui nav just for the visuals.
- **Tests:** `MainMenuTests` has none for the new `moveTo`, `confirm` and `quit` API. A small CPU test would lock down the behaviour that doesn't involve ImGui.

### P2-3: Game cards have no title bar, so Loot, Inventory, Upgrades and Journal lost their close button, title and drag handle (regression)

- **Code:** `BeginGameCard` always adds `ImGuiWindowFlags_NoTitleBar` ([ImGuiGameChrome.cpp#L28](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiGameChrome.cpp#L28)).
- **Close button:** `p_open` is only drawn as the title-bar X, so passing `&open` or `&m_show*` now does nothing.
- **Loot** ([SandboxHud.cpp#L973-L1000](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox/SandboxHud.cpp#L973-L1000); Editor [EditorPlay.cpp#L1234-L1262](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorPlay.cpp#L1234-L1262)):
  - `open` stays `true`, so `settleLootTarget(..., open)` never closes the window.
  - The only ways out are taking everything or walking more than 3 m away.
  - While a loot target is set, `uiOpen()` stays true, which keeps the cursor uncaptured.
- **Inventory, Upgrades and Journal:**
  - They can only be closed with their hotkeys.
  - Their window title ("Inventory" and so on) isn't drawn anywhere.
  - They can't be moved, because `io.ConfigWindowsMoveFromTitleBarOnly = true` ([ImGuiHost.cpp#L289](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiHost.cpp#L289)) and there's no title bar to drag.
- **Fix:** have `BeginGameCard` take a visible title. Draw it in the heading font, add a small close button on the right when `open != nullptr`, and drop `NoTitleBar` for movable cards. A quick alternative is a "Close" button in the Loot card.

### P2-4 (pre-existing, now more likely to trigger): "2D Level" calls `End()` only when `Begin()` returned true

- **Code:** `if (m_sceneMode == SceneMode::Scene2D && ImGui::Begin("2D Level")) { ... ImGui::End(); }` at [EditorUi.cpp#L614-L652](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorUi.cpp#L614-L652).
- **Why it fails:** in a docked layout, `Begin` returns false for a tab that isn't selected, and `End` must still be called. Otherwise ImGui hits the "Missing End()" assert, and in Release the window stack gets corrupted.
- **Why it's more likely now:** "2D Level" shares the right dock node with Inspector, Lighting and Particle System ([ImGuiTheme.cpp#L209-L215](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiTheme.cpp#L209-L215)). The new `EditorDockV2` layout also rebuilds for every user.
- **Fix:** use the same pattern as everywhere else, `if (Begin) {...} End();`.

### P3-1: The toolbar is taller than the space reserved for it, so it overlaps the dock area

- **Code:** the toolbar window gets `SetNextWindowSize(w, GetFrameHeight())` ([EditorUi.cpp#L317-L321](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorUi.cpp#L317-L321)), and the dock host reserves exactly `GetFrameHeight()` at the top ([#L360](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorUi.cpp#L360)).
- **Why:** `CalcWindowSizeAfterConstraint` clamps windows that aren't children or `AlwaysAutoResize` to `style.WindowMinSize`, which defaults to 32×32 and is DPI-scaled.
- **Effect:** at 100 % the toolbar is about 32 px tall against a 23 px reservation, so roughly 9 px slide under the docked panels' tab bars. It's about 13 px at 150 %.
  - The content needs `2 + FrameHeight + 2` px.
- **Fix:** before `Begin`, call `PushStyleVar(ImGuiStyleVar_WindowMinSize, {0,0})`. Then compute `barH = GetFrameHeight() + 2*padY` once and pass that same `barH` to `beginPassthruDockSpace`.
- The status bar has the same clamp and has been about 9 px too tall (running off-screen) since before this change.

### P3-2: The Editor `##playhud` is at absolute (16, 48) while viewports are on (pre-existing, made worse)

- **Code:** [EditorPlay.cpp#L1099](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorPlay.cpp#L1099).
- **Off-screen case:** with `ViewportsEnable`, ImGui positions are desktop coordinates. If the Editor isn't at the desktop origin, the HUD lands outside the main viewport and becomes its own OS window.
- **Maximized case:** it now overlaps the new toolbar, which spans y ≈ 23–55.
- **Fix:** position it from `GetMainViewport()->WorkPos` plus the toolbar height.

### P3-3: Banners are about a third smaller (regression)

| Text | At 35cf523 | Now |
|---|---|---|
| ALARM | 15 × 2.2 = 33 px | heading 20 × 1.1 = 22 px |
| OBJECTIVE COMPLETE | 15 × 2.6 = 39 px | 22 px |
| PARRY | 33 px | 22 px ([EditorPlay.cpp#L1089-L1091](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorPlay.cpp#L1089-L1091)) |

- `CalcTextSizeA` at 1.1× an atlas size also upscales the bitmap, so the text is slightly soft.
- **Fix:** add a fourth "display" face at about 32 px, for banners only.

### P3-4: Smaller problems I confirmed

- **Menu row geometry** ([ImGuiGameChrome.cpp#L96-L114](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiGameChrome.cpp#L96-L114)):
  - Rows without a subtitle, such as Quit, use `rowH = 0`, which ImGui turns into one font height, about 15 px.
  - The text is drawn at `+FramePadding.y` from the top, so it sits about 4 px below the highlight and runs into the next row.
  - The plan's sprite menu used 56 px rows. Use an explicit height, such as `GetFrameHeight()*1.6`, and centre the text vertically.
- **The card's accent strip has square corners** while the card has 6 px rounding ([L35](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiGameChrome.cpp#L35)). Use `AddRectFilled(..., rounding, ImDrawFlags_RoundCornersTop)`.
- **The status bar ellipsis cuts by bytes** with `pop_back` ([EditorSceneFile.cpp#L848](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorSceneFile.cpp#L848)). A non-ASCII scene name can be cut in the middle of a UTF-8 sequence. It also calls `CalcTextSize` up to n² times per frame.
- **Stale help text:** "View → Cloud Volumes" ([EditorUi.cpp#L732](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorUi.cpp#L732)). That toggle now lives under **Rendering** ([L183](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Editor/EditorUi.cpp#L183)).
- **`setGameNavEnabled` calls `ImGui::GetIO()` with no context check** ([ImGuiGameChrome.cpp#L15-L22](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiGameChrome.cpp#L15-L22)).
  - The Sandbox calls it every frame without checking `isReady()` ([SandboxFrame.cpp#L326](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox/SandboxFrame.cpp#L326), [#L346](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox/SandboxFrame.cpp#L346)).
  - If `ImGuiHost::init` fails before `CreateContext` (the "missing window/device" path, [ImGuiHost.cpp#L272-L276](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Ui/ImGuiHost.cpp#L272-L276)), that dereferences a null context. The path is rare, but guard it with `ImGui::GetCurrentContext()`.
- **`ImGuiHost::init` leaks on early failure** (pre-existing). It allocates `m_impl` and creates the context before the checks that can fail, and `shutdown()` returns early when `!m_ready`.

### What I checked and found clean

- **Begin/End and Push/Pop balance:**
  - `BeginGameCard`/`EndGameCard` pairs on every early return, in Sandbox Inventory, Upgrades, Journal, Loot and Pause, Editor Upgrades, playhud and Loot, and Dialogue.
  - The Profiler mono `PushFont`/`PopFont`, the Dev Tools tab bar (no early returns inside the tabs), the Lighting tab items, `drawMainMenuImGui` fonts, the `BeginPropTable`/`EndPropTable` callers (they only end when begin returned true) and the toolbar `PushStyleVar(2)`/`PopStyleVar(2)`.
- **ImGui ID collisions in the property grid:**
  - Repeated labels ("Enabled", "Intensity", "Debug view") are either under separate `PushID` scopes (SSAO, SSR, Exposure, CloudLOD), in separate tab items, or in exclusive `if/else` inspector branches.
  - `PropRow` pushes the label as its ID.
  - None of the 230 call sites pass a label containing `##`. The helper would print the `##suffix` as visible text, so keep it that way.
- **DX12 font rebuild:** `waitForGpu()` runs before `InvalidateDeviceObjects`, and it happens between frames, before `NewFrame`. The font texture is recreated through the backend's own upload queue and fence, and slot 0 stays pinned.
- **ImGui stays out of DarkEngine and UnitTests,** and there are no exceptions (`throw`, `try` or `.at()`) in the new UI code.
- **The FreeType CMake integration** (the CRT, warnings, include directories and the CMake 4 minimum) is fine. See Phase 2 above.

---

## 3. Risks I couldn't confirm by reading the code

- **R-1: DPI rebuild while viewports are on.**
  - `syncDpi` only drains the main renderer queue.
  - In 1.91.8, secondary-viewport windows render on their own `vd->CommandQueue`, and the backend doesn't block the CPU on them each frame.
  - If a torn-off Editor or Debugger window is open when the main window changes DPI, the old font texture could be released while a secondary queue is still sampling it.
  - This depends on timing. If you keep viewports, call `ImGui_ImplDX12_InvalidateDeviceObjects` only after waiting on every viewport's fence; destroying and recreating platform windows would also work.
- **R-2: Dialogue choices could be committed twice** (pre-existing). The Sandbox handles Up, Down and Enter for choices ([SandboxApp.cpp#L1001-L1009](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox/SandboxApp.cpp#L1001-L1009)), and the ImGui `Selectable` rows also activate on Enter once the nav cursor is visible. In practice the next node's labels differ, so the second activation usually misses.
- **R-3: A missing Windows fallback font triggers a Debug assert.** If `segoeui.ttf` or `consola.ttf` is missing, `AddFontFromFileTTF` hits `IM_ASSERT_USER_ERROR("Could not load font file!")` in Debug before it returns null. Content fonts are checked with `findContentFile` first, so this only affects the Windows fallbacks.
- **R-4: The Phase 1 glyph list is maintained by hand** (see §1).

---

## 4. Remaining visual and UX gaps against the plan

- **Rounded floating windows in the Editor and Debugger.** Viewports are still on (Phase 1).
- **Sandbox docking is still off.** Dev Tools, the Profiler and the cards float (Phase 4).
- **Terrain and Particle panels** still use raw label-and-widget stacks (75 widgets). The VisualDebugger doesn't use the property grid.
- **The status bar is one ellipsized string,** not aligned segments.
- **ImGui pixel constants don't scale with DPI** (Phase 5).
- **There's no Panels → Lighting toggle,** and the Lighting window has no close (`p_open`). It's always shown in 3D.
- **Font Awesome fallback rows and PNG rows don't line up:** PNG rows have an image followed by `SameLine`, and fallback rows have an inline glyph. The 5 missing PNGs are part of this.
- **No 4th display font** for banners, so banners shrank (P3-3).
- **The pause card** ([SandboxHud.cpp#L1147](https://github.com/DarkenedGit/DarkEngine6/blob/84fa6f5d95298f97bc6424555eed554dee0de9ef/Sandbox/SandboxHud.cpp#L1147)) is centred with `ImGuiCond_Appearing`, so it doesn't re-centre after a resize. It doesn't take nav focus either.
- **Housekeeping:** `scripts/__pycache__/gen_audio_wavs.cpython-313.pyc` was committed. Add `__pycache__/` to `.gitignore`.

### Regressions compared with 35cf523

1. **P1-1:** a single Esc or Back quits the Sandbox instead of pausing.
2. **P2-1 / P2-2:** the menu loses the preselected row, Enter needs an arrow first, WASD doesn't work, and hovering doesn't highlight.
3. **P2-3:** Loot can't be closed, and the cards have no title, X or drag handle.
4. **P3-1:** the toolbar overlaps the dock area.
5. **P3-3:** banners and parry text are about a third smaller.

---

## 5. Next quick wins, in order (each is small)

1. **Menu input fix** (P1-1 and P2-1/P2-2), all in `drawMainMenuImGui`:
   - Ignore Esc, B and Back while `IsWindowAppearing()`.
   - Call `SetItemDefaultFocus()` on the selected row and `SetNavCursorVisible(true)` when the window appears.
   - Handle W/S yourself.
   - In the Sandbox, make Esc in the pause menu `hide()`. Add CPU tests for `moveTo`, `confirm` and `quit`.
2. **Card titles and close buttons** (P2-3). Add a `const char* title` parameter to `BeginGameCard`. Draw it in the heading font with an X button when `open` is set, and let the card be moved from that header row.
3. **"2D Level" End()** (P2-4). Move `End()` out of the `if`.
4. **Toolbar height** (P3-1). Push `WindowMinSize = {0,0}`, and reserve the toolbar's real height for the dock host. Anchor `##playhud` to `WorkPos` plus the toolbar height.
5. **Decide on viewports.** If torn-off windows aren't needed, pass `false` from the Editor and Debugger to get the rounded windows the plan intended. That also removes risk R-1.
6. **A `UiFonts::display` face** at about 30–32 px for ALARM, OBJECTIVE COMPLETE and PARRY. Load heading and mono in the non-FreeType build too.
7. **A small `uiPx(float)` helper** (base px × the host's DPI) for every `SetNextWindowSize`, `SetNextWindowPos` offset and fixed column width.
8. **Small fixes:**
   - Fix the "View → Cloud Volumes" text.
   - Cut the status bar on UTF-8 boundaries, or split it into a 3-column table.
   - Round the card's accent-strip corners.
   - Guard `setGameNavEnabled` with a null-context check.
   - Add the 5 missing type PNGs.
9. **Phase 4 follow-up:** move `EditorTerrain.cpp` and `ParticleEditorPanel.cpp` onto `Prop*`, and turn on Sandbox docking (without viewports) with a fresh `sandbox_imgui.ini` layout.

All of the above needs one visual pass on Windows/D3D12 when the `darkengine6-pc` worker is up.
