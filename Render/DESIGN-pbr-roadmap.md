# PBR gap roadmap (post-deferred v1)

| Field | Value |
|-------|-------|
| **Status** | Draft rev 1 |
| **Date** | 2026-09-17 |
| **Tip baseline** | `d662d0cb` (models hold materials) |
| **Depends on** | [DESIGN-deferred-renderer.md](./DESIGN-deferred-renderer.md), [DESIGN-local-lights.md](./DESIGN-local-lights.md) |

## Purpose

Track the next seven deferred-PBR features in **build order**. Each child RFC is implementable and testable on its own; this doc freezes dependencies and track-level done-when.

## Build order

| # | RFC | Why this order |
|---|-----|----------------|
| 1 | [DESIGN-color-management.md](./DESIGN-color-management.md) | **Implemented** (execute-plan 5124c804 PRs 1–4). Wrong color space poisons every later BRDF/IBL constant |
| 2 | [DESIGN-ibl.md](./DESIGN-ibl.md) | Replaces flat ambient; needs linear HDR sampling |
| 3 | [DESIGN-pbr-material-maps.md](./DESIGN-pbr-material-maps.md) | Authored normal/ORM/AO/emissive into G-buffer |
| 4 | [DESIGN-ssao.md](./DESIGN-ssao.md) | Multiplies ambient/IBL only; needs good normals |
| 5 | [DESIGN-auto-exposure.md](./DESIGN-auto-exposure.md) | Makes bloom + ACES usable across lighting ranges |
| 6 | [DESIGN-local-light-shadows.md](./DESIGN-local-light-shadows.md) | Independent of IBL; needs stable local-light volumes |
| 7 | [DESIGN-reflections.md](./DESIGN-reflections.md) | SSR v1 composed with IBL; wants normal maps |

## Dependency graph

```
color-management (done)
       │
       ├──────────► ibl ──────► ssao (apply to IBL)
       │              │
       │              └─────────► reflections (SSR + IBL fallback)
       │
       └──────────► pbr-material-maps ──► ssao / reflections (quality)
       
local-lights (done) ──► local-light-shadows

tonemap/bloom (done) ──► auto-exposure
```

## Track done-when

1. All seven RFCs marked **Accepted** (or Implemented) with linked PRs.
2. UnitTests cover every **Acceptance tests → Unit** case named in those RFCs.
3. Sandbox HybridDeferred path: IBL outdoor HDRI + ORM materials + GTAO + auto-exposure + ≥1 shadowed spot + SSR on wet floor demo (or roughness-gated metals).
4. `-forward` path remains functional; new features no-op or degrade gracefully when G-buffer unavailable.
5. No C++ exceptions introduced; failures are `bool` + `DE_LOG_*`.

## Out of track (explicitly later)

- DDGI / path-traced GI / Lumen-like
- Area lights / LTC
- Clustered light lists
- Full frame graph
- Clear-coat / anisotropy / SSS lobes
- Point-light cubemap shadows (follow-up inside local-light-shadows RFC)

## PR policy

One feature RFC implementation = one PR (or stacked PR slice per RFC's Rollout section). Do not mix unrelated refactors.
