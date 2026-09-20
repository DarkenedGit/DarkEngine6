# Terrain look-track cheatsheet

Companion to this RFC (look track). Replaces the Plan A CDLOD one-pager. Streaming follow-up is [`DESIGN-terrain-streaming.md`](./DESIGN-terrain-streaming.md).

**Rule:** ENU meters. Same metal-rough BRDF as props (`PbrLighting.hlsli` + deferred IBL). Geomipmap weld, ΔLOD≤1 — **no CDLOD in this track**. Physics queries = the same `HeightMap`. **No VT / Nanite / Cesium / tessellation / bindless / BC7.**

### Stack (this RFC)

| Layer | Pick |
| --- | --- |
| Geo LOD | **Keep geomipmap + edge weld** (tip). CDLOD = follow-up RFC |
| Materials | **4** metal-rough; height-blend (`k<=0` = splat); **Whiteout** in +X TBN; gated triplanar **albedo+ORM only** |
| Textures | Packed heap **14 SRVs**; height in **ORM.a**; shadow last. **No VT / arrays / BC** |
| Displacement | CPU HF mesh LOD. **No POM / tessellation** |
| Data | FBM / U16 / sculpt `HeightMap` + RGBA8 splat |
| Physics | Same HF queries (no coarse cook) |
| Editor | World-level, in this RFC |

### Tip gap (one line)

`HeightMap` + geomipmap + 4 albedo splat → **G-buffer still writes rough=1/metal=0/ao=1/geom N**. Fix the writer; do not start with CDLOD.

### Heap (`bindLayout: 1`)

```text
t0,t3,t6,t9   albedo sRGB
t1,t4,t7,t10  normal Linear
t2,t5,t8,t11  ORM Linear (R AO, G rough, B metal, A height)
t12           splat RGBA8
t13           shadow (copyShadow)
G-buffer table 13 / forward 14
G-buffer CB 63 floats (k, t, slope, 4 scalar uint tints) — RS 64/64
  HLSL arrays = 16 bytes/element — do not use uint[4]; C++ uint32_t[4] would disagree
Forward CB 60 — cannot grow; albedo lerp only; SHADOW_T t13; tiling unconverted
G-buffer layerTiling[] = repeats/m (CPU worldScale); planar+triplanar use worldPos * s
```

### Height-blend

If `k<=0`: `outW` = renormalized splat, ignore ORM.a (checkers). Else \(h_i=H_i+k w_i\), keep winners within \(t\), renormalize. Dummy A=0.5 for authored `k=0.5`. Never lerp normal RGB. Whiteout in `tW=cross(nG,+Z)`. Triplanar albedo+ORM only.

### G-buffer

RT0 albedo, emis 0. RT1 EncodeOct(Whiteout N), blended rough/metal. MRT3 blended AO. Then deferred+IBL.

### Editor / save

One terrain per 3D scene. `createTerrainPipeline` (not `createWorldEnvironment`). `EditorApp` holds World+Material+splat. `syncTerrainLod`+`waitForGpu`. Skip 40 m plane; `drawGBuffer`/`draw`/`drawDepth`; bounds from `terrain.bounds()`. `groundHitFromRay` = `HeightMap::raycast`. JSON `terrain{}` + sidecars. Version 2. 2D ignores.

### Build order

PR1 CPU helpers → PR2 heap+G-buffer (look) → PR3 content → PR4 Editor → PR5 docs.

### Explode list

- Normal RGB lerp / Overlay “blend”
- `k=0` implemented as the raw formula (4-way mix)
- YZ/XY normal maps through the XZ heightfield TBN
- `cross(+Z, nG)` TBN (−X on flats)
- `worldPos * layerTiling` without CPU repeats/m conversion
- CDLOD / bindless / BC7 as this track
- Growing `TerrainFrameConstants` (63/64)
- `SHADOW_T` left at t5
- HLSL `uint tint[4]` (16 bytes **per element**, ~76 floats; C++ array would disagree)
- Reuse mesh white ORM (metal=1)
- Silent >4 layer drop
- Terrain-only BRDF / skipping IBL
- Editor as “later P6” / `createWorldEnvironment=true` (pulls Water/Sky)
- Editor ±22 shadow box with a 256 m HF
- Sculpt without `waitForGpu`
- `AssetRef` on `SplatMap.h`
- Exceptions on missing files (`try`/`catch`/`throw` in the PR diff — review grep, not a gtest)
- Mercator meters / VT / tessellation as world LOD

### Size

DESIGN: docs. PR1: small-medium. PR2: medium (look). PR3: content. PR4: medium-large (Editor+I/O). PR5: docs.
