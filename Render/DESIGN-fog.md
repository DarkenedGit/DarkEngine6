# Fog

| Field | Value |
|-------|-------|
| **Status** | Implemented |
| **Date** | 2026-09-29 |
| **Area** | `content/shaders/Fog.hlsli`, `Render/Fog.*`, `Sky/Environment.*`, deferred lighting, sky, water, forward terrain, Sandbox dev tools |
| **Audience** | Engine and Sandbox owners who already know this tree |

Lit fog on the view ray. One integrator combines a uniform distance haze, an exponential height layer, and a valley slab around the water line. The pixel is `lit * transmittance + inScatter`. Sandbox's hybrid path applies that once, after PBR, from the reconstructed world position. It is not a separate post pass.

---

## Where it runs

`Fog.hlsli` is included by the passes that shade a surface or the sky. `makeFogGpu` / `applyFogToLighting` pack `Sky::Environment` into those constant buffers. `fillFogHeightMap` adds the coarse height-field origin, cell size, and world size so the valley term can read terrain.

| Pass | Shader | What it fogs |
|------|--------|----------------|
| Deferred lighting | `DeferredLighting.hlsl` | G-buffer opaques, including terrain on the hybrid path. Full integrate, coarse height map, per-step cascade shadow on sun in-scatter. |
| Local lights | `LocalLightVolume.hlsl` | Punctual light only. Transmittance from the same integrate; sun/ambient in-scatter from that call is discarded. A second 8-step march adds scatter from that one light. |
| Sky | `Sky.hlsl` | Atmosphere, over a stand-in ray of **280 m**. No height map. |
| Water | `Water.hlsl` | Camera to the water surface. Full integrate, height map, per-step sun shadow. |
| Forward terrain | `Terrain.hlsl` | The forward (`-forward`) terrain draw only. Distance and height, valley density forced to 0. |

Hybrid deferred does not also run `Terrain.hlsl`. Terrain is drawn into the G-buffer (`TerrainGBuffer.hlsl` has no fog) and fogged with everything else in the deferred pass. The forward terrain shader is the other path, not a second layer.

These do not sample fog:

- Forward meshes and skinned meshes.
- Cloud volumes. A cloud has its own extinction in `CloudVolume.hlsl`.
- Particles and the transparent forward draws.
- The Editor. `EditorRender3D` sets `fogDensity` to 0 and leaves the other fog fields at the zeroed `LightingConstants`. The sky is drawn with `fogScale` 0. Editor water is not given an `Environment`, so `makeFogGpu` never runs for it.

Lighting off (`lighting < 0.5`, or `lightingActive()` false) makes `makeFogGpu` write zero densities and a black fog color. The sky's `fogScale` is 0 in that case, which multiplies all three densities by zero before the sky integrate.

IBL debug views in the deferred shader return before `FogIntegrate`.

---

## The three densities

All three are extinction coefficients in 1/meters. Transmittance along a segment is `exp(-opticalDepth)`. In-scatter uses `density * Li * dt`, not `1 - exp(-density * dt)`. The closed form paints a sample as an opaque ball.

### Distance

Uniform haze. Optical depth is `fogDensity * distance`, independent of height and direction.

Auto mode (`Environment::evaluate`, `fogAuto` true):

```text
(0.007 + 0.016 * cover + 0.024 * rain) * (1.15 - 0.4 * daylight) * fogDistanceScale
```

`cover` and `rain` are the weather knobs in `[0, 1]`. `daylight` is `SmoothStep(-0.10, 0.18, sunElevation)`, so night is thicker than noon. At scale 1, clear noon is about **0.005 / m**: transmittance is about one half near **130 m** and a few percent by several hundred meters. Storm and night raise it. The dev-tools readout calls this **dist**.

### Height

Exponential slab. Density at height `y` is

```text
heightFogDensity * exp(-falloff * (y - heightFogHeight))
```

clamped to `3 * heightFogDensity`. Default falloff is **0.06** (about 17 m to 1/e). `makeFogGpu` sets `heightFogHeight` to the water level passed by the host. The forward terrain path does not: it writes the height-map origin Y into `heightFogHeight`.

The optical depth from the camera along a ray has a closed form. `exponentialHeightOpticalDepth` on the CPU and `FogHeightOpticalDepth` on the GPU are that integral. A horizontal ray (`dirY ≈ 0`) collapses to `density(cameraY) * distance`. The clamp is applied to the density at the camera, and that clamped value is the base of the integral. It is not re-applied at every sample of the closed form.

Auto mode treats this as a twilight layer. The gate is `SmoothStep(0.34, 0.05, abs(sunElevation))`: full when the sun is within about 3° of the horizon, gone by about 19°. Noon and deep night stay near zero. The same gate is `heightFogAmount`.

```text
twilight * (0.032 + 0.018 * cover + 0.014 * rain) * heightFogScale
```

### Valley

A Gaussian band around the water line, and only where the terrain is low enough to sit in it.

```text
dy   = (y - waterLevel) / volumetricHeight
band = exp(-dy * dy)
wet  = saturate((waterLevel + volumetricHeight - terrainY) / volumetricHeight)
density = volumetricFogDensity * band * wet
```

Default slab height is **14 m**. On the water line the band is 1. One slab above or below it is `exp(-1) ≈ 0.37`. `wet` is 1 at the water line and 0 when the terrain is one slab above the water. Terrain below the water stays fully wet.

`terrainY` comes from the coarse height texture. UV is `(xz - origin) / worldSize`. A texel outside `[0, 1]` returns `1e6`, so `wet` is 0 (no valley fog off the map). `heightCellSize <= 0` skips the fetch and returns `waterLevel - volumetricHeight`, which forces `wet` to 1. That is also what `FogIntegrateNoHeight` uses for every sample: the sky, and any caller without a height map, treats the ground as flooded. The sky therefore gets an unmasked valley slab. `fillFogHeightMap` on a null or invalid map sets `heightCellSize` to 0 and leaves the bound dummy 1×1 unread.

Auto mode:

```text
(0.020 + 0.018 * cover + 0.024 * rain) * (0.88 + 0.12 * (1 - daylight)) * volumetricFogScale
```

Clear noon stays above 0.005. Storms are thicker. The dev-tools readout calls this **valley**.

---

## Integrate

`FogIntegrate(camera, worldPos, params, heightMap, sampler, shadow)` is the full ray from the camera to that world position. Distance under 1 mm returns transmittance 1 and no scatter.

1. Distance and height optical depths are analytic over the whole segment. `T = exp(-(tauDistance + tauHeight))`.
2. Valley extinction is marched. Each step samples the height map (or the flooded stand-in) and does `Tv *= exp(-valleyDensity * dt)`.
3. Sun in-scatter is the same march. Sample density is the sum of all three terms. Transmittance to the sample is the analytic distance/height term to that distance, times `Tv` so far. The sun color is one Henyey–Greenstein evaluation (`g = 0.32`) for the view ray, shared by every step: `saturate(lightColor) * fogAlbedo * (0.18 + 0.50 * phase) * shadow`.
4. Ambient in-scatter is `fogAlbedo * (0.70 + 1.7 * saturate(ambient)) * (1 - T)` after valley has been folded into `T`. It is not shadowed and not marched.

`FogFinish` saturates transmittance and caps in-scatter at **1.6** per channel.

`ApplyLitFog` is `lit * transmittance + inScatter`.

### Shadowed sun march

Passes that define `FOG_SAMPLE_CSM 1` before including `Fog.hlsli` (deferred lighting, sky, water, forward terrain) take **16** steps (`FOG_MARCH_STEPS`). Each step calls `ComputeShadowVolumetric`: one cascade tap, no 3×3 PCF, no normal offset. Out of cascade or shadow strength 0 stays lit. The `shadow` argument to `FogIntegrate` is unused on this path. The step position is jittered with a hash of `worldPos + camera` so the bands do not align to the pixel grid.

Without `FOG_SAMPLE_CSM` (local lights), distance and height stay analytic, the valley march is **12** steps at the slab centers, and the single `shadow` value lights the whole sun term. Local lights pass `1`, so that sun term is unshadowed. They then ignore `inScatter` and keep only transmittance.

### Sky stand-in

The sky has no surface. It integrates from the camera to `camera + viewDir * 280`. `fogScale` multiplies all three densities (Sandbox passes 1 when lighting is on). There is no height map, so the valley term is the full Gaussian. A 280 m clear-noon distance depth is order 1, so the sky is hazed, not fogged to a solid color. The horizon of a real view ray is much longer than 280 m; the stand-in is a fixed budget, not an infinite atmosphere.

### Fog color

Auto mode sets `fogColor` to `skyHorizon * (0.75 + 0.25 * cover)` before the densities. `FogAlbedo` then pulls that halfway toward a gray of the same luminance, tinted `(0.90, 0.94, 1.00)`. The sun term is `lightColor * albedo`, not `fogColor * lightColor`. Multiplying those two was what clipped the green channel under ACES and drew a lime shell.

### Local-light scatter

After the punctual light is multiplied by fog transmittance, `LocalLightFogScatter` marches **8** steps from the camera to the shaded point. Density is all three terms. The light's windowed attenuation is clamped to 1.5 so a sample on the bulb cannot dump a solid ball into the fog. The added color is `albedo * lightColor * attenuation * 0.0015`. The pass is additive; it does not add the sun/ambient fog again.

---

## Host packing

`FogGpu` is the CPU copy. `makeFogGpu(env, waterLevel, lighting)`:

- Always sets water level, `heightFogHeight = waterLevel`, falloff 0.06, and slab height 14, then overwrites falloff and slab from the environment when lighting is on and `env` is non-null.
- Lighting off, or a null environment, zeros the three densities and the fog color and returns. Tuned falloff and slab height are not copied on that path; the densities are zero, so the integrate does nothing.

`applyFogToLighting` copies the struct into `LightingConstants`. Those fields sit in the deferred root constants (`fogDensity` beside `cameraPos`, the height and valley block after `ambientColor`). `sizeof(LightingConstants)` is 56 floats. The packing is pinned by `Fog.ApplyFogToLightingCopiesFields` and the `static_assert`s on `LightingConstants`: fog must not push `pbrLightColor` off a 16-byte boundary or grow the buffer past the 64-DWORD root-signature budget.

Sandbox hybrid path, after the G-buffer:

```text
FogGpu fog = makeFogGpu(&env, waterLevel, lighting);
fillFogHeightMap(fog, &terrain.coarse());
applyFogToLighting(lc, fog);
```

`waterLevel` is `m_water.params().waterLevel`. The height texture is the coarse map, not the working resolution.

Forward terrain (`Terrain::draw` with an environment) copies densities itself and sets `heightFogHeight` to the height-map origin Y. It still zeros fog when `lighting` is false. The shader then forces `volumetricFogDensity` to 0, so the valley march contributes nothing even though `FOG_SAMPLE_CSM` is on and the 16-step sun march still runs for distance and height.

Water's constant fill calls `makeFogGpu` when an environment is passed and `lighting` is true.

---

## Dev tools

Sandbox Dev Tools (M) → **Fog**.

| Control | When | Effect |
|---------|------|--------|
| Auto from weather / time | always | `fogAuto`. On: `evaluate` overwrites color and the three densities. Off: `setFog*` values stick across `evaluate` (`ManualFogSurvivesEvaluate`). |
| Distance / height / valley scale | auto | Multipliers, default 1, slider 0–6. |
| Distance / height / valley density, fog color | manual | Raw coefficients. Distance slider 0–0.08, height and valley 0–0.12. |
| Height falloff | always | `heightFogFalloff`, default 0.06, slider 0.01–0.25. Not overwritten by auto evaluate. |
| Valley height (m) | always | `volumetricFogHeight`, default 14, slider 2–40. Same. |
| Reset fog | always | `resetFogTune`: auto on, scales 1, falloff 0.06, slab 14, then `evaluate`. |

The line above the controls prints the live densities (`dist`, `height`, `valley`) and a swatch of `fogColor`.

---

## Tests

| Test | Pins |
|------|------|
| `Fog.HeightFogAmountPeaksOnTheHorizon` | Gate is 1 on the horizon and ~0 at ±0.4 rad and at 1.1 rad. |
| `Fog.HeightOpticalDepthThickerAtLowAltitude` | A 40 m horizontal ray at y=2 is more than twice the depth of the same ray at y=40. |
| `Fog.ValleyDensityOnlyWhereTerrainMeetsWater` | On the water line over low terrain the density is live; 20 m up, or over terrain 20 m above the water, it is ~0. |
| `Fog.MakeFogGpuZerosWhenUnlit` | Lighting off zeros densities. Lighting on at dusk copies a non-zero height density and the water level into both `waterLevel` and `heightFogHeight`. |
| `Fog.ApplyFogToLightingCopiesFields` | Field copy, and `LightingConstants` stays 56 floats with `pbrLightColor` at float 48. |
| `Fog.FillFogHeightMap_CoarseAndDummy` | Null/invalid map forces `heightCellSize` 0. A 1025² map at cell 2 m and origin −1024 is a 2048 m world. |
| `Environment.OvercastDimsTheSun` | Storm distance density exceeds clear. |
| `Environment.HeightFogPeaksAtDawnAndDusk` | Dusk and dawn exceed noon by 8×. Noon is under 1e-3. Midnight is under a quarter of dusk. |
| `Environment.ValleyFogThickensInStorms` | Storm valley density exceeds clear, and clear stays above 0.005. |
| `Environment.ManualFogSurvivesEvaluate` | `fogAuto` false keeps the set densities and color. |

---

## Limits

- One segment, camera to the first surface. Fog behind a transparent, inside a cloud volume, or between the water surface and the lake bed is not a second integral. Water fogs the ray to the surface; the opaque under it was already fogged to the G-buffer depth before the water drew.
- The sky budget is 280 m. Raising distance density fogs the sky on that fixed length, not on a ray to infinity.
- Valley fog off the coarse map, or with a zero cell size, follows the rules above (none, or fully wet). It does not fall back to a second map.
- The height-fog clamp lives on the camera density inside the closed form. A ray that climbs out of a thick slab still uses that camera base for the analytic term. The marched sun samples use `FogHeightDensity`, which clamps per sample.
- Forward meshes, particles, clouds, and the Editor are outside this pass. Turning up Sandbox fog does not change those.
- Local-light in-scatter is a deliberately small 8-step term. It is not the deferred sun march.
- `FOG_MARCH_STEPS` is a compile-time count. It is not on the dev panel.
