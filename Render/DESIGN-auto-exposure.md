# Auto-exposure

| Field | Value |
|-------|-------|
| **Status** | Draft rev 2 |
| **Date** | 2026-09-29 |
| **Supersedes** | Draft rev 1 (2026-09-17), the 64-bin compute histogram |
| **Depends on** | Landed color management (linear Rec.709 HDR, exposure at tonemap). `TonemapPipeline`, `BloomPipeline`, `SceneRenderer::applyPost`. |
| **Area** | `Render/AutoExposure.*` (new), `content/shaders/AutoExposure.hlsl` (new), `BloomPipeline` threshold, `SceneRenderer::applyPost`, Sandbox dev tools |

A small exposure correction on top of `Environment::exposure()`. The meter is the center-weighted geometric mean of scene-referred luminance, built with the same graphics downsample as bloom. It does not replace the dusk/noon curve, and it does not scale the HDR buffer.

Rev 1 metered a full-frame 64-bin histogram with compute atomics, clamped to ±4 EV, and proposed multiplying that into the HDR image before bloom and temporal anti-aliasing. That fights this post stack and the time-of-day exposure. The histogram stays a later option for interiors. v1 is the downsample below.

---

## What the frame does today

Sandbox HybridDeferred writes linear Rec.709 into `R16G16B16A16_FLOAT`. `SceneRenderer::applyPost` then runs:

1. **Bloom** on that unexposed image. Extract is half-resolution. Threshold is `BloomPipeline::kThreshold` (**1.0**) with knee **0.5**, both in scene luminance. The composite is additive (`ONE + ONE`) back onto the HDR target.
2. **Temporal anti-aliasing**, then **motion blur**, both on that HDR.
3. **Tonemap.** `TonemapSettings::exposure` is `Environment::exposure()`. Saturate mode is `linearToSrgb(saturate(hdr * exposure))`. ACES is Narkowicz of `hdr * exposure` with no extra encode.

`Environment::evaluate` sets the artistic exposure, not a camera meter:

```text
Lerp(0.55, 1.05, daylight) * (1 - 0.15 * cover)
```

Night sits near 0.55, clear noon near 1.05. Forward sky still multiplies by this inside the sky shader (`skyExposure = 1` only on the deferred path). The Editor leaves tonemap exposure at 1 and has no `Environment` on the 3D view.

Luminance for the meter is the same Rec.709 weights bloom already uses: `dot(rgb, float3(0.2126, 0.7152, 0.0722))`. Mid-grey is **0.18** linear. Sky depth is `IsSkyDepth` (`depth <= 0`) in `Depth.hlsli`.

The frame has no compute pass. Terrain erosion is `cs_5_0` on a private command list and is not a model for post. Bloom, temporal anti-aliasing, ambient occlusion, reflections, and motion blur are fullscreen pixel shaders and render targets.

---

## Goals (v1)

- Meter the current unexposed HDR, before bloom, on HybridDeferred.
- Geometric mean, center-weighted, sky pixels dropped.
- Smooth the error in EV. Drop exposure quickly when the view gets brighter. Raise it slowly when the view gets darker.
- Clamp the correction to a small range so dusk stays dusk.
- `finalExposure = Environment::exposure() * exp2(evCorrection)`.
- Tonemap keeps that product. Temporal anti-aliasing and motion blur stay scene-referred.
- Bloom’s threshold and knee scale by `1 / finalExposure`, so a pixel that is bright after exposure is the pixel that blooms.
- Manual holds the correction at 0. The artistic exposure is the manual control.
- Sandbox dev tools show measured luminance, the correction, and the final exposure.

## Non-goals (v1)

- A luminance histogram, a percentile, or a compute UAV on the frame list.
- Replacing `Environment::exposure()`, or a second manual multiplier defaulting to 1.
- Writing exposure into the HDR target. No extra full-resolution copy.
- Local tonemapping, a vignette of adaptation, or metering bloom, emissive-only layers, or faces.
- Editor. Exposure stays 1.
- Forward. It keeps today’s sky-shader exposure. The meter does not run, because the forward sky is already scaled and there is no deferred depth to reject sky.
- Moving temporal anti-aliasing or motion blur into exposed space.

---

## Key decisions

1. **Meter scene-referred HDR this frame**, the image bloom already samples. Bloom’s own glow is not in the meter, so the glow cannot chase itself. The formula `log2(0.18 / measured)` is valid only on that unexposed luminance.
2. **Geometric mean, not a mean of linear luma and not a median.** `exp2(mean(log2(luma)))` is the standard outdoor meter. A few fireflies barely move it. A 50th percentile of the whole frame, sky included, meters the sky.
3. **Drop sky, weight the center.** Sky weight is 0 via `IsSkyDepth`. Remaining pixels use a radial weight, 1 at the center and 0.15 at the mid-edge (the formula below). The ground under the crosshair leads. The horizon still counts.
4. **Quarter resolution is enough.** The first pass writes `max(width/4, 1)` by `max(height/4, 1)`. The eye cannot see a meter difference against full resolution, and the pyramid stays small.
5. **The correction rides on the artistic exposure.** Clamp `evCorrection` to **±1.5 EV** (about 0.35× to 2.8×). ±4 EV would lift night toward noon. An EV bias slider, default 0, adds before the clamp for “a bit brighter” without turning the meter off.
6. **Iris speeds.** When the target EV is below the current one (the view got brighter), adapt at **6 / s**. When the target is above (the view got darker), adapt at **1 / s**. The step is `lerp(ev, target, 1 - exp(-dt * speed))`. `dt = 0` holds EV.
7. **One float comes back to the CPU.** Double-buffer a readback of the 1×1 result and map the previous frame. Adaptation is slower than a frame, so the delay does not show. The dev-tools numbers, the clamp, and the unit tests live next to `Environment::exposure()` on the CPU. The first frames, a resize, and an all-sky frame hold the last correction (0 until the first sample lands).
8. **Bloom tracks exposure at the threshold.** `threshold = 1.0 / finalExposure`, `knee = 0.5 / finalExposure`. The composite stays additive in scene space, so tonemap scales scene and bloom together. Do not premultiply the HDR.
9. **Manual is correction 0.** Entering Auto resets the correction to 0 and adapts from the artistic exposure. Leaving Auto snaps the correction back to 0 on that frame.
10. **Graphics passes only.** Two render targets and fullscreen draws, Bloom-shaped. No `cs_5_0`, no `ALLOW_UNORDERED_ACCESS`.

---

## Meter

### Weight

At the quarter-res pixel, with `uv` in 0..1 and scene depth:

```text
r = length(uv * 2 - 1)          // 0 at center, 1 at mid-edge, sqrt(2) at a corner
w = IsSkyDepth(depth) ? 0 : lerp(0.15, 1.0, saturate(1 - r))
luma = dot(hdr, float3(0.2126, 0.7152, 0.0722))
```

Store `float2(log2(max(luma, 1e-4)) * w, w)` in **R32G32_FLOAT**. A product-and-weight pair downsamples with plain averages. A zero weight contributes nothing and cannot pull the log toward 0.

### Pyramid

Repeat a 2×2 average of that pair down to 1×1, the same halving bloom uses. At 1×1:

```text
measured = (weight > 1e-6) ? exp2(logSum / weight) : 0
```

`measured == 0` means no lit pixels (all sky, or a cleared frame). The CPU keeps the previous EV.

### CPU

```text
evTarget = log2(targetGrey / max(measured, 1e-4)) + evBias
speed    = (evTarget < ev) ? adaptBright : adaptDark
ev       = lerp(ev, evTarget, 1 - exp(-dt * speed))
ev       = clamp(ev, -maxEv, maxEv)
final    = envExposure * exp2(ev)
```

Defaults: `targetGrey = 0.18`, `evBias = 0`, `adaptBright = 6`, `adaptDark = 1`, `maxEv = 1.5`. Manual, or a zero measurement, forces the applied correction to 0 for the frame. A zero measurement does not wipe the stored `ev`, so the next good frame continues the adaptation.

`final` is `TonemapSettings::exposure` and the value passed into bloom.

---

## Passes and order

New `AutoExposurePipeline`, created and resized with the bloom targets in `SceneRenderer`. Runs only when the path is HybridDeferred, the pass was created, and the mode is Auto. Failure to create logs and leaves the correction at 0. The frame still tonemaps with `Environment::exposure()`.

```text
scene HDR + depth
    → quarter-res log/weight
    → downsample to 1×1
    → copy 1×1 into the readback slot for this frame
    → map the previous frame’s slot (0 on the first frame)
    → CPU adapts
bloom extract (threshold and knee scaled)
    → bloom composite onto HDR
    → TAA → motion blur
    → tonemap(final)
```

Insert the meter at the start of `applyPost`, before `BloomPipeline::draw`. Depth is still the scene depth. HDR is still the unexposed scene color, because bloom has not added itself yet.

`BloomPipeline::draw` gains the exposure multiplier (or the scaled threshold and knee). `kThreshold` and `kKnee` stay the scene-space defaults at exposure 1. Strength is unchanged.

No new full-resolution target. The pyramid is quarter-res R32G32_FLOAT and its mips. Readback is two buffers of one `float2`, one per frame slot, copied with `CopyTextureRegion` / a 1×1 copy into a readback heap. Map is the slot written two frames ago if the copy is still in flight on a one-frame-deep GPU; with the engine’s existing per-frame fence, mapping the other slot after `wait` for that frame is enough. Do not stall the frame to read this frame’s 1×1.

---

## API

```cpp
enum class ExposureMode : uint8_t { Manual, Auto };

struct AutoExposureSettings
{
    ExposureMode mode         = ExposureMode::Auto;
    float        targetGrey   = 0.18f;
    float        evBias       = 0.0f;
    float        maxEv        = 1.5f;   // clamp of the correction, not of the artistic exposure
    float        adaptBright  = 6.0f;   // 1/s, view got brighter, exposure falls
    float        adaptDark    = 1.0f;   // 1/s, view got darker, exposure rises
};

struct AutoExposureResult
{
    float measuredLuma = 0.0f;  // 0 = no sample this frame
    float evCorrection = 0.0f;
    float finalExposure = 1.0f; // envExposure * exp2(evCorrection), or envExposure in Manual
};

class AutoExposurePipeline
{
    bool create(ID3D12Device* device, uint32_t width, uint32_t height);
    bool resize(ID3D12Device* device, uint32_t width, uint32_t height);
    // GPU pyramid + copy into this frame’s readback slot. Returns false if the pass is down.
    bool meter(ID3D12GraphicsCommandList* cmd, Renderer& renderer);
    // Map the ready slot. 0 when none is ready or the weight was 0.
    float readMeasuredLuma(uint32_t frameIndex);
    bool isValid() const;
};
```

The adaptation itself is a free function so the tests do not need a device:

```cpp
struct AutoExposureState { float ev = 0.0f; };

// measured <= 0 keeps state.ev and returns a result with evCorrection 0 when manual,
// or the held ev applied when auto and the sample is missing — see decision 7.
// Decision 7, frozen here: a missing sample applies the previous evCorrection
// (0 until the first good sample). It does not reset toward 0.
AutoExposureResult adaptExposure(AutoExposureState& state, float measuredLuma, float envExposure,
                                 float dt, const AutoExposureSettings& settings);
```

`adaptExposure` is the only place the clamp, the speeds, and Manual live. `SceneRenderer::applyPost` calls `meter`, then the host (Sandbox) calls `adaptExposure` with `m_env.exposure()` and `dt` and writes `TonemapSettings::exposure` plus the bloom scale. Keeping the tick on the host matches the dev-tools readout and avoids `SceneRenderer` reaching into `Environment`.

Sandbox stores `AutoExposureSettings` and `AutoExposureState` next to `m_env`. Default mode is **Auto**. The sky panel’s exposure figure stays the artistic base. Dev Tools → a short **Exposure** block:

| Control | Effect |
|---------|--------|
| Auto | `mode`. Off holds the correction at 0. |
| EV bias | −2..2, default 0, added to the target before the clamp. |
| Max EV | 0..3, default 1.5. |
| Bright speed / dark speed | The two rates. |
| Readout | measured luma, EV correction, artistic exposure, final exposure. |

Editor does not grow this UI and does not create the pass.

---

## Acceptance tests

CPU, on `adaptExposure` and a shared log-average helper the shader’s 1×1 math matches.

| Test | Expected |
|------|----------|
| `AutoExp_ManualIsArtistic` | Mode Manual, any measured luma → correction 0, final == env exposure. |
| `AutoExp_MidGreyIsZero` | Measured 0.18, bias 0, dt large → correction ~0, final == env exposure. |
| `AutoExp_EvClamp` | Measured far below grey → correction == +maxEv. Far above → −maxEv. Final is env exposure times `exp2` of that. |
| `AutoExp_DtZero_NoChange` | `dt = 0` leaves the stored EV where it was. |
| `AutoExp_DarkerRaisesOverTime` | A steady dark measurement increases EV across frames and stays under the target until it arrives. |
| `AutoExp_BrightFallsFasterThanDarkRises` | From 0, one step toward +2 EV moves less than one step toward −2 EV with the default speeds. |
| `AutoExp_MissingSampleHolds` | Measured 0 leaves the stored EV unchanged and still applies it. |
| `AutoExp_EnterAutoResets` | Switching Manual → Auto starts the stored EV at 0. Documented as the host’s job when the checkbox flips; the test covers a one-line reset helper. |
| `AutoExp_LogAverage` | Weights `(1, luma 0.18)` and `(1, luma 0.72)` → geometric mean `sqrt(0.18 * 0.72)`. A zero-weight sample does not move it. |
| `AutoExp_BloomThreshold` | `threshold(exposure) = 1 / exposure`, `knee = 0.5 / exposure`. Exposure 1 keeps today’s 1.0 and 0.5. |

Shader compile of `AutoExposure.hlsl` (quarter-res pass and downsample pass) joins the existing shader-compile tests.

Visual check, not a unit test: walk from an open noon field into a shadowed forest. The forest lifts over about a second and stops inside the EV cap, so it does not become noon. Step back into the sun and the image comes down faster than it rose. Bloom on a campfire stays a highlight, not a disk that grows when the meter opens. Temporal anti-aliasing does not ghost a bright frame over a dark one when the meter moves.

---

## Risks

| Risk | What v1 does |
|------|----------------|
| All-sky frame (looking straight up) | Weight is 0. Hold the last correction. Do not divide by zero and do not snap to max EV. |
| Readback not ready on frame 0, or after a resize | Treat as a missing sample. Resize also resets the stored EV to 0 so a stale 1×1 from the old size cannot apply. |
| Fireflies and the sun disk | Log-average plus the center weight. The sun on the center pixel can still pull. The ±1.5 cap bounds it. A histogram is the follow-up if that pull is visible. |
| Meter sees the player’s own bright VFX | Accepted in v1. The center weight makes a full-screen flash count, which is what an iris would do. |
| Bloom threshold under a large artistic exposure | Scaling by the final product, not by the correction alone, keeps night (0.55) and the meter consistent. |
| Forward double exposure | Pass does not run. Forward sky keeps its shader multiply. |

---

## Later, not v1

A histogram percentile is the right meter when a bright window inside a dark room dominates the average. Build it only after an interior shows that failure. It would still feed `adaptExposure` the same single luminance, so the CPU side would not change. It would be the first frame-loop compute pass, which this stack has avoided on purpose.
