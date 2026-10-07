# Distant sky cloud layer

| Field | Value |
|-------|-------|
| **Status** | Proposed. The shell is in tree and is the sky the editor and sandbox draw. PIX of the editor Sky scope is still required before this becomes Implemented. |
| **Date** | 2026-10-06 |
| **Area** | `Sky/Environment.*`, `Sky/CloudLayer.*`, `Render/SkyPipeline.*`, `Render/SsrPipeline.*`, `Render/WaterPipeline.*`, `content/shaders/SkyEval.hlsli`, `content/shaders/CloudLayer.hlsli`, `Scene/SceneFile.*`, Editor Sky window |
| **Audience** | Engine, Sandbox, and Editor owners who already know HybridDeferred |
| **See also** | `Render/DESIGN-cloud-volumes.md` is the placed-volume march. It stays. This file does not change that pass. |
| **Replaces** | The rejected sky-shell attempt (root `b2`, `SKY_CLOUDS_MODE`, the Sky-window coefficient sheet). That attempt is gone. This file is the deck the tree builds. |

---

## Overview

Everyday clouds are a lit shell inside `EvaluateSky`, the function the sky pixel shader, water, and SSR already share. One sphere hit, a 2D noise field, three sun taps, then a composite onto the analytic sky. Placed volumes stay a separate fullscreen march, off unless View → Cloud Volumes is on and that volume's `enabled` is set.

The shell is below-deck only. A camera at or above the deck, or a ray that points down, gets the analytic sky and no shell. Walking through a cloud, or looking down onto the deck, is a placed volume, not this pass.

Shading coefficients that an artist does not see are literals in `CloudLayer.hlsli`. The Sky window edits coverage, altitude, amount, wind speed, and wind direction. Those are the fields the scene file round-trips. Scene `version` stays 2.

No measured GPU time exists for this layer. Acceptance is a Windows build of the three shaders plus a PIX capture of the editor Sky scope. This document does not quote an ALU budget as a timing.

---

## What ships today

The deck below is in the tree. This section is the sky it replaced: `EvaluateSky` drew the analytic sky (gradient, Rayleigh/Mie glow, dusk band, overcast, sun disc) and then a flat 5-octave FBM plane scrolled by `weather.windSpeed` and `cloudTime`. That plane is gone. `Sky.hlsl` says that function must stay consistent with `Sky::Environment::evaluateSky`. The CPU function is the analytic sky only. It has no FBM plane. It feeds `m_skyZenith`, `m_skyHorizon`, and ambient. Water and SSR call the same GPU function, so reflections show the deck. The CPU ambient does not.

Call sites:

| Caller | How the sky constants arrive | Exposure passed into `EvaluateSky` |
|--------|------------------------------|-------------------------------------|
| Sky, HybridDeferred (Editor and Sandbox) | `SkyFrameConstants`, 56 floats, root 32-bit constants at `b0` | `1`. Tonemap owns exposure. |
| Sky, ForwardFirst | same root constants | `Environment::exposure()` |
| Water | `SkyEvalParams` flattened at the end of `WaterFrameConstants` (`b0`) | `1` from `fillSkyEval` |
| SSR | `SkyEvalParams` as `b1`, 256-byte slot, `skyEvalCbvByteOffset(frameIndex)` | `1` from `fillSkyEvalParams` |

`SkyEvalParams` is 24 floats. The SSR upload slot is 256 bytes (64 floats). Water places `skyEval` at float offset 136 of a CBV whose aligned size is 768 bytes, so the tail can grow to 56 floats before that 768-byte alignment moves.

The sky root signature, before the rejected fourth parameter, is 59 of 64 DWORDs: 56 root constants, a shadow CBV (2), a shadow SRV table (1). Five DWORDs are free. A further CBV costs 2 and leaves 3. That is the wrong place to hang a sky-only cloud buffer. Water's root signature is six parameters and already contains the sky tail. SSR's root signature is three parameters and already binds `SkyEvalParams`.

After lighting, the sky pixel shader may run `FogIntegrateNoHeight` along a **280 m** stand-in (`Sky.hlsl`). The Editor calls `SkyPipeline::draw` with `fogScale` 0, so that integrate does not run in the editor. Sandbox passes `fogScale` 1 when `lightingActive()` is true and 0 when it is false. HDR scene targets compile the sky with `ENCODE_SRGB` 0, so `encodeSceneRgb` is a no-op. The forward LDR path can encode. Deferred sky uses `SkyPass::DeferredLast`: depth test on, depth write off, `skyDepthFunc()`, so the pixel shader runs on sky pixels. ForwardFirst draws the sky full screen with depth off, before geometry.

`Renderer::kFrameCount` is 2. `moveToNextFrame` signals the queue, advances `frameIndex` to the swap-chain back buffer, and waits on that slot's fence before the CPU records into it. `CloudVolumeGpuList::upload` and `SsrPipeline::skyEvalCbvByteOffset` both key off `frameIndex % 2`. A counter incremented inside `draw` is not that index.

Weather coverage is `WeatherState::cloudCoverage` (Clear 0.05, Partly 0.35, Overcast 0.82, Storm 0.96), saved as `sky.cloudCoverage`. `weather.windSpeed` is a unitless 0.04 (Storm writes 0.10) and only scrolls the flat plane. `weather.windDir` defaults to `(1, 0.2)` and is saved as `sky.windDir`. The Sky window has time of day and the four weather buttons. It has no coverage slider. View → Cloud Volumes is a separate menu item on `DebugRenderState::clouds`.

---

## Goals

- One GPU cloud evaluation for the sky pixel shader, water, and SSR. Reflections show the same shell as the sky.
- CPU `evaluateSky` stays the analytic sky. Zenith, horizon, ambient, and the shell's ambient color all read that result, so the shell does not tint ambient with its own albedo.
- The sky pipeline still creates when the shell shader fails to compile. The sun disc remains.
- Editor controls are coverage, altitude in meters, amount, wind speed in meters per second, and wind direction. Every one of those round-trips through the `sky` object. A missing key uses the default in this document.
- Wind keeps moving while time of day is scrubbed or `timeScale` is 0.
- The shell's horizon fade is its own term. It is not `Fog.hlsli` and it is not the 280 m sky stand-in.
- Placed volumes stay as `DESIGN-cloud-volumes.md` describes them, including default off.

## Non-goals

- Shadowing the terrain or the sun cascades from the shell.
- A camera inside the layer, or above it looking down. No clamped stand-in that pretends the camera is still below the deck.
- Tileable Perlin-Worley textures, a history buffer, or a lower-resolution cloud target.
- A Sky-window full of Henyey–Greenstein, multi-scatter, powder, or kappa sliders.
- A second coverage, a second wind direction, or a `cloudLayerEnabled` flag. Coverage 0 is how the deck goes away. Clear is 0.05, which is already a thin deck.
- Changing scene `version`.
- A quoted millisecond cost. PIX comes after it compiles on Windows.

---

## Key decisions

1. **The flat FBM plane is removed in the same change that adds the shell.** There is no `SKY_CLOUDS_MODE`. Water and SSR cannot keep the old plane while the sky shows the shell. The CPU analytic sky is unchanged on purpose: it never had the plane.

2. **Cloud constants ride the buffers those three shaders already fill.** They are appended to `SkyEvalParams` and to `SkyFrameConstants`. The sky stops using 56 root 32-bit constants and binds `b0` as a CBV. The sky root signature goes back to three parameters (sky CBV, shadow CBV, shadow SRV), 5 of 64 DWORDs. Register `b2` is not added. Water and SSR do not gain a root parameter.

3. **Upload uses `renderer.frameIndex()`.** `SkyPipeline::upload` is not `const` and writes slot `frameIndex % 2`. `draw` stays `const` and only binds that slot. It does not advance a counter. One frame records one slot even if the sky were drawn twice. The CPU writes the slot only after `moveToNextFrame` has waited on it, which is already true for every other per-frame upload during the frame.

4. **Paper coefficients are literals.** The scene file and the editor cannot drift from the shader. The look is the table in "Literals" below.

5. **Compile failure is isolated to the shell.** `SkyPipeline::create` compiles `PSMain` twice, `CLOUD_LAYER=0` and `CLOUD_LAYER=1`, with the same `ENCODE_SRGB` macros the pass already uses. If the `1` compile fails, create logs the FXC message and keeps the `0` PSO. create returns false only when the `0` compile fails. The `0` shader is the analytic sky with the FBM plane already gone, not a third cloud model. Water and SSR compile `CLOUD_LAYER=1` only, as they do for any other shader error today.

6. **Below the deck, or no shell.** `cloudShellHit` on the CPU and `CloudShellHit` in HLSL are the same formula. Tests lock the CPU function. The shader comments that it must match.

---

## Removed shell

The rejected sky shell is gone: root `b2`, `SKY_CLOUDS_MODE`, the coefficient `CloudLayerDesc`, scene keys `cloudLayerEnabled` / `cloudThickness` / `cloudTauMax` / `cloudWindDir`, and the Sky-window coefficient sheet. The deck in this document is what the tree builds. Do not delete it.

`SkyPipeline::create` compiles `CLOUD_LAYER` 0 and 1. When the 1 PSO exists it is the one `draw` binds, so the deck is the default sky under the current weather coverage. There is no `cloudLayerEnabled` flag. Placed volumes stay off unless View → Cloud Volumes is on and that volume's `enabled` is set. The flat FBM plane is gone from sky, water, and SSR.

This document stays **Proposed** until a PIX capture of the editor HybridDeferred Sky scope, once with Partly coverage and once with coverage 0, shows the Sky scope and the sun in both and records the two timings. A Windows shader compile is not that capture.

---

## Shell

Planet radius `R` is the literal `6.36e6` meters. The deck is the sphere of radius `R + altitude`. `altitude` is meters above world Y = 0 and defaults to **2000**. Thickness is not a second intersection. It is the literal vertical extent of the sun march, **800 m**.

`cloudShellHit(cameraY, viewY, altitude, R, out t)`:

- Returns false when `viewY <= 0` or `cameraY >= altitude - 1`.
- Otherwise `y0 = cameraY`, `k = (altitude - y0) * (2R + altitude + y0)`, `b = (R + y0) * viewY`, `s = sqrt(b*b + k)`, `t = (b >= 0) ? k / (s + b) : (s - b)`.
- Does not clamp `cameraY` down under the deck.

`t` is meters along the view ray. The shading point is `camera.xz + view.xz * t`. Hit cosine is `(b + t) / (R + altitude)`, floored at `0.05` when used as a divisor.

The HLSL function takes the same inputs. A camera flying the editor above 2000 m sees clear sky in that direction. The Sky window says so in one line of disabled text.

### Density

2D value noise, the existing `Hash21` / `Noise2`. Three octaves, amplitude halved each octave, domain multiplied by `float2x2(1.6, 1.2, -1.2, 1.6)`. Base frequency literal `1/4000` per meter. Detail frequency literal `1/900`.

Coverage `c` is the existing `coverage` register (weather), not a new one. Base sample `n` at the wind-shifted point:

```
d = saturate((n - (1 - c)) / max(c, 0.05))
d = saturate(d - (1 - d) * 0.35 * detail)
```

`0.35` is the erosion literal. Detail is sampled once at the hit, with the detail wind offset. The three sun taps sample the base octaves only.

If `c < 0.02` or `d <= 1e-3`, the function returns no cloud (`rgb = 0`, `a = 1`).

### Lighting

Evaluated on the CPU in `Environment::evaluate`, stored for the upload, in the same linear space as `sunColor` before the shader's exposure multiply.

```
dip   = sqrt(2 * altitude / R)
elevC = sunElevation + dip
```

Direct color uses the ground sun's noon-to-dusk mix and extinction, evaluated at `elevC`, then multiplied by `(1 - 0.35 * rain)`. It does **not** multiply by `(1 - 0.82 * coverage)`. The moon term is the existing moon color evaluated with `elevC` in place of the sun elevation. The blend to the moon is `smoothstep(-0.02, 0.06, elevC)`.

Ambient is `evaluateSky` at `(0, 1, 0)` and at a horizon direction, the values already stored as zenith and horizon. Those calls stay on the CPU analytic sky.

Optical depth of the view column:

```
tau  = 12 * amount * d / max(muHit, 0.05)
T    = exp(-tau)
```

`12` is the literal `tauMax`. `amount` defaults to **1** and is the editor's Amount slider.

Sun optical depth is three taps along the sun direction projected onto the XZ plane. Reach is `min(0.5 * 800 / max(lightDir.y, 0.08), 3000)` meters. `kappa` literal `0.5` scales the accumulated density into `tauS`.

Phase is dual-lobe Henyey–Greenstein relative to isotropic (`* 4π`), three Wrenninge octaves:

| Literal | Value |
|---------|-------|
| `g0` | 0.45 |
| `g1` | -0.16 |
| back weight | 0.32 |
| silver lining | 0.75 on `pow(saturate(cosθ), 8)` |
| multi-scatter `a`, `b`, `c` | 0.5, 0.5, 0.5 |
| powder | 0.6, and only while `cosθ < 0` |
| albedo | 0.9 |

`a <= b` stays true at these literals. Energy of the octaves is `Lsun = E * Σ a^n * exp(-b^n * tauS) * phase(c^n * g)`.

```
S = albedo * (E * sun * powder + ambient)
ambient = lerp(zenith, horizon, d)
```

### Composite inside `EvaluateSky`

Order, matching the analytic sky that is already there:

1. Gradient, glow, dusk band, overcast → `skyNoDisc`.
2. Add the sun disc into `base`.
3. Shell: aerial perspective `S = lerp(skyNoDisc, S, exp(-t / 25000))`, then fade `alpha = fade * (1 - T)` with `fade = saturate((35000 - t) / (0.25 * 35000))`. Both distances are meters. `35000` is the draw distance. `25000` is the haze distance. The result is `base = base * (1 - alpha) + S * alpha`.
4. `return max(base, 0) * exposure`.

Step 3's haze lerps toward `skyNoDisc`. It does not call `FogIntegrate`. The 280 m stand-in in `PSMain` still runs only when `fogScale > 0`, after step 4, on the whole sky color including the shell. Editor `fogScale` is 0. Sandbox `fogScale` follows `lightingActive()`. Do not move that stand-in, and do not run it out to `t`.

`encodeSceneRgb` stays where it is. On the HDR target it is a no-op. Cloud radiance is linear scene color, the same as the rest of `EvaluateSky`.

`CLOUD_LAYER=0` skips step 3. The FBM block is deleted in both variants.

### Include order

`CloudLayer.hlsli` defines `CloudShellHit` and `EvaluateCloudLayer` and does not declare a cbuffer. `SkyEval.hlsli` includes it after `Noise2` and before `EvaluateSky`. The caller's cbuffer has already declared `cameraPos` and the cloud fields. FXC `ps_5_0` sees the definition before the call. `Sky.hlsl`, `Water.hlsl`, and `Ssr.hlsl` include `SkyEval.hlsli` after their cbuffers, as they do now.

---

## Constants

`SkyEvalParams` keeps its current 24 floats in order, including `cloudTime` and `_pad`. Twenty floats are appended:

| Field | Contents |
|-------|----------|
| `clLightColor` | rgb direct radiance, `a` = amount |
| `clLightDir` | xyz direction, `w` = altitude in meters |
| `clSkyTop` | rgb analytic zenith, `a` unused 0 |
| `clSkyBottom` | rgb analytic horizon, `a` unused 0 |
| `clWind` | xy base offset in meters, zw detail offset in meters |

`sizeof(SkyEvalParams)` becomes 44 floats, which is under the SSR slot of 64 floats and under the water tail budget of 56 floats. `kSkyEvalCbBytes` stays 256. The water CBV aligned size stays 768. `static_assert` both.

`SkyFrameConstants` keeps its current 56 floats in order and appends the same 20, same names, so `EvaluateSky` compiles against either cbuffer. Size becomes 76 floats. The sky upload allocates 512 bytes per slot (256-byte CBV alignment) times 2 frames.

One CPU function, `writeCloudLayer`, fills those 20 floats from `Environment`. `SkyPipeline::upload`, water's sky fill, and `fillSkyEvalParams` all call it. `exposure` in `SkyEvalParams` stays 1. The sky CBV's `exposure` field stays what `draw` already writes: 1 on HybridDeferred, `Environment::exposure()` on ForwardFirst.

`weather.windSpeed` and `cloudTime` remain in the 24-float prefix so the existing layout does not shuffle. The shell does not read them. After the FBM plane is gone, nothing in `EvaluateSky` reads them.

### Root signature after the change

| Parameter | Binding | DWORDs |
|-----------|---------|--------|
| 0 | CBV `b0` `SkyFrameConstants` | 2 |
| 1 | CBV `b1` shadow | 2 |
| 2 | SRV table `t0` shadow | 1 |

Total 5. The comparison sampler stays `s1`. Pixel visibility on the shadow bindings stays as it is today.

---

## Wind

`Environment::tick` adds `dt` seconds to a clock that is not `timeOfDay` and is not multiplied by `timeScale`. The base offset adds `normalize(windDir) * windSpeedMps * dt`. The detail offset uses a fixed 90°-class turn of that direction, `(dir.y * 0.6 - dir.x * 0.8, dir.x * 0.6 + dir.y * 0.8)`, at `1.5 * windSpeedMps`. A zero direction uses `(1, 0)`.

Offsets are runtime state. They are not scene fields. Load starts them at 0. Scrubbing time of day does not add an extra step. `timeScale == 0` still animates the deck.

`windSpeedMps` defaults to **8**. The weather buttons set it together with coverage: Clear 3, Partly 8, Overcast 10, Storm 16. They still set `weather.windSpeed` to the values they set today, which the shell ignores.

---

## Editor and scene file

The Sky window gains a **Clouds** section:

| Control | Binds to | Range | Unit on the label and in the value |
|---------|----------|-------|--------------------------------------|
| Coverage | `weather.cloudCoverage` | 0 .. 1 | fraction |
| Altitude | `cloudLayer.altitude` | 500 .. 8000 | meters |
| Amount | `cloudLayer.amount` | 0 .. 2 | scalar on the literal tau |
| Wind speed | `cloudLayer.windSpeedMps` | 0 .. 20 | meters per second |
| Wind dir | `weather.windDir` | drag, xy | direction, not normalized in the widget |

The four weather buttons write coverage and wind speed as above, so the sliders move when a button is pressed. Altitude and amount call `evaluate()` because the horizon dip and the direct color depend on them. Wind speed and wind direction do not need `evaluate()` for the color, and `tick` picks them up on the next frame.

Disabled text under the section: "Sky deck, drawn with the sky. A Cloud Volume is a separate object. It draws only when that volume is enabled and View → Cloud Volumes is on."

The section does not say the deck replaces volumes. It does not expose thickness, tau, frequencies, `g0`, `g1`, multi-scatter, powder, kappa, haze, or draw distance.

`SceneFile` `sky` object:

| Key | When absent |
|-----|-------------|
| `cloudCoverage` | existing weather-preset default |
| `windDir` | existing `(1, 0.2)` |
| `cloudAltitude` | 2000 |
| `cloudAmount` | 1 |
| `cloudWindSpeedMps` | 8 |

`capture` writes those five whenever it writes `sky`. It does not write `cloudLayerEnabled`, `cloudThickness`, `cloudTauMax`, or `cloudWindDir`. `apply` uses the defaults above when a key is missing. `level.json` needs no new keys for the default look.

`CloudLayerDesc` is three floats: `altitude`, `amount`, `windSpeedMps`. Sanitize on evaluate and on load: altitude in `[500, 8000]`, amount in `[0, 2]`, wind speed in `[0, 20]`.

---

## Frame order

Unchanged, with one upload added next to the other per-frame uploads:

1. `Environment::tick` / `evaluate`.
2. `SkyPipeline::upload(renderer.frameIndex(), env)` writes the sky CBV slot. Water and SSR fill their own copies in the uploads they already do.
3. HybridDeferred: G-buffer, lighting, then sky (`exposure` 1, editor `fogScale` 0, sandbox `fogScale` from lighting), then water (which calls `EvaluateSky` for reflections), then cloud volumes if the debug flag is on.
4. ForwardFirst: sky first (`exposure` from the environment, `fogScale` from lighting), then geometry. Water and volumes keep their current order.
5. Tonemap applies exposure on the hybrid path. The sky shader has already multiplied by the exposure register, which is 1 on that path.

The shell does not write depth and does not change the sky PSO blend. Deferred sky pixels are still the only hybrid pixels that run it, because the depth test is unchanged.

---

## Acceptance

Build `Sky.hlsl` `PSMain` for `CLOUD_LAYER` 0 and 1, `Water.hlsl` `PSMain`, and `Ssr.hlsl` `PSTrace`, all `ps_5_0`, the same way `ShaderCompile` already builds them.

CPU tests:

- `cloudShellHit` hits for a camera at Y = 10 looking up at altitude 2000, misses for `viewY <= 0`, and misses for camera Y = 2000 without moving the camera down.
- `timeScale == 0` still changes the wind offset after a tick. Setting `timeOfDay` and calling `evaluate` without `tick` does not.
- Scene round-trip writes and reads altitude, amount, and wind speed in meters per second. A JSON object with only the old `sky` keys loads the defaults in the table above and does not require `cloudLayerEnabled`.
- `sizeof(SkyEvalParams) == 44 * sizeof(float)` and the first 24 floats stay the current `SkyEvalParams` prefix. Water's aligned CBV size stays 768. Sky root parameter count is 3.

Editor, after a Windows run: Coverage, Altitude (m), Amount, Wind speed (m/s), and Wind dir are on the Sky window. View → Cloud Volumes still toggles the placed march. The default `level.json` volume stays `enabled: false` and does not appear until both that flag and the view item are on.

PIX, editor, HybridDeferred, one capture with Partly coverage and one with coverage 0: the Sky scope is present in both, and the sun is present in both. Record the two timings in the capture notes. This document stays **Proposed** until those two compiles and that capture exist. It does not become Implemented on a Linux compile or on an operation count.

---

## Rejected

- **Sky-only `b2` and `SKY_CLOUDS_MODE`.** Reflections and the sky diverged, and the sky root signature sat at 61 of 64 DWORDs. The shared tail plus a `b0` CBV is the fix.
- **`const draw` that bumps a two-slot counter.** That does not compile, and it is not `frameIndex`.
- **Calling `EvaluateCloudLayer` above its definition.** FXC `ps_5_0` requires the definition first. The include order above is the fix.
- **Clamping the camera to `altitude - 1`.** That paints a deck under a camera that has flown above it. A miss is the v1 result.
- **Labeling meter values as kilometers.** Altitude, haze, and draw distance are meters in the math. The editor shows meters. Haze and draw distance are literals, so they have no slider to mislabel.
- **Persisting twenty coefficients, or defaulting a new enabled flag to true.** Missing JSON must not invent a second cloud system. The shell replaces the FBM plane. Volumes keep their own `enabled`.
- **Treating the 280 m fog stand-in, or `encodeSceneRgb`, as the shell's composite.** Hybrid HDR does neither of those the way a forward LDR sky would.
