# Performance baselines (render graph Phase 0)

Phase 0 of [the render graph plan](../plans/DarkEngine6-render-graph-plan.md) records **FXC** baselines before any renderer change. Each later phase is checked against them: the image must match, and GPU time must not regress beyond the phase's gate.

Exit gate for Phase 0: baselines are checked in under `docs/perf/baselines/`, and the GPU-based validation (GBV) findings are listed below. They don't have to be fixed.

## What a baseline is

For each host and camera preset there is one folder entry:

| File | From | Contents |
|------|------|----------|
| `<host>_<preset>.png` | `-capture-frame` | Back buffer of the shot frame (RGBA8, alpha forced to 255). No HUD or editor UI. |
| `<host>_<preset>.json` | `-capture-frame` | Timings over the frames before the shot (see below). |
| `<host>_<preset>.wpix` | PIX, by hand | A GPU capture of the same camera. Optional for every preset except `default`. |
| `summary.md` | `capture-baselines.ps1` | One table for the whole run. |

Folder name: `docs/perf/baselines/<yyyy-mm-dd>-<short sha>-fxc-<Debug|Release>/`. Release numbers are the reference. Debug runs are only for the GBV list.

A baseline is valid only if its JSON says `"baselineValid": true`: the shot was written, at least one frame was timed, and the host's frame graph compiled and its pass order matched the host's feature list (`Render/FrameFeatures.h`) on every frame (`passSequenceFailures == 0`).

## Command line

Both `Sandbox.exe` and `Editor.exe` accept these. Parsing lives in the engine (`Core/FrameCapture`, `parseAppCommandLine`).

| Argument | Meaning |
|----------|---------|
| `-capture-frame N` | Shoot frame N (N ≥ 2), counted from the first frame the host reports its scene ready. Implies `-no-splash -no-menu`. |
| `-camera <preset>` | Preset from `content/perf/capture_cameras.json`. Required with `-capture-frame`. |
| `-capture-avg K` | Time the K frames before the shot (default 300, fewer if N − 1 < K). |
| `-capture-out <dir>` | Output folder. Default: `captures/` under the working directory. |
| `-capture-stay` | Keep running after the shot, still pinned: same camera, vision state, paused sim, fixed step and no UI, until the window is closed (for a PIX capture of the same view). |
| `-scene:<file>` | Scene to load. Now works in the Editor too. |
| `-no-vsync` | Present without vsync. Use it for every timing run. |
| `-window WxH` | Window size, e.g. `-window 2560x1440` for the 1440p gate. |

While capturing, the host:

- waits until the scene is ready (Sandbox: boot done, menu closed; Editor: scene loaded, 3D view, not in play mode). It gives up after 600 s;
- pauses the simulation, uses a fixed 1/60 s step, and discards TAA history and the previous view-projection on the first frame (GTAO/SSR history settles during the timed frames);
- re-applies the preset camera (and in Sandbox the vision mode, echo, camouflage and underwater state) every frame;
- hides the HUD and the editor UI;
- quits after writing the PNG and the JSON. With `-capture-stay` it keeps the pinned view instead; there is no return to interactive play, so close the window when done.

### JSON fields

`schemaVersion`, `host`, `preset`, `png`, `shotOk`, `passSequenceFailures`, `baselineValid`, `build`, `version`, `git`, `shaderCompiler` (`"FXC"`), `frame`, `width`, `height`, `vsync`, `framesAveraged`, then `frameMs`, `cpuMs`, `gpuMs` and `drawCalls`, each `{count, mean, min, max, p50, p95}`. `ranges` holds the same stats for every profiler scope, keyed by name and depth.

## Camera presets

`content/perf/capture_cameras.json`. `host` is `sandbox` or `editor` (an empty host matches both). `anchor: "player"` makes `eye`/`target` offsets from the possessed body; `anchor: "world"` makes them positions. With `underwater: true` the y values are relative to the water level.

Sandbox presets: `default`, `night_sight`, `infrared`, `infrared_half`, `ultraviolet`, `ultraviolet_half`, `echo`, `camouflage`, `underwater`. Editor: `default`.

The world positions are first guesses (`underwater` is a placeholder). To pick a better view in Sandbox, open the profiler (P) and press **Log camera as capture preset**; the log gets a JSON line to paste into the file. The Editor ignores the vision fields. **Changing a preset invalidates its baselines.**

## D3D12 diagnostics switches

Read once at device creation (`Render/RendererDiagnostics.cpp`).

| Variable | Default | Effect |
|----------|---------|--------|
| `DE_D3D12_DEBUG=0` | Debug: on. Release: off | Turns the debug layer off in a Debug build. |
| `DE_D3D12_GBV=1` | off | GPU-based validation (`ID3D12Debug1::SetEnableGPUBasedValidation`). Debug builds only; needs the debug layer. Very slow: use a small `-capture-frame`. |
| `DE_D3D12_DRED=0` / `=1` | Debug: on (off when PIX/Nsight is attached). Release: off. `=1` forces it on in any build, even under PIX | DRED auto-breadcrumbs and page-fault reporting. On device removal the log gets the removed reason, the command lists that didn't finish (±6 ops around the failure) and the page-fault address with its allocations. |

The debug layer is skipped when PIX or Nsight is attached, as before, and DRED is too unless `DE_D3D12_DRED=1`. Debug-layer messages are now also written to `DarkEngine6.log` (`D3D12 [SEVERITY #id] ...`, the first 4 of each id), with per-id totals at shutdown.

## Recording a baseline set

On Windows, from the repo root, after a Release build:

```powershell
.\scripts\capture-baselines.ps1 -Config Release
```

That runs every Sandbox preset plus the Editor `default` at 2560×1440 without vsync and collects the files into a new `docs/perf/baselines/` folder with a `summary.md`. Then, by hand:

1. Check every PNG: the right view, mode and no UI.
2. Take a PIX GPU capture of at least `sandbox_default` and `editor_default`: launch under PIX with the same arguments plus `-capture-stay`, and capture after the shot. Save it into the same folder as `<host>_<preset>.wpix`. Large captures can stay out of git; note where they are in `summary.md`.
3. Run a Debug build with `DE_D3D12_GBV=1` and a short run (`-capture-frame 30 -capture-avg 10`) for each preset, then copy the `D3D12 [...]` lines from `DarkEngine6.log` into the list below.

## GBV findings

Not recorded yet: this needs a Windows Debug run (step 3 above). Template:

| # | Host / preset | Severity | Message id | Message (first line) | Pass / scope | Notes |
|---|---------------|----------|-----------|----------------------|--------------|-------|
| | | | | | | |

## Baseline checklist

- [ ] `sandbox_default`
- [ ] `sandbox_underwater` (preset moved over open water first)
- [ ] `sandbox_night_sight`
- [ ] `sandbox_infrared`, `sandbox_infrared_half`
- [ ] `sandbox_ultraviolet`, `sandbox_ultraviolet_half`
- [ ] `sandbox_echo`
- [ ] `sandbox_camouflage`
- [ ] `editor_default`
- [ ] PIX captures for `sandbox_default` and `editor_default`
- [ ] GBV findings listed above
