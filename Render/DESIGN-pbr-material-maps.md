# PBR material maps (normal / ORM / emissive)

| Field | Value |
|-------|-------|
| **Status** | Draft rev 1 |
| **Date** | 2026-09-17 |
| **Depends on** | DESIGN-color-management.md |

## Summary

Extend `Dark::Material` beyond albedo + scalars to **normal**, **ORM** (AO/Rough/Metal), and **emissive** textures. G-buffer writers unpack into existing RT0/RT1 packing; scalars remain multipliers.

## Goals (v1)

- Optional maps on `Material`; missing map → current scalar/default behavior.
- Tangent-space normal maps → world octahedral in RT1.rg.
- ORM: R=AO, G=Roughness, B=Metallic (glTF-style); A unused.
- Emissive map RGB * scalar `emissive` → contribute to RT0.a encoding (see packing).
- Alpha **Mask** uses albedo.a (or dedicated mask) with `clip` in GBuffer PS.
- Editor/Material panel shows slots; recipe key includes map asset ids.

## Non-goals (v1)

- Height/parallax, clearcoat maps, anisotropy maps.
- Texture arrays / bindless material heap redesign (use existing per-draw SRV pattern).
- Changing to three G-buffer RTs.

## Key decisions

1. **ORM packed texture** preferred over separate rough/metal/AO files (glTF friendly).
2. **Normals:** BC5 or UNORM RG OK later; v1 `R8G8B8A8_UNORM` XYZ in RGB, construct Z.
3. **Scalar multipliers:** `roughness *= orm.g`, `metallic *= orm.b`, `ao *= orm.r` (default scalars 1,1,1 for AO new field).
4. **AO in G-buffer:** store in unused bit if possible — **v1: apply AO only in lighting via RT expansion OR pack AO into emissive encoding carefully**. Frozen choice: **pack AO into RT0 unused — RT0.a stays emissive**; pass AO as `attrib` hack **or** multiply into albedo early. **Decision: multiply AO into deferred ambient/IBL only by storing AO in RT1 — replace?**. Final v1 packing:

   - RT0: `rgb = albedoLinear`, `a = emissive` (unchanged)
   - RT1: `rg = octahedral world N`, `b = roughness`, `a = metallic` (unchanged)
   - **AO:** not in G-buffer v1; sample ORM in lighting not available → **bake AO into albedo at GBuffer time** as `albedo *= orm.r` for v1 ambient term approximation, **and** expose `Material::ao()` scalar. Document limitation until RT2 or bitpack.

   Better v1: **change RT1.b to pack rough in 0..0.5 and AO later** — too clever. **Accepted v1:** add optional `R8_UNORM` **RT2 AO** only if SceneBuffers easily extends; else albedo-premultiply AO.

   **Frozen:** Extend `SceneBuffers` with optional **RT2 `R8_UNORM` AO** when material maps feature enabled; deferred lighting samples it. If schedule tight, albedo multiply fallback behind `#define DE_GBUFFER_AO_RT 0`.

5. **Emissive map:** `emissiveOut = max(emissiveScalar, luminance(emissiveTex * emissiveColor))` written to RT0.a compressed as now (`emissiveGain` in lighting).

## Current tip hooks

- `Assets/Material.h` / `.cpp` / `materialRecipeKey`
- `Render/MaterialSurface.*` — `applyMaterialSurface` → GBuffer constants
- `MeshGBufferConstants` / mesh & skinned GBuffer shaders
- UnitTests `MaterialSurfaceTests`, `MaterialTests`

## Public C++ API

```cpp
class Material {
  // existing ...
  void setNormalImage(AssetRef<Image>);
  void setOrmImage(AssetRef<Image>);      // AO, Rough, Metal
  void setEmissiveImage(AssetRef<Image>);
  AssetRef<Image> normalImage() const;
  AssetRef<Image> ormImage() const;
  AssetRef<Image> emissiveImage() const;

  void setAo(float ao);   // default 1
  float ao() const;

  float normalScale() const; // default 1
  void setNormalScale(float);
};

// recipe key extends with ids + ao + normalScale
std::string materialRecipeKey(const Material&);
```

GPU bind group per draw: t0 albedo, t1 normal, t2 orm, t3 emissive (root signature bump).

## G-buffer / shader contract

```hlsl
// sample
float3 albedo = srgbTex.Sample(...).rgb * baseColor.rgb;
float3 nt = normalize(normalTex.Sample(...).xyz * 2 - 1);
nt.xy *= normalScale; nt = normalize(nt);
float3 nWorld = normalize(mul(nt, TBN)); // rows/cols per tip mesh
float4 orm = ormTex.Sample(...);
float rough = saturate(orm.g * roughScalar);
float metal = saturate(orm.b * metalScalar);
float ao = saturate(orm.r * aoScalar);
float emis = max(emissiveScalar, dot(emissiveTex.Sample(...).rgb, float3(0.3,0.59,0.11)));
```

Mask mode: `clip(albedoTex.a - cutoff)` with `cutoff=0.5` default.

## Mesh requirements

- Static/skinned meshes must provide **tangents** (or derive in shader from UV derivatives — v1 prefer authored tangents in Model).
- If no tangent: skip normal map (log once).

## Acceptance tests

### Unit

| Test | Expected |
|------|----------|
| `Material_RecipeKey_IncludesMapIds` | different normal id → different key |
| `Material_MissingMaps_Valid` | albedo-only still `isValid()` |
| `Material_OrmDecode_CpuFixture` | known pixel → ao/rough/metal |
| `Material_CopyFrom_CopiesMapRefs` | copyFrom duplicates refs |
| `MaterialSurface_Apply_GBufferConstants` | rough/metal/emis match |

### Visual

- Normal map on flat plane shows lighting variation under rotating sun.
- ORM roughness gradient sphere matches scalar-only equivalents at constant ORM.

### Negative

- Masked material without albedo.a → clip all or none documented; no throw.
- Broken image ref → ignore map, log, fallback scalar.

## Rollout

1. Material API + recipe + tests
2. Root signature / GBuffer shader sampling
3. Tangents in model path
4. AO RT2 or albedo multiply
5. Editor UI slots

## Risks

- Tangent basis mismatch (mirrored UVs)
- Root signature versioning vs particle/mesh pipelines
