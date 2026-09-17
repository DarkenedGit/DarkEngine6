# Screen-space ambient occlusion (GTAO)

| Field | Value |
|-------|-------|
| **Status** | Draft rev 1 |
| **Date** | 2026-09-17 |
| **Depends on** | color-management, pbr-material-maps (normals), ibl (apply target) |

## Summary

Add **GTAO** (Jimenez et al. / XeGTAO-style) at half resolution, temporally stabilized with existing velocity, applied **only** to ambient/IBL (not direct sun or local punctuals).

## Goals (v1)

- Half-res AO R8 target.
- Upsample + temporal filter using velocity RT.
- Multiply IBL/ambient in deferred lighting (and sky ambient if any).
- Debug view; intensity radius power params.
- Disable flag leaves lighting bitwise-identical for direct terms.

## Non-goals (v1)

- HBAO+, bent normals GI, multi-bounce AO.
- AO on transparent water (follow-up).

## Key decisions

1. **Algorithm: GTAO** (better thin-feature behavior than classic SSAO).
2. **Half resolution** produce, bilateral upsample to full.
3. **Apply to ambient/IBL only** (`direct * 1 + ambient * ao`).
4. **Temporal:** accumulate in history using TAA velocity; clamp history.
5. **Depth:** existing D32; reconstruct view position like deferred.

## API

```cpp
struct GtaoSettings {
  bool  enabled = true;
  float radius = 0.5f;      // view-space meters-ish
  float thickness = 1.0f;
  float power = 1.5f;
  float intensity = 1.0f;
  int   steps = 4;
  int   directions = 4;
  bool  halfRes = true;
};

class GtaoPipeline {
  bool create(ID3D12Device*);
  void resize(uint32_t w, uint32_t h);
  void compute(cmd, depthSrv, normalSrv, velocitySrv, const Camera&, const GtaoSettings&);
  GpuSrv aoSrv() const;
};
```

## Frame order

After G-buffer, **before** deferred lighting (or lighting reads previous frame AO — prefer same-frame):

`GBuffer → GTAO → DeferredLighting(+IBL) → LocalLights → …`

## Acceptance tests

| Test | Expected |
|------|----------|
| `Gtao_FlatPlane_Center_NearOne` | AO ≥ 0.95 |
| `Gtao_CornerFixture_Darker` | corner < plane - 0.1 |
| `Gtao_Disabled_AmbientUnchanged` | AO tex white / skip multiply |
| `Gtao_PowerZero_White` | intensity path → no darkening |

Visual: crevice under crate darkens; no AO shimmer when camera still (temporal on).

## Rollout

1. Raw half-res GTAO compute/pixel pass
2. Upsample + bind ambient multiply
3. Temporal
4. Tunables in Editor

## Risks

- Halos on thin geometry — thickness param
- Temporal ghosting — clamp


## Resource table

| Name | Size | Format | Notes |
|------|------|--------|-------|
| `AoHalf` | w/2 × h/2 | R8_UNORM | raw GTAO |
| `AoHistory` | w × h | R16_FLOAT | temporal |
| `AoFull` | w × h | R8_UNORM | upsampled output |

## Root / compute params

```cpp
struct GtaoGpuParams {
  float2 invSizeHalf;
  float2 invSizeFull;
  float4x4 invProj;
  float4x4 reprojection; // thisViewProj * invPrevViewProj
  float radius, thickness, power, intensity;
  int steps, directions;
  float nearZ, farZ;
};
```

Bind: t0 depth, t1 world-normal (from GBuffer attrib decode or separate), t2 velocity, t3 aoHistory; u0 aoHalf/u0 aoFull.

## Exact ambient apply

In deferred lighting / IBL composite:

```hlsl
float ao = AoFull.Sample(pointClamp, uv).r;
ao = pow(saturate(ao), power); // or power already applied in pass
float3 ambient = evaluateIbl(...) * ao * intensityAo;
float3 color = directSun + directLocal + ambient + emissive;
```

Direct sun/CSM and local volume lights **do not** multiply by AO in v1.

## More unit tests

| Test | Detail |
|------|--------|
| `Gtao_Settings_Defaults` | enabled, halfRes true, power 1.5 |
| `Gtao_Resize_RecreatesTargets` | 1280x720 → 1920x1080, `aoSrv` valid |
| `Gtao_Create_NoDevice_False` | nullptr device → false |

## Manual QA checklist

1. Enable AO debug view — should look like soft creases, not white noise when still.
2. Toggle `enabled` — direct sunlight unchanged (screenshot difference only in shadowed ambient regions).
3. Move camera around a crate — no 1-frame black flash (temporal).
