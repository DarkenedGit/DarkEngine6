# Auto-exposure

| Field | Value |
|-------|-------|
| **Status** | Draft rev 1 |
| **Date** | 2026-09-17 |
| **Depends on** | TonemapPipeline, Bloom; color-management |

## Summary

GPU **log-luminance histogram** (or hierarchical reduce) → EV that adapts toward a target mid-grey, driving `TonemapSettings::exposure`. Manual exposure override remains.

## Goals (v1)

- 64-bin log histogram of HDR color (after lighting+sky+transparents, **before** bloom or after — **K1**).
- Percentile metering (e.g. 50th) → exposure.
- Smooth adaptation (different up/down speeds).
- `ExposureMode::Manual | Auto`.
- Editor/Sandbox debug readout of EV.

## Non-goals (v1)

- Local tonemapping / eye adaptation vignette.
- Histogram on faces via compute UAV without clear ownership — OK to use compute.

## Key decisions

1. **Meter after opaque+sky+local lights+forward transparents**, on HDR; **bloom uses post-exposure HDR** (scale HDR by exposure before bloom) so bloom tracks perception.
2. **Target:** middle grey 0.18 in linear after exposure.
3. **Adaptation:** `ev = lerp(ev, targetEv, 1 - exp(-dt * speed))` with `speedDarken` > `speedBrighten` optional.
4. **Clamp EV** to [−4, +4] defaults.
5. Manual mode: use `Environment::exposure()` only.

## API

```cpp
enum class ExposureMode : uint8_t { Manual, Auto };

struct AutoExposureSettings {
  ExposureMode mode = ExposureMode::Auto;
  float manualExposure = 1.0f;   // multiplier
  float targetGrey = 0.18f;
  float minEv = -4.f, maxEv = 4.f;
  float adaptUp = 3.f;           // 1/s
  float adaptDown = 1.f;
  float histogramLogMin = -8.f;
  float histogramLogMax = 4.f;
};

class AutoExposurePass {
  bool create(ID3D12Device*);
  // reads HDR SRV, writes exposure float to CB or readback ring
  void execute(cmd, hdrSrv, uint32_t w, uint32_t h, float dt, AutoExposureSettings&);
  float exposureMultiplier() const; // exp2(ev) or linear scale — freeze: linear multiply on HDR
};
```

Integrate in `SceneRenderer::applyPost`: auto-exposure → bloom → TAA → MB → tonemap (tonemap exposure=1 if already applied).

## Acceptance tests

| Test | Expected |
|------|----------|
| `AutoExp_EvClamp` | target beyond max → maxEv |
| `AutoExp_ManualIgnoresHistogram` | mode Manual → multiplier == manualExposure |
| `AutoExp_AdaptMonotonic` | darker frame raises exposure over frames |
| `AutoExp_DtZero_NoChange` | dt=0 keeps EV |

Visual: tunnel exit adapts without instant clip; bloom doesn't explode more than manual tuned scene.

## Risks

- Fireflies skew histogram — percentile not mean; optional firefly clamp
- Readback latency — keep exposure on GPU CB updated by compute without CPU readback for v1


## Histogram bins

- 64 bins, log2 luminance from `histogramLogMin`..`histogramLogMax`.
- Luma = `dot(hdr.rgb, float3(0.2126, 0.7152, 0.0722))` in linear.
- Ignore pixels with luma < 1e-6 (sky-optional include — **v1 include all**).
- Metering value = luminance at **50th percentile** bin; convert to EV relative to `targetGrey`.

```
evTarget = log2(targetGrey / max(meterLuma, 1e-4))
ev = clamp(adapt(ev, evTarget, dt), minEv, maxEv)
exposureMul = exp2(ev)
hdrExposed = hdr * exposureMul
```

## GPU dispatch sketch

1. Clear histogram UAV (uint bins).
2. Fullscreen/compute thread per pixel → `InterlockedAdd` bin.
3. Prefix sum / reduce to percentile → write `ExposureCB.exposureMul`.
4. No CPU readback required.

## Interaction matrix

| Mode | Bloom input | Tonemap exposure field |
|------|-------------|------------------------|
| Auto | post-`exposureMul` HDR | 1.0 (already applied) |
| Manual | HDR * manualExposure | 1.0 same pattern OR tonemap*manual — **frozen: apply before bloom for both** |

## Extra tests

| Test | Expected |
|------|----------|
| `AutoExp_Percentile50_KnownBuffer` | synthetic buffer with known median → expected EV±0.05 |
| `AutoExp_AllBlack_ClampsMax` | meter floor → maxEv |
| `AutoExp_AllWhite_ClampsMin` | → minEv |
| `AutoExp_SpeedUpVsDown` | adaptUp≠adaptDown constants respected in CPU sim |
