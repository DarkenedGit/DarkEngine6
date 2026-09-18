# Terrain Plan A cheatsheet

Companion to [DESIGN-terrain-system.md](./DESIGN-terrain-system.md). Tip audit `239366ac` (`Terrain/`).

**Rule:** ENU meters. Same metal-rough BRDF as props. Morph or weld — no cracks. Physics HF syncs on cook. **No VT / Nanite / Cesium in v1.**

## Stack

| Layer | Pick |
| --- | --- |
| Geo LOD | **CDLOD** (morph, no skirts). Tip today: geomipmap + edge weld — keep until CDLOD parity |
| Materials | 4–8 metal-rough; height-blend + slope; RNM; gated triplanar |
| Textures | Chunk weights + BC7/BC5; bindless. **No VT** |
| Displacement | HF mesh LOD + POM close. No world tessellation |
| Data | DEM / proc / sculpt → one `TerrainTile` |
| Physics | Coarser HF; sync on cook/sculpt |

## Tip gap (one line)

`HeightMap` + chunked geomipmap + 4 albedo splat → need CDLOD morph, metal-rough height-blend, tiled ENU, phys sync.

## Tile

```text
height R16F | weights RGBA8×1..2 | minH/maxH | materialSetId | originENU + cellSizeM
```

## Height-blend

\(h_i=H_i+k w_i\), keep winners within transition \(t\), renormalize. Never lerp normal RGB.

## Build order

P0 DESIGN → P1 one tile + PBR → P2 CDLOD → P3 splat → P4 stream → P5 physics → P6 editor → P7 cook → P8 POM → P9 GPU → P10 RVT optional

## Explode list

- Normal RGB lerp / render≠phys height / Mercator meters
- Cracks / VT·Nanite before CDLOD / tessellation as world LOD
- Silent >8 layer drop / DSM as collision / terrain≠prop BRDF

## Size

DESIGN: docs. P1: medium. P2 CDLOD: medium-large (risk). Rest: split.
