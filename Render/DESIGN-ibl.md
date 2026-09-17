# Image-based lighting (IBL)

| Field | Value |
|-------|-------|
| **Status** | Draft rev 1 |
| **Date** | 2026-09-17 |
| **Priority** | P0 |
| **Depends on** | DESIGN-color-management.md |

## Summary

Replace flat `ambientColor` with **split-sum IBL**: diffuse irradiance cubemap + GGX prefiltered specular mip chain + BRDF integration LUT (Karis / Filament). Environment authored as equirectangular HDR (`content/env/*.hdr|.exr`).

## Goals (v1)

- Load one equirect HDR environment per world/level (or engine default).
- Bake once at load (or offline tool) into GPU cubes + 2D LUT.
- Deferred directional pass adds IBL term; local lights unchanged.
- Runtime intensity + rotation (Y-up angle).
- Debug views: irradiance only, specular only, LUT.
- Feature flag `iblEnabled` (default on once content present; off → legacy ambient).

## Non-goals (v1)

- Real-time every-frame convolution for dynamic sky.
- Parallax-corrected local probes (see reflections RFC follow-up).
- Multiple simultaneous HDRIs blended.
- Spherical harmonics path (cubes only for v1).

## Key decisions

1. **Split-sum approximation** compatible with existing GGX in `PbrLighting.hlsli` / `content/shaders/PbrLighting.hlsli`.
2. **Bake on first `EnvironmentMap::ensureGpu`** on a graphics queue (synchronous wait OK for v1); optional offline `.iblcache` later.
3. **Cubemap size:** irradiance 32², prefiltered 128² with **5 mips**, LUT 256² RG16F.
4. **HDR formats:** irradiance/prefiltered `R16G16B16A16_FLOAT` (or `R11G11B10_FLOAT` if soak proves OK — start RGBA16F).
5. **Coordinate system:** same as engine world (+Y up); equirect −Z forward matching Filament unless tip sky says otherwise — document conversion once in bake.
6. **Ambient fallback:** if IBL off/failed, use existing `ambientColor * ambientIntensity`.
7. **On linear working space (now landed), multiply Lambert/GGX diffuse by `1/π` and retune `Environment` / candela in the same IBL PR.** Flip/remove `PbrLighting_DiffuseHasNoInvPi`. Do not ship IBL with the old engine-unit diffuse. Color-management **C14** deferred π here on purpose.

## Current tip hooks

- `DeferredLightingPipeline` / `LightingConstants` ambient fields
- `SkyPipeline` analytic sky (keep for background; IBL lighting separate)
- `content/shaders/DeferredLighting.hlsl`
- `Assets` image loaders for HDR — extend or add `HdrImage::loadEquirect`

## Architecture

```
content/env/kloppenheim_06_4k.hdr
    → HdrEquirect (CPU float RGB)
    → bake:
         equirect→cube (128)
         irradiance convolve → cube 32
         prefilter GGX → cube 128 + mips
         BRDF LUT → 256²
    → EnvironmentLighting bind table
    → DeferredLighting PS + LocalLight (optional specular only deferred)
```

## Public C++ API

```cpp
struct IblBakeSettings {
  uint32_t equirectToCubeSize = 128;
  uint32_t irradianceSize     = 32;
  uint32_t prefilterSize      = 128;
  uint32_t prefilterMips      = 5;
  uint32_t brdfLutSize        = 256;
  uint32_t sampleCountIrr     = 64;
  uint32_t sampleCountPref    = 64;
};

class EnvironmentMap : public Asset {
public:
  bool loadEquirect(AssetManager& assets, const std::string& virtualPath);
  bool ensureBaked(Renderer& renderer, const IblBakeSettings& = {});
  bool isReady() const;

  // GPU handles / indices into PackedSrvHeap — exact type per tip conventions
  GpuSrv irradianceCube() const;
  GpuSrv prefilteredCube() const;
  GpuSrv brdfLut() const;
};

struct IblShadingParams {
  float intensity = 1.0f;
  float rotationRadY = 0.0f;
  float maxRoughnessMip = 4.0f; // prefilterMips-1
  int   enabled = 1;
};

// On LightingConstants or adjacent CB:
// IblShadingParams + SRV table slot layout frozen below
```

`World` / Sandbox holds `AssetRef<EnvironmentMap> m_env`.

## GPU resources

| Resource | Type | Format |
|----------|------|--------|
| Equirect src | Texture2D | RGBA16F |
| Irradiance | TextureCube | RGBA16F |
| Prefiltered | TextureCube + mips | RGBA16F |
| BRDF LUT | Texture2D | RG16F |

Samplers: irradiance linear clamp; prefiltered linear clamp with mip; LUT linear clamp.

## Shader contract

```hlsl
// Ibl.hlsli
float3 iblDiffuse(float3 albedo, float metallic, float3 n, TextureCube irr, ...);
float3 iblSpecular(float3 F0, float roughness, float3 n, float3 v, TextureCube pref, Texture2D lut, ...);
float3 evaluateIbl(...); // diffuse*(1-F) + specular with energy terms matching Filament
```

Mip: `lod = roughness * maxRoughnessMip`.

Bind in deferred lighting after sun+CSM; **before** fog or **inside** fog as appropriate (match tip fog: typically light then fog).

## Algorithms

- Importance-sample GGX VNDF or Filament's prefilter (cite Karis 2014 / Filament IBL).
- Irradiance: cosine-weighted hemisphere.
- LUT: inputs \((N·V, roughness)\) → \((scale, bias)\) for F0.

## Frame integration

1. Level load → `loadEquirect` + `ensureBaked`
2. Each frame: if ready, bind IBL SRVs + params into deferred lighting
3. Sky draw still analytic or optional background sample of equirect (follow-up); v1 can keep analytic sky disc

## Debug

- `iblDebug = 0 off display, 1 irradiance, 2 prefiltered lod0, 3 lut`
- Intensity / rotation ImGui in Editor

## Acceptance tests

### Unit (CPU)

| Test | Expected |
|------|----------|
| `Ibl_BrdfLut_Rough0_Edge` | LUT(NdotV≈1,r≈0).g near 0, .r near 1 (tol loose 0.05) |
| `Ibl_BrdfLut_Rough1` | finite, in [0,1] |
| `Ibl_PrefilterMipCount` | settings.prefilterMips matches resource |
| `Ibl_LoadMissing_ReturnsFalse` | false, no throw |
| `Ibl_RotationMatrix_Y90` | +X maps to +Z (document handedness) |

### GPU / integration

| Test | Expected |
|------|----------|
| `Ibl_MetallicSphere_ShowsEnv` | chrome ball under HDRI ≠ flat ambient screenshot |
| `Ibl_Disable_FallsBackAmbient` | pixel RMSE to ambient-only path < tol when intensity=0 |
| `Ibl_IntensityZero` | IBL contribution ≈ 0 |

### Negative

- Bake with device lost / null renderer → false
- Unsupported file → false + log

## Rollout

1. BRDF LUT offline or compute + UnitTests
2. Equirect load + cube convert
3. Irradiance + prefilter bake
4. Wire DeferredLighting
5. Content: one Poly Haven outdoor HDRI under `content/env/`
6. Multiply Lambert/GGX diffuse by `1/π`; retune `Environment` / candela; flip `PbrLighting_DiffuseHasNoInvPi`. Same PR — do not land IBL with engine-unit diffuse.

## Risks

- Bake time hitch on load — show loading screen progress hook
- Energy mismatch vs sun candela after IBL — key decision 7 retunes sun/ambient/candela in this PR (`1/π`); auto-exposure RFC is a later soak
