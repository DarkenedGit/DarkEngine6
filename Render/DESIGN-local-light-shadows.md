# Local light shadows (spot atlas v1)

| Field | Value |
|-------|-------|
| **Status** | Draft rev 1 |
| **Date** | 2026-09-17 |
| **Depends on** | DESIGN-local-lights.md (volumes landed) |

## Summary

Shadow maps for **spot** local lights via a shared **atlas**, PCF filtered in `LocalLightVolume` shader. Point-light cubes are follow-up. Wire existing `castShadow` (or add) on local light component.

## Goals (v1)

- 2048² atlas, slots e.g. 512² (16 spots) or 256² (64) — **frozen default: 512² slots in 2048 atlas (16)**.
- Per-frame allocate slots by screen-area / camera distance score (same gather as lights).
- Depth-only spot draws (static + skinned casters subset).
- Volume lighting samples atlas with PCF 3×3.
- Budget: `maxShadowedSpots=4` per frame default (subset of 16 atlas residents).

## Non-goals (v1)

- Point omnidirectional cubes.
- Soft PCSS / VSM / ray-marched shadows.
- Cached static shadow maps across frames (nice follow-up).

## Key decisions

1. **Format:** `D16_UNORM` atlas (or D32 if bias fights — start D16).
2. **Bias:** constant + slope; tunable.
3. **Only spots** with `castShadow && enabled && intensity>0`.
4. **Caster pass:** reuse mesh/skinned shadow PSO variants from CSM where possible (different VP).
5. Failure to allocate slot → light still fills unshadowed.

## API

```cpp
struct LocalShadowSettings {
  uint32_t atlasSize = 2048;
  uint32_t slotSize = 512;
  uint32_t maxShadowedSpots = 4;
  float depthBias = 0.001f;
  float slopeBias = 1.5f;
  float pcfRadius = 1.0f;
};

struct LocalShadowSlot {
  uint16_t lightIndex; // into LocalLightGpuList
  uint16_t atlasX, atlasY; // pixel origin
  float lightViewProj[16];
};

class LocalShadowAtlas {
  bool create(Renderer&, const LocalShadowSettings& = {});
  void beginFrame();
  bool allocate(uint16_t lightIndex, const SpotLightView&, LocalShadowSlot& out);
  void renderCasters(cmd, World&, /*caster draw*/);
  GpuSrv atlasSrv() const;
};
```

ECS: `LocalLightComponent::castShadow` bool (verify tip; add if reserved).

## Shader contract

In `LocalLightVolume.hlsl`, if light flagged shadowed: transform world pos to atlas UV, compare depth, multiply lighting by shadow factor.

## Acceptance tests

| Test | Expected |
|------|----------|
| `LocalShadow_AtlasPack_NoOverlap` | slots non-overlapping |
| `LocalShadow_Budget` | 5th shadowed spot → no slot / unshadowed |
| `LocalShadow_DisabledFlag` | castShadow false → no allocate |
| `LocalShadow_SpotMatrix_ForwardMapsToCenter` | unit test VP |

Visual: flashlight shadow of cube on ground; peter-pacing within bias tuning.

## Rollout

1. Atlas resource + allocator tests
2. Depth draw for one spot
3. PCF in volume shader
4. Skinned casters
5. Editor toggle on light

## Risks

- Atlas thrashing when lights move — hysteresis score
- Extra draw cost — keep maxShadowedSpots low


## Depth pass PSO

- Format: D16 atlas as DSV (single 2D texture; viewport per slot).
- Raster: depth-only, no color writes; front solid; bias via `DepthBias`/`SlopeScaledDepthBias`.
- Shaders: reuse CSM shadow VS/PS with different CB `LightViewProj` + viewport.

## Spot projection

```
fovY = outerConeAngle * 2 (document unit: radians)
aspect = 1
near = 0.05
far = light.range
view = lookAt(light.pos, light.pos + light.dir, upStable(light.dir))
proj = perspectiveFovLH(fovY, aspect, near, far)
```

Stable up: pick world up unless nearly parallel, then +X.

## Atlas CPU allocator

```cpp
// tests
LocalShadow_AtlasPack_16Slots
LocalShadow_AtlasPack_RejectsWhenFull
LocalShadow_Score_PreferCloserToCamera
```

Score = `importance * intensity / max(dist, 0.1)` with hysteresis (+10% keep previous slot holders).

## Shader sampling

```hlsl
float shadowPcfsSpot(Texture2DArrayOr2D atlas, float3 worldPos, float4x4 lightVP, float2 slotUvOrigin, float slotScale, float bias);
// atlas is 2D; UV = origin + uv * scale
```

## Component field

```cpp
struct LocalLightComponent {
  // ...
  bool castShadow = false; // v1 default off
};
```

JSON: `"castShadow": true` round-trip UnitTest.

## Manual QA

1. Spot-only scene, `castShadow=true`, opaque box → clear umbra on ground.
2. Disable flag → unshadowed but still lit.
3. 5 shadowed spots with maxShadowedSpots=4 → weakest drops shadows without disappearing light.
