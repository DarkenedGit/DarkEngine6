# Reflections (SSR v1)

| Field | Value |
|-------|-------|
| **Status** | Draft rev 1 |
| **Date** | 2026-09-17 |
| **Depends on** | IBL, material maps (normals), hierarchical depth optional later |

## Summary

**v1 = Screen-space reflections** for glossy surfaces (roughness below threshold), composited with IBL specular. Reflection probes deferred to follow-up. SSR traces against depth buffer; misses fall back to IBL.

## Goals (v1)

- Hi-Z optional; v1 linear ray march in screen space against full-res depth (or half-res).
- Gate: `roughness <= 0.4` and metallic/dielectric fresnel weight.
- Temporal accumulation using velocity.
- Compose: `lerp(IBLSpec, SSR, ssrConfidence)`.
- Debug view of hit UV / confidence.

## Non-goals (v1)

- Rough specular SSR blur to match GGX lobe exactly (approx with mip on HDR color).
- Planar reflection RT for mirrors.
- Probe blend network.

## Key decisions

1. **Primary v1 = SSR**, probes = follow-up (faster outdoor win with IBL already).
2. **Trace against post-lighting HDR** of previous frame **or** current pre-transparent — **frozen: previous frame HDR + current depth** to avoid include self light feedback complexity; document ghosting.
3. Max steps 32; thickness threshold; fade at screen edges.
4. Roughness→blur: sample HDR color at mip `roughness * k`.

## API

```cpp
struct SsrSettings {
  bool enabled = true;
  float maxRoughness = 0.4f;
  int   maxSteps = 32;
  float thickness = 0.2f;
  float stride = 2.0f;
  float edgeFade = 0.1f;
};

class SsrPipeline {
  bool create(...);
  void execute(cmd, depth, normals, prevHdr, velocity, settings);
  GpuSrv resultSrv() const; // RGB radiance, A confidence
};
```

Frame: after lighting+sky (+local), before transparencies **or** after (water separate). **Frozen:** after deferred+local+sky, SSR, then water/particles forward.

## Acceptance tests

| Test | Expected |
|------|----------|
| `Ssr_Miss_ConfidenceZero` | sky miss → conf=0 |
| `Ssr_RoughGate` | roughness 0.9 → no trace / conf=0 |
| `Ssr_FallbackIbl` | conf=0 → lighting matches IBL-only specular path |

Visual: glossy floor reflects crate; at screen edge fades to IBL; no huge speckles when still (temporal).

## Rollout

1. Ray march + confidence
2. Compose in deferred or fullscreen composite pass
3. Temporal
4. Half-res trace

## Risks

- Feedback sparkles — use prev HDR
- Cost — half-res + early out

## Ray march details (v1)

```
viewPos = reconstruct(uv, depth)
viewDir = normalize(viewPos)
R = reflect(viewDir, nView)
for step in 0..maxSteps:
  viewPos += R * stride * depthDependent
  uv2 = project(viewPos)
  if uv2 out of screen → miss (fade)
  sceneZ = depthAt(uv2)
  if sceneZ within thickness behind rayZ → hit
```

Confidence: edge fade * fresnel * (1 - roughness/maxRoughness) * hit.

## Composite

```hlsl
float3 iblS = iblSpecular(...);
float3 ssrS = SsrTex.Sample(...).rgb;
float conf = SsrTex.a;
float3 spec = lerp(iblS, ssrS, conf);
```

## Extra tests

| Test | Expected |
|------|----------|
| `Ssr_Project_RoundTrip` | CPU project/unproject center pixel |
| `Ssr_EdgeFade_ZeroAtBorder` | uv 0/1 → fade 0 |
| `Ssr_Settings_MaxRoughnessGate` | 0.41 roughness gated out |

## Manual QA

1. Glossy floor (`roughness~0.05`) reflects colored prop.
2. Raise roughness above 0.4 → SSR off, IBL only.
3. Look at sky → confidence 0.
4. Strafe camera — temporal keeps reflection stable without long smear.
