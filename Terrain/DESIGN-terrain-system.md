# Terrain system (Plan A — CDLOD + splat)

| Field | Value |
|-------|-------|
| **Status** | Draft rev 1 |
| **Date** | 2026-09-18 |
| **Tip baseline** | `239366ac9a1198778f4c42ea296f4a27352f21ae` (`main` Terrain/ tree) |
| **Depends on** | `Terrain/HeightMap`, `Terrain/SplatMap`, `Terrain/TerrainLod`, `Terrain/Terrain` (`TerrainWorld`), `Terrain/TerrainMaterial`, `Render/TerrainPipeline`, existing deferred PBR lighting |
| **Out of this PR** | No code. Follow-ups are sequential spikes below. |

## Purpose

Freeze an implementable outdoor heightfield stack for DarkEngine6 that matches ResearchBot Plan A (2026-09-18: **CDLOD + 4–8 metal-rough splat, no VT v1**) without throwing away the working geomipmap path overnight.

Companion one-pager: [DESIGN-terrain-cheatsheet.md](./DESIGN-terrain-cheatsheet.md).

Research inputs (workspace, not in-repo): `/workspace/darkengine6-terrain-system-brief.md`, `/workspace/darkengine6-terrain-cheatsheet.md`.

## Current tip (P0 audit)

**Present**

- `HeightMap` — regular-grid float samples, origin + cellSize + heightScale, bilinear `heightAtWorld` / `tryHeightAtWorld`, normals, min-max pyramid raycast, FBM / U16 / layer composite create paths.
- `TerrainWorld` — **chunked geomipmap**: power-of-two `chunkCells` (default 16), distance LOD table (`kMaxLodLevels = 8`), `restrictNeighborLods` (ΔLOD ≤ 1), `EdgeMask` welding so coarser neighbors drop T-junctions. CPU rebuilds `MeshData` per dirty chunk; GPU `Mesh` upload.
- `TerrainLod` — `buildPatchMesh` / `buildGridIndices` (water shares the index pattern). Skirts are **not** used; cracks are avoided by weld + skip-odd-edge-verts.
- `SplatMap` — **exactly 4** layers (`kMaxTerrainLayers = 4`), RGBA8 weights, one texel per height sample. `generateFromHeight` paints dirt/grass/rock/snow from height + slope rules. Shader/blender renormalizes.
- `TerrainMaterial` — four **albedo** layer textures + splat + shadow in a `PackedSrvHeap` (6 SRVs). Layer descs are tiling + tint. Surfaces go through `TerrainFrameConstants` / GBuffer path.
- Queries: `heightAtWorld`, `raycast`, `normalAtWorld`, frustum-culled `draw` / `drawGBuffer` / `drawDepth`.

**Missing vs Plan A**

| Plan A | Tip today |
| --- | --- |
| CDLOD patches + **distance morph** (no skirts) | Geomipmap + **edge weld**; no morph factor |
| `TerrainTile` ENU meters, streamed residency | One contiguous `HeightMap` / world |
| Height R16F on GPU sampled in VS | CPU mesh displacement into VB |
| 4–8 **metal-rough** layers + height-blend + RNM | 4 albedo + tint; soft RGBA; no height-blend / RNM / gated triplanar |
| Bindless layer arrays | Fixed 4 SRV slots in packed heap |
| Coarser **physics** HF synced on cook/sculpt | Same HF used for queries (no separate collision cook) |
| POM close-up | None |
| DEM / procedural / sculpt → one cook | FBM + U16 load helpers only; no cook pipeline |
| Virtual texture | None (keep it that way for v1) |

**Keep (do not rip in P1)**

- `HeightMap` sampling / raycast API surface used by gameplay.
- Chunk grid bookkeeping and frustum draw lists as a migration scaffold.
- Water’s shared `buildGridIndices` contract until water gets its own LOD story.
- Existing deferred lighting bind points; terrain must stay on the same metal-rough contract as props (PBR RFCs).

## Goals (v1 track)

1. One runtime tile format: `TerrainTile` (ENU origin, cell size meters, height R16F, ≤8 weights, materialSetId, minH/maxH, optional holes).
2. **CDLOD** selection + patch morph (crack-free without skirts). Escape hatch: keep geomipmap behind a flag until CDLOD matches draw cost and crack-free checks.
3. Materials: 4–8 metal-rough layers; **height-blend** + slope-blend; **RNM** (Whiteout fallback); triplanar only when steep; bindless arrays when heap path exists.
4. No full VT / Nanite / Cesium globe in v1.
5. Displacement = heightfield mesh LOD + **POM** near camera. No world tessellation.
6. Physics owns a **coarser** heightfield; sync on cook / sculpt commit (never silent drift vs render).
7. Same BRDF as props. No “terrain-only” lighting model.
8. No C++ exceptions; failures `bool` + `DE_LOG_*`.

## Non-goals (v1)

- Nanite / cluster mesh terrain, SVT / MegaTexture, RVT bake (P7+).
- Planetary Cesium quantized-mesh as the gameplay surface.
- Overhangs / caves as heightfield (authored meshes later).
- Mercator / EPSG:3857 as gameplay meters.
- Invented FPS budgets in the RFC.
- Replacing water or foliage systems in the same PR as CDLOD.

## Default stack (frozen)

| Layer | Pick | Until |
| --- | --- | --- |
| Geo LOD | **CDLOD** (quadtree patches + morph) | Geomipmap flag retired after parity |
| Materials | **4–8** metal-rough splat, height+slope blend, RNM, gated triplanar | — |
| Textures | Chunk weights + BC7/BC5 layers, bindless | No VT |
| Displacement | HF mesh LOD + POM close | No tessellation |
| Data | DEM / proc / sculpt → one `TerrainTile` | — |
| Physics | Coarser HF, sync on cook/sculpt | — |
| Space | Local **ENU** meters | Never 3857 gameplay |

### Height-blend (core)

\[
h_i = H_i + k\,w_i,\quad
\hat{w}_i=\mathrm{sat}\!\big((h_i-(h_{\max}-t))/t\big),\quad
w_i'=\hat{w}_i/\sum\hat{w}_j
\]

Normals: **never** lerp RGB. RNM desktop default; Whiteout cheap; UDN last resort.

### Unified tile (sketch)

```text
TerrainTile:
  originENU      float3
  cellSizeMeters float
  height         R16F (or R32F authoring → R16F cook)
  weights        RGBA8 × 1..2  (4 or 8 layers)
  minH, maxH     float
  materialSetId  uint
  holes?         bit mask / optional R8
```

## Build order

| Phase | Work | Exit |
| --- | --- | --- |
| **P0** | This audit + DESIGN (this PR) | Doc merged |
| **P1** | Spike: one tile (R16F height + 4-layer weights) drawn with **existing** PBR lighting; VS can still use a CPU grid | Visual + unit sample tests |
| **P2** | CDLOD select + patch morph; A/B vs geomipmap cracks / draw calls | Flag to pick path; crack-free checklist |
| **P3** | Metal-rough splat + height-blend + RNM; extend `kMaxTerrainLayers` to 8 behind materialSet | Matches props BRDF |
| **P4** | Stream residency (tile LRU); keep one-tile path working | — |
| **P5** | Coarser physics HF + sync API | Render vs phys height tests |
| **P6** | Editor sculpt/paint dirty → recook min/max → phys sync | — |
| **P7** | DEM / procedural cook into `TerrainTile` | Licence notes in cook tool |
| **P8** | POM / detail | — |
| **P9** | GPU-driven / mesh shaders (optional) | — |
| **P10** | RVT-class bake (optional) | Only if splat ALU hurts |

## What must not break

- `HeightMap::heightAtWorld` / `tryHeightAtWorld` / `raycast` signatures used by Sandbox / gameplay.
- Geomipmap path remains buildable until CDLOD flag is default and a listening/visual pass says cracks are gone.
- `TerrainMaterial` bind layout changes are versioned; GBuffer terrain pass must keep compiling.
- Water grid index helper stays valid or gets an explicit dual-path update in the same PR that changes patch topology.
- World XZ meters stay linear ENU; do not invent a second terrain space.

## What will change (and when)

| Change | When | Effect |
| --- | --- | --- |
| VB heights → VS texture fetch | P1–P2 | Meshes get thinner; morph needs both LOD heights |
| Edge weld → morph factor | P2 | Crack policy changes; weld code can retire behind flag |
| Albedo-only layers → metal-rough pack | P3 | Look changes; authoring must supply masks |
| 4 → up to 8 layers | P3 | More weights texture / bindless indices |
| Separate physics HF | P5 | Must fail tests if cook forgets sync |

## Tradeoffs

- **CDLOD vs keep geomipmap.** Tip already invested in weld/ΔLOD≤1. CDLOD is the Plan A default (morph, no skirts, research-backed). Cost: rewrite selection + VS morph. Benefit: fewer unique meshes, better streaming fit, matches ResearchBot. Keep geomipmap as fallback until parity.
- **CPU mesh vs VS height sample.** CPU rebuild is simple and works offline. VS sample unlocks morph and cheaper LOD switches. P1 may still CPU-build one tile; P2 should move height to VS.
- **4 albedo layers vs 8 metal-rough.** Tip’s 4-channel splat is a good scaffold. Height-blend needs per-layer height in the mask. Do not silently drop layers above 4 — materialSet declares count.
- **No VT.** Correct for indie v1. Unique dirt later = RVT/AVT escape, not P1.
- **Same HF for physics.** Fine for prototype; wrong for large worlds (cost + edit sync). P5 makes the split explicit.
- **Cesium / Nanite.** Escape for globe / hero mesh regions — not the outdoor default.

## Acceptance

**Docs (this PR)**

- [ ] Tip audit table matches `Terrain/` on the named baseline SHA.
- [ ] Stack matches ResearchBot defaults (CDLOD, 4–8 splat, no VT, ENU, phys sync).
- [ ] Explode list present on cheatsheet.

**P1 spike (follow-up)**

- [ ] One `TerrainTile` renders with deferred PBR (metal-rough or documented interim).
- [ ] Height sample unit test: world XZ → Y within epsilon of CPU reference.
- [ ] Four-layer weights renormalize in shader.

**P2 CDLOD**

- [ ] Adjacent patches crack-free under camera motion (checklist + screenshots).
- [ ] Morph factor continuous across LOD boundary.
- [ ] Geomipmap still selectable via flag until default flips.

**P5 physics**

- [ ] Coarse HF Y vs render HF Y within agreed epsilon on cook.
- [ ] Sculpt commit updates both or fails loud.

## Explode list

- Normal RGB lerp / Overlay “blend”
- Render height ≠ physics height with no sync
- Mercator meters as gameplay space
- Cracks (morph or weld missing)
- VT / Nanite / Cesium before CDLOD + splat
- Tessellation as world LOD
- >8 layers silently dropped
- DSM used as collision DEM
- Ignoring DEM / ion licences in cook tools
- Terrain-only BRDF that diverges from props
- Invented fps numbers in the RFC

## Size

| PR | Size |
| --- | --- |
| This DESIGN | Docs only |
| P1 one-tile spike | Medium |
| P2 CDLOD | Medium-large (the risky geo PR) |
| P3 PBR splat | Medium |
| P4–P5 stream + physics | Medium-large |
| P6–P8 editor / cook / POM | Large, split |

Full Plan A is many PRs. It is not a weekend rewrite of `TerrainWorld`.
