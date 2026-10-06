# Cloud system

| Field | Value |
|-------|-------|
| **Status** | Implemented |
| **Date** | 2026-10-06 (updated: lightweight cloud layer) |
| **Area** | **New:** `Sky/Environment.*` (CloudLayerDesc), `Render/SkyPipeline.*`, `content/shaders/CloudLayer.hlsli`, Editor Sky panel. **Legacy:** `Sky/CloudVolume.*`, `Render/CloudVolumePipeline.*`, `Render/CloudVolumeGpuList.*`, `content/shaders/CloudVolume.hlsl`, Sandbox Cloud LOD panel |
| **Audience** | Engine and Sandbox owners who already know this tree |

DarkEngine6 now has **two cloud rendering paths**:

1. **Lightweight distant cloud layer (default, new in 2026-10-06):** A 2.5D curved shell evaluated inside the sky pass. One shell-ray hit, 2D noise, 3 sun taps, Beer–Lambert + dual-lobe HG + multi-scatter octaves + powder + aerial perspective. Reacts to sky color and day/night instantly. ~15 2D noise evaluations per sky pixel (or ~6 texture fetches in phase 2). **Enabled by default.**

2. **Placed participating-media volumes (legacy, optional):** Each placed volume is ray-marched in one fullscreen pixel pass and composited into the HDR target. Distance LOD keeps that march on the original fixed step count for nearby chords, and spends a coarse tail on the part of a ray that runs past the detail distance. **Disabled by default;** available for close-range hero shots.

---

## Lightweight distant cloud layer (default)

### Overview

A lit 2.5D cloud layer at altitude $h$ (default 2 km) with thickness $H$ (default 800 m), wrapped on a sphere of radius $R$ (6.36×10⁶ m). Evaluated per sky pixel inside `EvaluateSky` (`content/shaders/SkyEval.hlsli` + `CloudLayer.hlsli`). No history buffer, no separate pass, no ray march. Replaces the flat noise plane that was at the bottom of the old `EvaluateSky` with a physically curved shell.

**Compositing order per sky pixel:**
1. Base sky (gradient, Rayleigh/Mie glow, moon glow, dusk band, overcast) → `skyNoDisc`
2. Add sun disc
3. Composite clouds: `sky·T + inscatter` (aerial-perspective target is `skyNoDisc`)
4. Multiply by `exposure`
5. Apply volumetric fog (280 m stand-in)
6. Encode to sRGB / output

The clouds land **before** fog and **before** exposure, so they inherit the same post chain (bloom, TAA, tonemap) as the rest of the sky.

### Technique

**Shell intersection:** Stable fp32 ray/sphere formula (see brief §3.1). Camera height `y0` is clamped below the shell (`y0 < h − 1 m`) for distant-only rendering. Returns distance `t` and hit cosine `muHit`.

**Density:** 2D coverage/density field `d(x,z) ∈ [0,1]` at the hit point. Coverage remap (HZD-style): `d = sat((n_base − (1−c)) / max(c, 0.05))`, then erode thin edges with detail noise. Phase 1 uses 3-octave 2D value noise (ALU, `Hash21`/`Noise2` from `SkyEval.hlsli`). Phase 2 (future) will use tileable Perlin-Worley textures (BC4, 512² base + 256² detail).

**View opacity:** Beer–Lambert through the column: `τ_v = τ_max · d / max(μ_hit, 0.05)`, `T_v = exp(−τ_v)`.

**Sun optical depth (2D march):** 3 taps along the sun direction projected onto the layer. Phenomenological: edges facing the sun see empty neighbors and stay bright. Low sun means longer reach and longer shadows. Returns `τ_s`.

**Phase:** Dual-lobe Henyey–Greenstein (Frostbite 2016), used **relative to isotropic** (×4π) so an isotropic cloud returns `E·T`. Default: `g0 = 0.45`, `g1 = −0.16`, back weight `w = 0.32`, silver lining (optional `pow(cosθ, 8)` lobe).

**Multiple scattering:** Wrenninge et al. octaves (Frostbite eqs. 19–20): `L_sun = E_light · Σ a^n exp(−b^n τ_s) · 4π p_dual(θ, c^n g)`. Keep `a ≤ b` for energy conservation.

**Powder:** HZD's dark sun-facing edges, applied only when looking away from the sun: `P = lerp(1, 1 − exp(−2τ_s), k_p · (1−cosθ)/2)`.

**Ambient:** Height-gradient sky light: `L_amb = k_amb · lerp(L_zenith, L_horizon, d)`.

**Aerial perspective:** Haze toward the clear-sky color `skyNoDisc` in the same direction: `T_ap = exp(−t / D_haze)`, `S' = lerp(skyNoDisc, S, T_ap)`. This is what makes horizon clouds melt into the sunset band.

**Final:** `L = L_sky · (1−α) + S' · α`, where `α = fade · (1 − T_v)` and `fade` is a distance fade to `t_max` (default 35 km).

### Lighting and day/night

Cloud light comes from `Sky::Environment`, evaluated in `evaluate()` each frame:

- **Cloud-top light color (`cloudLightColor`):** DE6's own sun model at `elev + dip`, **without** the `(1 − 0.82·coverage)(1 − 0.35·rain)` dimming that applies to ground-level `sunColor`. Horizon dip: `δ ≈ sqrt(2h/R)` (for h=2 km, δ≈1.4°). Clouds keep direct (red) light for a while after the sun sets at ground level — the classic pink-after-sunset look, for free. Moon blends in at night.
- **Cloud light direction (`cloudLightDir`):** Sun→moon blend via `smoothstep(−0.02, 0.06, elev + dip)`.
- **Ambient (`cloudSkyTop`, `cloudSkyBottom`):** `m_skyZenith` and `m_skyHorizon`, which follow day, dusk, night, and the overcast lerp.

Sunset: three things handle it with no special cases:
1. Air-mass extinction reddens the cloud-top light.
2. Aerial perspective pulls distant clouds toward the dusk horizon band `EvaluateSky` already draws.
3. The forward HG lobe brightens clouds near the low sun.

**Night:** `cloudLightColor` becomes moonlight along `moonDir`, so the same phase and taps give moonlit edges. Ambient drops to the night sky (zenith `(0.01, 0.02, 0.05)`).

**Overcast and rain:** `coverage` (from `weather.cloudCoverage`) drives the density remap. `rain` darkens `cloudLightColor`; you could also raise `tauMax` like HZD does for rain clouds.

### Animation and wind

Wind offsets integrate on the CPU in real time in `Environment::tick(dt)`: `m_cloudWindBase += windDir · windSpeedMps · dt`, with detail at 1.5× speed and rotated ~±37° so the two layers slide against each other. Phase 1 (ALU noise): no wrapping needed; fp32 step at 360 km is ~0.03 m, invisible after scaling by the noise frequency. Phase 2 (textures): wrap offsets modulo the texture period, which is seamless.

**Real cloud clock (fixed 2026-10-06):** `Environment::m_cloudClockSec` advances in real seconds, independent of `timeOfDay`. The old code froze clouds when `timeScale == 0` and jumped them when time was scrubbed.

### Shared-include macro and bindings

`SkyEval.hlsli` is shared by `Sky.hlsl`, `Water.hlsl`, and `Ssr.hlsl`. The cloud layer uses resources (`b2`, `CloudLayerConstants`) that Water and Ssr don't bind yet. To avoid breaking those shaders:

- **`SKY_CLOUDS_MODE` macro:**
  - `2` = new layer (Sky.hlsl defines this before including SkyEval)
  - `1` = legacy flat noise plane (Water/Ssr default to this)
- `CloudLayer.hlsli` is gated `#if SKY_CLOUDS_MODE == 2` and included by `Sky.hlsl` after `SkyEval.hlsli`.
- `EvaluateSky` calls `EvaluateCloudLayer(v, skyNoDisc)` when mode 2, otherwise the old `Fbm` plane.

**GPU constants (`b2`):**
- `CloudLayerGpu` struct: 10 `float4` (160 B), allocated in 256-B slots from a per-frame upload ring (double-buffered, 2 frames).
- Root parameter `kRootCloudCbv` = `3` (CBV `b2`, pixel visibility). Sky root signature now uses **4 parameters (61 of 64 DWORDs)**.
- Filled in `SkyPipeline::draw` from `env.cloudLayer` and bound every frame.

### Tuning parameters (Editor Sky panel)

The **Sky** window (Editor) has a collapsing **Cloud Layer** section with all tuning knobs:

| Section | Parameters |
|---------|------------|
| **Enable** | `enabled` (checkbox): turns the layer on/off |
| **Geometry** | `altitude` (m), `thickness` (m), `tMax` (draw distance, km) |
| **Density & Coverage** | `tauMax` (optical depth), `baseFreq` (1/m), `detailFreq` (1/m), `erosion` |
| **Lighting & Scattering** | `albedo`, `g0` (forward HG), `g1` (back HG), `backWeight`, `silverLining`, `powder`, `ambientScale` |
| **Multi-scatter** | `msA`, `msB`, `msC` (keep a ≤ b) |
| **Sun shadow** | `kappa` (shadow strength), `rMax` (max reach, m) |
| **Horizon & Wind** | `hazeDistance` (km), `windSpeedMps` (m/s), `windDir` (xy) |

All values persist in `content/scenes/<name>.json` under `"sky"` → `cloudLayerEnabled`, `cloudAltitude`, `cloudThickness`, `cloudTauMax`, `cloudWindSpeedMps`, `cloudWindDir`. Scene load/save via `SceneFile.cpp` (`applySceneAtmosphere` / `captureSceneAtmosphere`).

### Toggle between new layer and legacy CloudVolume

- **New lightweight layer (default):** Enabled by `env.cloudLayer.enabled = true` (default). Controlled in Editor **Sky** panel. Renders for every sky pixel in the sky pass.
- **Legacy CloudVolume (optional):** Placed entities with `CloudVolumeComponent`. Enabled per-entity via `desc.enabled` (Inspector) or globally via **Sandbox Dev Tools → Rendering → Cloud Volumes** checkbox (`debugState().clouds`). Renders in a separate fullscreen pass (`CloudVolumePipeline::draw`) after sky and water. **Default scene (`level.json`) has one CloudVolume entity with `enabled: false`**, so it's present but not drawn until the user enables it.
- **Both can coexist:** lightweight layer in the sky pass, then placed volumes composited after. Typical use: lightweight for distant/horizon clouds, optional hero volume for a near/inside shot.

### Cost comparison and profiling

| Path | Per pixel (estimated ALU/texture) |
|------|-----------------------------------|
| **Lightweight layer (phase 1)** | 1 `sqrt` shell hit + 15 `Noise2` (60 `Hash21`) + ~6 `exp` + 6 `pow`, **sky pixels only** in HybridDeferred |
| **Lightweight layer (phase 2)** | Same math with ~6 bilinear `R8` fetches instead of noise |
| **Legacy CloudVolume, fixed path** | ≤ 760 `GradientNoise3` (≈ 6,080 `Hash33`) per volume, **full res**, every frame, no history |

The sky PS already does a 16-step CSM fog march per pixel, so the cloud layer is the same order of work as what's there. **The lightweight layer replaces up to ~760 3D gradient-noise evaluations per pixel per volume (worst-case fixed path) with ~15 2D value-noise evaluations per sky pixel.**

**How to profile (PIX):**
1. Open PIX (DarkEngine6 has PIX markers enabled).
2. Capture a GPU frame in HybridDeferred or ForwardFirst.
3. Locate the `"Sky"` and `"Cloud Volumes"` GPU scopes.
4. Compare: **before** (legacy CloudVolume on, layer off) vs **after** (layer on, legacy off).
5. Test at noon, sunset, night, and overcast to see lighting variation.
6. ForwardFirst: sky is drawn full screen before geometry, so the cloud cost applies to every pixel. HybridDeferred: sky pixels only.

**IMPORTANT:** The lightweight layer has **not been tested on Windows/D3D12** as of this commit. The shader code compiles under SM 5.0 / FXC and the structure mirrors working DE6 patterns (upload buffers, root CBV, macro-gated includes), but **no Windows build or PIX capture has been run**. The ALU/texture cost estimates above are operation counts from reading the code, not measured timings. Travis should profile with PIX to get real ms numbers.

### What is untested on Windows

- **Everything.** This implementation was developed on Linux without access to a Windows/D3D12 build or PIX. The code:
  - Follows DE6 patterns (no exceptions, `bool` + `DE_LOG_ERROR`, root CBV like CloudVolume, double-buffered upload like CloudVolumeGpuList).
  - Compiles under FXC SM 5.0 (`compileShaderFromContent` in `SkyPipeline.cpp`).
  - Uses `memcpy` and `SetGraphicsRootConstantBufferView`, which are standard D3D12.
  - The sky is still drawn (the pass runs), so if the shader has bugs they will show (black/pink/NaN, not a silent no-op).
- **Needs Windows verification:**
  - Does the sky still render (clear, overcast, sunset, night)?
  - Do clouds appear? Do they react to coverage, time of day, sun elevation?
  - Do they animate (wind)?
  - Do Water and Ssr still compile and render reflections correctly (they default to `SKY_CLOUDS_MODE 1`)?
  - PIX timings for the `"Sky"` scope with clouds on/off.
- **If it doesn't work on Windows, the most likely issues are:**
  - HLSL compile error in `CloudLayer.hlsli` (FXC quirk).
  - Root signature mismatch (wrong `b2` register or visibility).
  - Upload buffer alignment (already 256-B aligned, should be fine).
  - Macro not defined (shader sees mode 0 instead of 2/1).

### Horizon, aliasing, and fade

- The curved shell makes clouds sink and foreshorten toward the horizon. HZD notes the same.
- `tMax ≈ 35 km` as a start (HZD draws its cloudscape within 35 km), with a 25% distance fade.
- **Aliasing near the horizon:** Phase 1 (ALU noise): drop the detail octave as `t` grows (not yet implemented). Phase 2 (textures): hardware mips handle it. Ghost of Tsushima also reduces density where UV derivatives are high; add that if moiré shows up.

---

## Placed participating-media volumes (legacy, optional)

## Draw

`CloudVolumePipeline::draw` runs after the sky and water, while the HDR target is bound and scene depth is readable (`bindHdrDepthRead`). Sandbox does this in `SandboxApp` once `hasGBuffer()` is true. The Editor 3D view does the same with default LOD settings. Transparents, particles, and the post chain (bloom, TAA, motion blur, tonemap) run later, so cloud in-scatter is part of the image those passes see.

The pass is a fullscreen triangle (`DrawInstanced(3, 1)`), depth test off, one color target. Blend is premultiplied transmittance:

- source `ONE`, dest `SRC_ALPHA`, add
- the shader returns `float4(scatter, T)` with scatter already multiplied by transmittance along the ray
- result is `scatter + destination * T`

Pixels that miss every volume, or that accumulate nothing (`T > 0.995` and no scatter), `discard`. Depth is not written. The ray is clipped to scene depth by reconstructing the world position from the depth buffer (`tScene`). Sky depth uses a far cap of **2800 m** (`kCloudFar`) instead of the camera far plane. `DebugRenderState::clouds` skips the pass. The dev-tools Rendering section toggles that flag.

Up to **16** volumes (`kMaxCloudVolumes`). `gatherCloudVolumes` packs enabled volumes with non-zero density, drops any whose bounding sphere misses the cull frustum, and uploads the list through `CloudVolumeGpuList` (128-byte records, one slot per frame).

---

## Volume

A cloud is an entity with `TransformComponent` and `CloudVolumeComponent`. The transform scale is the full width, height, and depth; half extents are `abs(scale) * 0.5`, with a 5 cm floor. Position is the center. Rotation is sent as the inverse quaternion and applied on the GPU.

`CloudVolumeDesc` (defaults in parentheses):

| Field | Role |
|-------|------|
| `shape` | Box or ellipsoid (ellipsoid) |
| `density` | Multiplier on the sampled field (0.90) |
| `coverage` | Threshold against the fractal. Higher keeps more cloud (0.58) |
| `softness` | Edge width of the shape mask and of the coverage remap (0.42) |
| `absorption`, `scattering` | Extinction and in-scatter scale (1.15, 1.00) |
| `anisotropy` | Henyey–Greenstein lobe, clamped to ±0.95 (0.45) |
| `noiseScale` | World-space base frequency (0.055, about an 18 m cell) |
| `detailScale`, `detailStrength` | Second fractal subtracted from the base (3.40, 0.38) |
| `heightFalloff` | Thins the cloud toward the top of the volume (0.55) |
| `silverLining` | Extra forward lobe, `pow(cos, 8)` (0.75) |
| `windSpeed`, `windDir` | Scrolls the noise (1.20, mostly +X) |
| `albedo` | In-scatter tint (0.90, 0.93, 1.00) |
| `enabled` | Packed and drawn only when set |

The shape mask is analytic. A box uses a rounded SDF. An ellipsoid uses radial distance over the half extents. A vertical profile then fades the bottom 32% of the local height and applies `heightFalloff` toward the top. Noise is evaluated only after the mask is non-zero.

The base field is a 4-octave gradient-noise FBM. The lattice is rotated between octaves so the axes do not tile. At full quality a 3-tap domain warp (0.35) is added before the FBM, and a 2-octave detail FBM at `detailScale` is subtracted, scaled by `detailStrength`. Coverage remaps the result: values below `1 - coverage` become empty, with a soft knee from `softness`.

This volume coverage is not the sky's weather `cloudCoverage`. The analytic sky and the placed volumes are separate.

Sandbox spawns whatever `cloud_volume` objects are in the loaded scene. `content/scenes/level.json` places one ellipsoid at `(0, 36, 0)` with scale `(140, 32, 140)`. If the scene has none, Sandbox spawns that same default. The Editor placement default is `(96, 28, 96)` centered 32 m above the ground hit.

---

## Lighting

Each sample with density above `1e-5` adds one step of single scattering and updates transmittance:

```
sigmaS = density * scattering
sigmaT = density * (absorption + scattering)
scatter += T * sigmaS * Li * dt
T      *= exp(-sigmaT * dt)
```

`Li` is sun plus ambient, times albedo. The sun term is Beer–Lambert along `sunDir` times a dual-lobe phase (forward `g`, back-scatter `-0.35 g` at 32%) plus the silver-lining lobe. Ambient uses a cheap multiple-scatter stand-in, `1 - exp(-3.5 * density)`. The march stops when `T < 0.012`.

The sun march starts from a base step `max(min(halfExtents) * 0.18, 1.2)` meters. The reference march is **5** of those steps (`kCloudLightSteps`), and it stops early when its own transmittance drops under `0.02`. LOD may change how many taps are taken; the step length is scaled so those taps still cover the same `5 * base` distance. A 2-tap tail is a coarser estimate of the same sun ray, not a shorter one. The sun samples always use the 2-octave density with warp and detail off.

The per-pixel start offset is interleaved-gradient noise (`Ign` of the pixel and `time`). On a uniform segment the first sample is `t0 + jitter * dt`, and each later sample advances by `dt`. On the fade, each slab jitters by its own `dt`.

---

## Distance LOD

The expensive sample is the full-quality density: 3 warp noises + 4 base octaves + 2 detail octaves, then a 5-tap sun march of 2 octaves. The step count used to be fixed at 40 (56 when the ray starts inside the volume), so a larger box made `dt` longer, the early-outs stopped firing, and every pixel the box covered paid the full sample. LOD keeps that fixed march only while it is still the right one, and splits longer rays into three bands. Bands are distances **along the ray**, not a switch on the volume as a whole. One box can be full quality overhead and coarse at the horizon.

Settings live on `CloudLodSettings`, copied into `CloudVolumeFrame::lod` and uploaded as root constants. Defaults:

| Knob | Default | Meaning |
|------|---------|---------|
| Enabled | on | Off forces the fixed march for every ray |
| Detail distance | 200 m | Full quality ends here |
| Fade distance | 48 m | Band where detail and octave weight blend out |
| Near step | 4 m | Target step inside the shell |
| Far step | 18 m | Target step on the tail (about one base noise cell) |
| Max near steps | 32 | Budget for the shell and for the fade |
| Max far steps | 16 | Budget for the tail |
| Near light steps | 5 | Sun taps on the shell and through the fade |
| Far light steps | 2 | Sun taps on the tail |

`sanitizeCloudLod` runs on upload and again after the dev-tools sliders. Detail distance is at least 1 m, fade at least 0, near step in `[0.5, 64]`, far step at least the near step and at most 256, step budgets in `[1, 64]`, light steps in `[1, 8]`.

### Which march a ray takes

1. **Fixed march.** LOD is off, or the ray exits the volume at or before the detail distance (`t1 <= detailDistance`). Step count is 40, or 56 when `t0 <= 1e-3` (camera inside, or the shell starts on the camera). `dt = (t1 - t0) / steps`. Every sample is 4-octave density with warp and detail, and 5 sun taps. A default 140 m volume viewed from inside or from nearby stays on this path, because the chord ends before 200 m.

2. **Near shell.** `t0` to `min(t1, detailDistance)`. Step count is `ceil(span / nearStep)`, clamped to max near steps. Uniform `dt = span / count`. Samples are full quality (4 octaves, warp, detail, near light steps). Hitting the step budget coarsens the shell instead of adding samples: 200 m at the default budget is 6.25 m, not 4 m.

3. **Fade.** From the detail distance to `detailDistance + fadeDistance`, clipped to the ray. Step count uses the midpoint of the near and far steps, under the same near-step budget. Slab lengths grow from the near step to the far step (smoothstep across the band, normalized so the slabs sum to the band). Octave weight eases from 4 to 2. `detailStrength` and the domain warp ease from full to off. Sun taps stay at the near count so the shadow does not change in the middle of the blend. A fade of 0 skips this band.

4. **Tail.** From the end of the fade to `t1`. Step count is `ceil(span / farStep)`, clamped to max far steps. Uniform `dt`. Density is 2 octaves, no warp, no detail, far light steps. If the remaining distance is longer than `farStep * maxFarSteps`, `dt` grows past the far-step target so the ray still reaches the volume exit. That is the case a multi-kilometer box hits: the tail stays at 16 samples and gets coarser, instead of one sample every 18 m for the whole chord.

A ray that enters already past the shell skips the earlier bands. A ray that enters inside the fade starts mid-blend. Transmittance still stops the march at `T < 0.012`, including between bands.

The far step of ~18 m is chosen to hold the base cloud shapes (the `noiseScale` 0.055 cell). Detail cells are about 5 m and are what the fade removes. Past a few hundred meters those small billows are a real softening, which is the trade. The large masses stay as long as the tail step remains near one base cell. Stretching the tail on a huge box gives that up for a hard cap on cost.

---

## Root constants

`CloudVolumePassConstants` is 40 floats, uploaded with `SetGraphicsRoot32BitConstants`. The HLSL `cbuffer` matches this order. `lodEnabled` sits in the slot that used to be padding, next to `ambientColor`, so the LOD block starts on a `float4` boundary.

| Offset (floats) | Fields |
|-----------------|--------|
| 0 | `invViewProj` (16) |
| 16 | `cameraPos`, `time` |
| 20 | `sunDir`, `volumeCount` |
| 24 | `sunColor`, `nearZ` |
| 28 | `ambientColor`, `lodEnabled` |
| 32 | `lodDetailDist`, `lodFadeDist`, `lodNearStep`, `lodFarStep` |
| 36 | `lodMaxNear`, `lodMaxFar`, `lodNearLight`, `lodFarLight` |

Volume records are a raw SRV (`t1`), not inside this buffer. Depth is `t0`.

---

## Dev tools

Sandbox Dev Tools (M) → **Cloud LOD**, shown when the hybrid path has a G-buffer. The sliders edit `SandboxApp::m_cloudLod`, which is copied onto the frame each draw. The Editor has no panel; `CloudVolumeFrame{}` uses the defaults above, so Editor clouds take the same LOD.

The header is the place to compare. Turn Enabled off to force the fixed 40/56 march on every ray, including a large box. That fixed march always takes 5 sun taps; the near and far light-step sliders apply only once a ray extends past the detail distance. Raise max far steps, or lower the far step, when a stretched tail is softer than you want. Lower the detail distance to push a nearby volume onto the shell / fade / tail path.

---

## Limits

- The pass is full resolution. LOD changes samples along the ray. It does not render distant pixels at a lower resolution.
- There is no history buffer. Each frame is independent. TAA later in the post chain is the only temporal filter, and it sees the composited HDR, not a cloud-only history.
- The fixed march and the LOD shell do not share a sample grid. A volume that grows across the detail distance changes which algorithm runs, on the frame the far exit crosses that distance.
- Sun taps above 8 are clamped. The sun distance stays `5 * baseStep` regardless.
- Overlapping volumes march one after another in gather order. They do not share samples. Sixteen full-quality volumes on the same pixel still multiply.
- Empty space inside the shape still evaluates the base fractal. The shape mask and the LOD octave drop are the skips. A clear gap on the tail is cheap relative to a lit full-quality sample, and it is not free.
