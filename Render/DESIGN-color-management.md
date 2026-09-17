# Color management (linear / sRGB)

| Field | Value |
|-------|-------|
| **Status** | Draft rev 1 |
| **Date** | 2026-09-17 |
| **Priority** | P0 — build first |
| **Depends on** | Deferred HDR path (landed) |

## Summary

Establish a single color pipeline: **sRGB for authored albedo/UI**, **linear float for lighting HDR**, **sRGB encode only in tonemap→swapchain**. Today albedo `R8G8B8A8_UNORM` without `_SRGB` and unclear shader decode risk double-gamma or washed BRDF.

## Goals (v1)

- Document and enforce working-color-space rules for every sampled texture class.
- Provide CPU helpers `srgbToLinear` / `linearToSrgb` (exact IEC 60966-2-1 piece-wise).
- Albedo (and UI sprites drawn as sRGB) sample as linear in shaders.
- HDR scene color (`R16G16B16A16_FLOAT`) stays linear.
- Tonemap outputs display-referred sRGB into `R8G8B8A8_UNORM` swapchain (with or without `_SRGB` RTV — pick one, see K2).
- Debug flag to visualize “linear albedo vs raw UNORM” mismatch.

## Non-goals (v1)

- Full ACES viewing transform / Rec.2020 / HDR10 display path.
- Per-texture ICC profiles.
- Changing HDR format away from RGBA16F.

## Key decisions

1. **Working space = linear Rec.709 primaries**, unitless radiance-like values (same as current GGX path).
2. **Albedo / baseColor textures** use DX12 format `R8G8B8A8_UNORM_SRGB` **or** shader `srgbDecode` — **v1 pick: `_SRGB` SRV** for albedo maps; solid colors authored in sRGB bytes converted on CPU when creating solid images.
3. **Data maps** (normal, ORM, masks, HUD mono) stay **linear UNORM** (no `_SRGB`).
4. **HDR targets** never use `_SRGB`.
5. **Tonemap** applies OETF (sRGB encode) when writing swapchain **if** swapchain RTV is non-sRGB; if swapchain RTV is `_SRGB`, write linear and let HW encode — **v1: non-sRGB swapchain + shader encode** (matches current ACES/saturate path control).
6. **No exceptions**; invalid formats fail `bool` create.

## Current tip hooks

- `Assets/Material.h` — albedo `AssetRef<Image>` + `baseColor[]`
- `Assets/Image` / GPU upload in `Render/GpuUpload.*` / `GpuResourceCache`
- `content/shaders/*GBuffer*.hlsl` — sample albedo
- `Render/TonemapPipeline.h` — `TonemapSettings::exposure`, ACES mode
- `Render/SceneBuffers.h` — HDR + G-buffer formats

## Architecture

```
Authoring (sRGB bytes)
    → Image asset (tag ColorSpace::sRGB or Linear)
    → GpuResourceCache creates SRV (_SRGB vs UNORM)
    → GBuffer PS: albedo already linear
    → DeferredLighting / LocalLights / IBL: linear HDR
    → Bloom/TAA/MB: linear
    → Tonemap: linear → sRGB bytes → swapchain
```

## Public C++ API

```cpp
namespace Dark::Color {
  enum class ColorSpace : uint8_t { Unknown, sRGB, Linear };

  float srgbToLinear(float c);          // piece-wise
  float linearToSrgb(float c);
  void  srgbToLinear3(const float srgb[3], float linear[3]);
  void  linearToSrgb3(const float linear[3], float srgb[3]);

  // Image import tag; default albedo=sRGB, normal/ORM=Linear
  ColorSpace inferColorSpaceForUsage(TextureUsage usage);
}

enum class TextureUsage : uint8_t { Albedo, Normal, Orm, Emissive, Data, Hud };

struct ImageCreateInfo {
  ColorSpace colorSpace = ColorSpace::Unknown;
  // ...
};
```

Defaults: `Material` albedo path → `TextureUsage::Albedo` → sRGB.

## GPU formats

| Usage | Typeless/resource | SRV |
|-------|-------------------|-----|
| Albedo | `R8G8B8A8_UNORM` | `R8G8B8A8_UNORM_SRGB` |
| ORM / normal / masks | `R8G8B8A8_UNORM` | `R8G8B8A8_UNORM` |
| HDR | `R16G16B16A16_FLOAT` | float |
| Swapchain | UNORM | write encoded |

## Shader contract

```hlsl
// Color.hlsli
float3 srgbToLinear(float3 c);
float3 linearToSrgb(float3 c);
// Prefer HW _SRGB for albedo; helpers for CPU-authored constants / debug
```

GBuffer: treat sampled albedo as linear. `baseColor` tint multiplied in linear.

## Algorithms

IEC 60966-2-1:

- If \(c \le 0.04045\): \(c/12.92\) else \(\bigl((c+0.055)/1.055)\bigr)^{2.4}\)

Inverse for encode.

## Frame integration

No new pass. Upload/create sites + tonemap + GBuffer sample.

## Debug

- `DebugRenderState::showAlbedoLinear` — bypass lighting, show albedo
- `showAlbedoRaw` — force non-sRGB view for diff

## Acceptance tests

### Unit (CPU)

| Test | Input | Expected |
|------|-------|----------|
| `Color_SrgbToLinear_BlackWhite` | 0, 1 | 0, 1 |
| `Color_SrgbToLinear_MidGray` | 0.5 | ≈0.214 (tol 1e-4) |
| `Color_RoundTrip` | 32 evenly spaced [0,1] | max err < 2/255 |
| `Color_InferUsage` | Albedo→sRGB, Normal→Linear | enum match |
| `Material_Solid_CpuLinearStoredOrTagged` | createSolid(188,…) | documented path: either store linear floats or tag sRGB bytes — assert one convention |

### Visual / golden

- Grey albedo 188/255 dielectric sphere under known directional: luminance within 5% of Filament reference spreadsheet for same L.
- Screenshot pair raw vs _SRGB must differ on colorful albedo.

### Negative

- Creating `_SRGB` SRV on `R16G16B16A16_FLOAT` fails `bool`, logs, no throw.
- Missing colorSpace tag defaults by usage; never throws.

## Rollout

1. CPU helpers + UnitTests
2. Tag Image/Material upload path
3. Flip albedo SRVs to `_SRGB`
4. Verify tonemap encode once; soak Sandbox/Editor

## Risks

- Double encode if tonemap and `_SRGB` RTV both apply.
- Existing content authored assuming broken gamma may “look darker” after fix — document as intentional.
