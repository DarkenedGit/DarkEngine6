# DarkEngine6 Foliage, Ground Cover, and Trails Plan

Build-ready, five-phase plan for baked instanced foliage, procedural grass, and walking paths. Based on the tree at `/workspace/de6-35cf523-b` (tip `35cf523`). No code is changed by this document; it is an implementation reference.

Each phase is independently shippable once its stated dependencies have landed. Later phases assume earlier ones are in, but none require a rewrite of the D3D12 foliage or grass pipelines.

---

## Current baseline (what is already in place)

| Piece | Where | Notes |
|---|---|---|
| Baked foliage spawn | `Terrain/FoliageSpawn.cpp` `spawnFoliage()`, `Terrain/FoliageSpawn.h` | 1 m lattice (`floor(widthM)` × `floor(depthM)`, 112–113, 272–295). Bernoulli accept `hash21(ix, iz, seed ^ salt) < p` (176–188). Trees/flowers/rocks/tufts jitter ±0.45 m / flowers ±0.12 m (`kTreeRockJitter` / `kFlowerJitter`, 28–29). Flowers: 2 slots (`kFlowerSlots`, offsets ±0.25 m). **No `normal.y` reject.** Rocks only use `normalAtWorld` to set `pitch` / `tiltYaw` (245–252). Trees/flowers/tufts stay upright. |
| Record + density | `Terrain/FoliageFile.h` | `FoliageKind`: Tree, Flower, Rock, Grass (18–25). `FoliageDensity` (27–40): per-layer rates, one model path per kind, `seed`. `FoliageRecord` is exactly 32 bytes: `x,y,z,yaw,scale,pitch,tiltYaw,kind,pad[3]` (42–56). Caps: `kMaxFoliageInstances = 12582912`, `kMaxFoliagePerFile = 1048575`, `kMaxFoliageDraw = 65536`. File magic `kFoliageMagic = 0x4C464544` (`DEFL`), **`kFoliageVersion = 1`**. `saveFoliageTile` **zeroes `pad[0..2]`** (`FoliageFile.cpp` 79–84). Load rejects any other version (160–164). |
| Tile sidecars | `Terrain/TerrainTileFile.h` 17–36 | `t_%02d_%02d.height.bin`, `.splat.png`, `.foliage.bin`. No cover file today. `TerrainGrid` loads foliage in `loadFineTile` (`TerrainGrid.cpp` 719–734) and exposes `residentFoliage()` (`TerrainGrid.h` 139). Editor working instances live in `EditorApp::m_foliage`, not on the grid (`EditorApp.h` 522–523). |
| Splat | `Terrain/SplatMap.h` 20–32, `SplatMap.cpp` 99–110 | R=dirt, G=grass, B=rock, A=snow. `generateFromHeight` paints rock from `slope = 1 - n.y` at `rockSlope ± blend` (defaults 0.45 ± 0.08, i.e. `n.y` ≈ 0.63–0.47, **~51–62°**), then multiplies dirt/grass by `(1 - rock)`. World Engine / HurricaneRidge uses a **painted** splat (`WorldEngineMap.cpp`, `content/terrain/HurricaneRidge/Splatmap 1025.png`), so grass/dirt can remain on cliffs. `paintDisk()` / `paintTexel()` already exist (`SplatMap.h` 50–51, `SplatMap.cpp` 199–240). |
| Scene JSON | `Scene/SceneTypes.h` `FoliageSceneDesc` 370–386, `GrassSceneDesc` 388–404, `TerrainSceneDesc` 412–433 | Scene `version` stays **2** (`SceneTypes.h` 545). Densities and the four model paths live under `terrain.foliage`; procedural grass under `terrain.grass`. Load/save in `Scene/SceneFile.cpp` 649–662 and 1028–1048. Missing foliage keys use struct defaults. `UnitTests/Scene/SceneFileTests.cpp` `TerrainFoliage_RoundTrip` (946+) covers the current keys. |
| Default densities | `FoliageDensity` / `level.json` 524–549 | Trees 0.002 dirt / 0.003 grass per m², flowers 0.006 / 0.008, rocks 0.004, tufts **`grassPerM2: 0.25`** in `level.json` (struct default is 0.0). Procedural `grass.enabled: true` in the same file. **Both grass systems run at once.** |
| Prototypes | `Render/FoliagePrototypes.h/.cpp` | One shared model per kind. `makeFoliageShowcaseParts` (149–218) drops `LOD1+` and keeps the cluster within **0.25 m** of the origin-nearest part. `grassTuft` (48–93) keeps a **single** tuft from a lineup. Fallback primitives: cylinder+cone tree, stem+blossom, sphere rock, crossed quads (220–346). Default glTFs (`models/BirchTree/scene.gltf`, Dandelion, RockMoss set, grass_medium) are referenced but **not in this checkout**. |
| Render / physics | `Render/FoliagePipeline.*`, `Physics/FoliageCollision.h` | CPU gather **96 m**, rebuild after **16 m** (`kGatherRadiusM` / `kGatherMoveM`, `FoliagePipeline.cpp` 43–44). GPU cull: **4 kinds × 8 parts**, 65536 instances (`FoliagePipeline.h` 71–77; `FoliageCull.hlsl` `kKindCount=4`, `kMaxInstances=65536`). Grass kind 3 is skipped in shadow views (`FoliageCull.hlsl` 140). World matrix: `Scale * RotY(yaw) * RotX(pitch) * RotY(tiltYaw) * Translate` (293–296). Flowers/tufts get ×10 (`kSmallFoliageScale`). **No runtime mesh LODs.** Wind in `TreeWind.hlsli`; rocks default `influence = 0` (`WindField.cpp` 75–76). Physics: trees+rocks, 64 m, **2048** boxes (`kMaxLiveFoliageBodies`, `kFoliageBodyRadiusM`). |
| Procedural grass | `Terrain/GrassField.cpp` `acceptCandidate()` 686–752, `GrassTypes.h` | 8 m tiles, 64×64 jittered candidates. Already rejects `normal.y < kNormalYMin` (**0.80, ≈36.9°**, line 43), `weights[1] < 0.35`, rock > 0.55, snow > 0.45, and `surfaceY < waterLevel`. Keep-rank thinning (`kBaseKeep` 32/16/8/5 × `densityScale`). LOD distances in `content/terrain/lod.json` (16 / 40 / 80 / 256 m). `setParams` dirties tiles when `densityScale` or `seed` change (278–293); `update` rebuilds dirty tiles via `tryRebuild` (906, 1077–1136). **Does not see trees or paths.** Blade variation: yaw, `heightMul` 0.70–1.30, `flexMul` 0.80–1.20, `phase` (747–750). One blade mesh per LOD (`GrassMesh.cpp`). |
| Editor UX | `Editor/EditorTerrain.cpp` | `startFoliageSpawn()` (1393–1504) runs automatically from `loadWorldEngineTerrain()` (1774). Sea level for imports is the height origin (1421–1422). Water volumes come from the lake sheet plus placed `SceneObjectType::Water` footprints (1423–1449). The Terrain panel (1779–1882) only **reports** tree/tuft counts (1839–1857) and has procedural-grass facing sliders. **There is no Place / density / slope UI and no re-place button** except by reloading World Engine terrain. |
| Slope test that documents the bug | `UnitTests/Terrain/FoliageSpawnTests.cpp` `TreesStayUprightRocksFollowNormal` (468–526) | A 1:1 ramp (≈45°, `n.y ≈ 0.707`) with a pure-grass splat still plants **9 upright trees**. The same ramp with a pure-rock splat plants rocks fully aligned to the normal, uncapped. |
| Game pathing (not for trails) | `AI/Walkability.h`, `AI/Pathfinder.h` | Agent nav on a bake of the **coarse** heightfield (`<= kMaxHeightMapSize` 1025, not a 4097 working map). Default `maxSlopeDeg = 45`. `PathRequest::expansionCap = 4096`. This is the pawn pathfinder, not a world-authoring tool. |
| Clustering / cover / species | — | No FBM/Worley patch mask, no min spacing, no understory satellites, no trunk/canopy bare patch, no biome/moisture map, no trail or cover sidecar, no variant/tint on `FoliageRecord`. `hash21(int x, int z, uint32_t seed)` already lives in `Terrain/TerrainGen.h` 24. |

**Why trees and rocks sit on steep slopes today:** `consider()` never rejects on slope. Splat rock weight only starts suppressing dirt/grass around 51–62°, and painted World Engine splats can leave grass on cliffs. Rocks are then *tilted to match* any slope they land on. **Why plants look samey and evenly sprinkled:** one mesh per kind (showcase sets are collapsed to the origin cluster), tree scale only 0.85–1.15, independent 1 m Bernoulli with no spacing or patch noise. **Why grass grows under trunks:** neither `spawnFoliage` nor `GrassField::acceptCandidate` consults tree positions.

**Constraints to keep:** C++23, no exceptions, Windows / D3D12, MIT. New `Terrain/*.cpp` files are picked up automatically (`cmake/DarkEngineTargets.cmake` `DE_ENGINE_REST_FOLDERS` includes `Terrain`, line 134). New tests under `UnitTests/Terrain/` are picked up by `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` (`UnitTests/CMakeLists.txt` 34–38). Do not open a GitHub PR until Travis asks.

Suggested default angles (also stored as `minNormalY` so the test can use the same constants):

| Category | `minNormalY` | Max slope from vertical | Tilt |
|---|---|---|---|
| Trees | 0.87 | ≈29.5° | Optional lean, cap `treeMaxLeanDeg` (default 7, 0 = stay upright) |
| Flowers / grass tufts | 0.82 | ≈34.9° | Upright (pitch 0) |
| Procedural grass | keep `kNormalYMin = 0.80` (≈36.9°) | already filtered | Root already lifted along the normal (`GrassField.cpp` 744–746) |
| Rocks | 0.40 | ≈66.4° | Align, then **clamp pitch** to `rockMaxPitchDeg` (default 30) |

A 45° grass ramp (`n.y = 1/√2 ≈ 0.707`) must plant **zero trees**.

---

## Phase 1 — Per-kind slope rules

### Goal and payoff

Stop planting trees on cliffs and stop glueing rocks to near-vertical faces, without waiting on new assets or a new file format. Existing `.foliage.bin` files are unchanged until the next spawn pass; the next Place / World Engine load uses the new rules. Visible immediately in Editor and Sandbox after a re-place.

### Files to touch

- `Terrain/FoliageFile.h` — add slope fields to `FoliageDensity` (defaults in the table above).
- `Terrain/FoliageSpawn.cpp` — in `consider()` (136–264), after jitter and the water test, sample `height->normalAtWorld(x, z)` and reject / clamp tilt per kind. Apply tree lean (if any) and rock pitch clamp when writing the record (currently only rocks write pitch, 245–252).
- `Scene/SceneTypes.h` `FoliageSceneDesc`, `Scene/SceneFile.cpp` save (649–662) and load (1028–1048) — persist the new fields under `terrain.foliage`. Missing keys use the new defaults (old scenes pick up the filter on the next spawn; this is intended).
- `Editor/EditorTerrain.cpp` `drawTerrainPanel()` (1779–1882) — add a **Foliage** section: slope sliders, optional lean, a **Place / Replace foliage** button that calls `startFoliageSpawn()`, and keep the existing count readout (1839–1857). Copy fields through the existing `m_foliageDensity` save/load path (832–844, 1092–1102, 1200–1207).
- `UnitTests/Terrain/FoliageSpawnTests.cpp` — rewrite / split `TreesStayUprightRocksFollowNormal` (468–526); add the 45° zero-tree test. `UnitTests/Scene/SceneFileTests.cpp` `TerrainFoliage_RoundTrip` (946+) must round-trip the new keys and still load a JSON that omits them.

### Implementation details and gotchas

1. **Reject after jitter, on the instance position, not the cell centre.** Sea-level already uses the cell centre (282–283) then jitter; slope must use the final `(x, z)` just like water (151–154). If `n.y` is below the kind's `minNormalY`, return without accepting.

2. **Tree lean is a cap, not full alignment.** If `treeMaxLeanDeg <= 0`, keep today's `pitch = 0`, `tiltYaw = 0`. Otherwise `pitch = min(acos(clamp(n.y, -1, 1)), treeMaxLeanDeg * DegToRad)` and `tiltYaw = atan2(n.x, n.z)` only when pitch is nonzero (same `n.y > 0.999` short-circuit rocks already use at 248). Do not lean flowers or tufts in this phase.

3. **Clamp rock pitch.** Keep current alignment, then `rec.pitch = min(rec.pitch, rockMaxPitchDeg * DegToRad)`. A cliff (`n.y` near 0) still gets a rock only if `n.y >= rockMinNormalY` (0.40 allows ~66°), and it will sit at 30°, not 90°.

4. **Do not piggy-back on splat rock weight.** `generateFromHeight` only fully clears dirt/grass around 51–62°, and HurricaneRidge is a painted splat. The new test must use a *pure grass* splat on a 45° ramp, same setup as `TreesStayUprightRocksFollowNormal`.

5. **Clamp on load, same style as existing densities** (`SceneFile.cpp` 1033–1038). Suggested ranges: `minNormalY` in `[0, 1]`, `treeMaxLeanDeg` in `[0, 15]`, `rockMaxPitchDeg` in `[0, 89]`.

6. **Add a Place button.** Changing sliders does nothing until `startFoliageSpawn()` runs, and today that only happens on World Engine load (1774). Without a button, Editor verification is painful. Re-place keeps the existing background thread + progress/cancel path (1393–1524). Mark `m_foliageStale` when sliders change and clear it on a successful spawn (the stale flag already exists on `FoliageSceneDesc`).

7. **Existing bins stay valid.** Slope is spawn-time only. `kFoliageVersion` stays 1. Runtime Sandbox/Editor just draw whatever records are already on disk.

### Unit tests

In `FoliageSpawnTests.cpp`, reuse `makeMap` / `baseIn` / `expectPadAndYaw` (14–68):

- **`TreesRejectedOn45DegGrassRamp`** — same 4×4, 1 m, `setHeight(x, z, float(x))` ramp as 468–526, pure grass splat, `grassTreesPerM2 = 1`, `seaLevel = -1`. Expect `out.records.empty()` (or at least zero `FoliageKind::Tree`).
- **`GentleSlopeTreesOptionalLean`** — a shallow ramp whose `n.y` is above 0.87 (e.g. rise 0.25 m per metre, ≈14°). With `treeMaxLeanDeg = 0`, pitch stays 0. With `treeMaxLeanDeg = 7`, pitch is in `(0, 7°]` and `tiltYaw` matches `atan2(n.x, n.z)`.
- **`RockPitchIsClamped`** — the existing 1:1 rock ramp, `rockPerM2 = 1`, `rockMaxPitchDeg = 30`. `acos(n.y)` on that ramp is 45°; expect `pitch` ≈ 30° ± 1e-5, not 45°. A second case with `rockMinNormalY = 0.80` on the same ramp plants zero rocks.
- **`FlowersAndTuftsFollowTheirThreshold`** — 45° grass ramp plants zero flowers and zero tufts at the 0.82 default; a flat grass map still plants them (`GrassFollowsGrassLayer` at 607 and `FlowerSlotsFollowRate` at 359 must keep passing).
- Leave `PureSnowPlacesNothing`, `SeaLevelUsesWorldY`, `TreesAndGrassSkipWaterVolumes`, `SameSeedRepeats`, and the lattice test alone. Flat-map counts should be unchanged.
- `SceneFileTests.cpp` `TerrainFoliage_RoundTrip`: write and read the six new floats. A second case with no slope keys in the JSON loads the struct defaults (0.87 / 7 / 0.82 / 0.82 / 0.40 / 30).

### Verification

- **Editor:** Load HurricaneRidge (or any steep World Engine map). Before this phase, trees sit on steep grass. After Place, trees stop on faces steeper than ~30°, flowers/tufts a bit looser, rocks disappear from near-vertical walls and no longer lie flush with cliffs. Toggle lean 0 vs 7° on a gentle hillside and re-place; trunks should be obviously different. Cancel mid-spawn still keeps the previous set.
- **Sandbox:** Open a scene that was re-saved after Place. Existing `.foliage.bin` from before the change still load (no version bump). After the Editor re-places and you save, Sandbox shows the filtered set. Procedural grass is unchanged (still uses `kNormalYMin = 0.80`).
- Log line from `spawnFoliage` (340) still reports kept / accepted / trees / flowers / rocks / grass; tree counts on steep maps should drop.

---

## Phase 2 — Tree spacing and ground-cover mask

### Goal and payoff

Trees no longer sit on top of each other. Grass and flowers thin out and get shorter under canopies, with a bare disk at the trunk, so both the baked tuft layer **and** procedural grass agree. This phase introduces the cover sidecar that Phase 3 (trails) will also write.

Note the overlap in `content/scenes/level.json` 524–549: `foliage.grassPerM2 = 0.25` and `grass.enabled = true`. If only one path reads the mask, trunks will still sit in a carpet of the other.

### Files to touch

- **New:** `Terrain/FoliageCover.h` + `Terrain/FoliageCover.cpp` — mask type, sample, stamp, load/save. Auto-globbed into DarkEngine.
- `Terrain/TerrainTileFile.h` — add `tileCoverFileName` / `tileCoverPath` next to `tileFoliageFileName` (31–36), e.g. `t_%02d_%02d.cover.bin`.
- `Terrain/TerrainGrid.h/.cpp` — `editableWorkingCover()` (Editor) and `residentCover(tx, tz)` (runtime), loaded and evicted beside foliage (`TerrainGrid.cpp` 719–734, 788).
- `Terrain/FoliageSpawn.h/.cpp` — min tree spacing; after accepted trees are known, stamp trunk + canopy into the mask; flowers and `FoliageKind::Grass` consult the mask in `consider()`.
- `Terrain/GrassField.cpp` `acceptCandidate()` (686–752) and `splatReady()` (676–684) — sample the same mask; scale `heightMul` and/or reject; dirty tiles when the working cover stamp changes (same path as `setParams`, 278–293).
- `Editor/EditorTerrain.cpp` — write cover tiles next to foliage bins (see save around 1028–1041); rebuild cover at the end of `startFoliageSpawn`; after a successful poll, dirty / reset grass so Editor Play sees bare patches without a restart.
- `Sandbox` load path already goes through `TerrainGrid`; once the grid loads `.cover.bin`, Sandbox grass picks it up. Confirm the Play/Editor grass update call (`EditorTerrain.cpp` 745, `SandboxApp.cpp` 2622) does not need a new argument if the mask hangs on the grid.
- Tests: `UnitTests/Terrain/FoliageSpawnTests.cpp`, new `UnitTests/Terrain/FoliageCoverTests.cpp`, `UnitTests/Terrain/GrassFieldTests.cpp`, `UnitTests/Terrain/FoliageFileTests.cpp` (filename, like `FileName` at 380–384).

### Implementation details and gotchas

1. **Mask format.** Two uint8 channels, bilinear sample: `coverage` (0 = bare / reject, 255 = full density) and `heightScale` (0–255 → 0–1, multiplied into grass `heightMul` and used as a soft flower/tuft probability). 1 m cells are enough (procedural grass candidates are 0.125 m; bilinear covers it). Suggested header: magic `DECV` (`0x56434544`), version 1, tileX/Z, width, height, cell size, origin XZ — then tightly packed `RG` bytes. Enforce a byte cap in the same spirit as `kMaxFoliageSidecarBytes` (32 MiB). A 512 m tile at 1 m is 512×512×2 ≈ 512 KiB.

2. **Spawn order inside `spawnFoliage` (and a post-pass if needed).** Today's two-pass scan (count, then keep; 316–333) does not know neighbors. Add a third, deterministic pass, or collect accepted trees first:
   1. Accept trees (and rocks) with slope rules + **spatial-hash spacing**.
   2. Clear the mask to 255/255, then stamp every kept tree.
   3. Accept flowers and tufts against the stamped mask.
   Keep the existing seed and salts so flat, uncrowded maps stay stable where spacing does not reject.

3. **Spatial hash for trees.** Cell size = `treeSpacingM` (default 3 m, clamp ~2–4 m, expose on `FoliageDensity`). On accept, query neighboring hash buckets; if another kept tree is within `spacing * 0.5 * (scale_a + scale_b)` (or a simpler fixed radius × max scale), reject. Rocks can use a tighter default (e.g. 0.8–1.2 m) so boulders do not occupy the same point; leave rock spacing optional in this phase if you want a smaller diff. Hash must be deterministic (scan order is already row-major).

4. **Trunk vs canopy stamp.** For each tree:
   - Trunk core: radius ≈ `0.5 m * rec.scale` (procedural trunk is a 0.5 m radius cylinder before instance scale, `FoliagePrototypes.cpp` 249; glTF overrides can use `0.25 * modelBounds.XZ` if the model is ready). Coverage 0, heightScale 0.
   - Canopy drip line: radius ≈ `0.45 * max(model AABB x, z) * rec.scale`, falling back to ~2 m × scale if no model is passed in (unit tests do not load glTFs). Soft falloff from the trunk out to the drip line: coverage rises 0 → 255, heightScale 0.35 → 1.0 so grass gets shorter and thinner, not only deleted.
   `FoliageSpawnIn` can take an optional `treeCanopyRadiusM` (Editor/Sandbox fill it from `FoliagePrototypes::model(Tree)->bounds()`, already used for leaf crowns in `Play/EnvironmentTick.cpp` 31–38). Tests pass an explicit radius.

5. **Both grass paths must sample it.**
   - `consider()` for Flower and Grass: treat `coverage/255` as a multiplier on `p` (or reject if coverage == 0). Soft edge still allows sparse flowers in the drip line.
   - `GrassField::acceptCandidate`: if coverage == 0, return false; else `heightMul *= heightScale`. When the Editor writes a new working cover, mark every resident grass tile `dirty` so `tryRebuild` resamples (do not wait for a density/seed change).
   - If cover is missing (old scenes, no sidecar), treat as 255/255 so behavior stays backward compatible.

6. **Editor vs runtime ownership.** Mirror foliage: Editor working cover is one map over the working heightfield; `save` writes per-tile `.cover.bin` into `tileDir`. Runtime `loadFineTile` loads it. Do not put the mask in the 32-byte `FoliageRecord` and do not bump `kFoliageVersion` in this phase.

7. **Do not stamp paths yet.** Leave the mask API open (`stampDisk`, `stampSoftAnulus`, `minBlend`) so Phase 3 can rasterize trails into the same buffer without a format change.

### Unit tests

- **`TreesRespectMinSpacing`** — flat pure-grass map, `grassTreesPerM2 = 1` (every cell would accept today). With `treeSpacingM = 3`, no two trees are closer than ~3 m (allow a small epsilon for jitter). Same seed is stable (`SameSeedRepeats` style, 397).
- **`FlowersAndTuftsAvoidTrunkCore`** — plant a known tree at the origin (or run spawn with density such that one tree is kept), stamp a 1 m core + 3 m canopy, then flowers/tufts at 1.0 / m². No flower/tuft with distance < core radius; density in the drip line is lower than outside.
- **`CoverRoundTrip`** — save/load a 16×16 mask, bitwise identical. Bad magic / truncated file / wrong tile coords fail and clear the output, same shape as `FoliageFileTests.cpp` `RejectsBadMagicVersionAndTile` (254).
- **`GrassFieldReadsCover`** — `makeGrid` a flat grass tile (`GrassFieldTests.cpp` 38), stamp a 2 m bare disk at the centre, `update(...)`, then `tileBlades` / `bladeTotal` has no blade with `xz` inside the core, and blades in the falloff have `heightMul` below the unmasked 0.70–1.30 range after the scale is applied. A grid with no cover still matches today's `SpawnGates` (532).
- Keep the steep-ramp grass test (566–571) passing; cover is a second gate, not a replacement for `kNormalYMin`.

### Verification

- **Editor:** Place foliage on a forested hillside. Trees have visible gaps. Under each trunk, both the glTF tufts and the procedural blades are gone; around the drip line, remaining blades are shorter. Reload the scene: bare patches return with the tiles (no need to re-place). Delete / miss a `.cover.bin` and confirm grass returns to full (and a warn log similar to missing foliage at `TerrainGrid.cpp` 727).
- **Sandbox:** `level.json` (both grass systems on) shows the same bare disks in play mode. Walk under a tree: no blade pop-in at the trunk when LOD promotes (`tryPromote` must preserve rejected candidates, same as today's "coarser predicate already rejected" comment at 786–788 — if a core is stamped after a tile was built, the dirty/rebuild path has to run).
- Counts in the Terrain panel: tree count drops vs Phase 1 on dense grass; tuft count drops more than trees.

---

## Phase 3 — Walking paths and game trails

### Goal and payoff

Bare walking paths that look like animals and humans actually use the landscape: a packed dirt core, short-grass shoulders, and a noisy edge that breaks the "ploughed line" look. Trees and rocks stay off the path. Authors can draw splines in the Editor; the engine can also lay *game trails* between points of interest with least-cost pathing over the heightmap.

**Depends on Phase 2** (shared cover mask and tree/rock exclusion). Do not invent a second grass-suppression path.

### Files to touch

- **New:** `Terrain/TrailPath.h` + `Terrain/TrailPath.cpp` — spline types, rasterize-into-cover, offline least-cost pathing. Do **not** call `AI::Pathfinder::find` (too small an expansion cap, agent avoidance, coarse-HF-only bake).
- `Scene/SceneTypes.h` — `TrailPathDesc` / `TrailSceneDesc` on `TerrainSceneDesc` (optional, missing keys = no trails). Scene `version` stays 2.
- `Scene/SceneFile.cpp` — save/load `terrain.trails`. Extend `SceneFileTests.cpp` with `TerrainTrails_RoundTrip` next to `TerrainFoliage_RoundTrip`.
- `Terrain/FoliageCover.cpp` — `stampTrail()` (bare core, short-grass shoulder, hash-noisy edge).
- `Terrain/FoliageSpawn.cpp` — reject trees and rocks when cover at `(x, z)` is below a path-core threshold (after trails have been rasterized, before tree stamping; see order below).
- `Terrain/TerrainTileFile.h` — no new filename if cover is already `.cover.bin`.
- `Editor/EditorTerrain.cpp` and/or new `Editor/EditorTrails.cpp` (Editor glob will pick it up) — trail tool in the Terrain (or a Trails) panel, viewport overlay, Generate button.
- `Editor` save of cover tiles after rasterize; `m_foliageStale = true` when trails change so the author knows to re-place.
- Tests: `UnitTests/Terrain/TrailPathTests.cpp`, plus cover/spawn tests for path exclusion.

### Implementation details and gotchas

1. **Data model (scene JSON, not scene objects).** Do not add a `SceneObjectType` (the outliner is already reserved for authored entities; foliage itself was kept off it for that reason). Store under `terrain.trails`:

   ```json
   "trails": {
     "autoGenerate": false,
     "maxAutoPaths": 12,
     "humanWidth": 1.6,
     "animalWidth": 0.8,
     "paths": [
       {
         "kind": "human",
         "width": 1.6,
         "authored": true,
         "closed": false,
         "points": [ [x, z], [x, z] ]
       }
     ]
   }
   ```

   Y is sampled from `heightAtWorld` at rasterize time so sculpting can be re-stamped without rewriting points. `authored: false` marks generated game trails so they can be regenerated without erasing hand-drawn ones.

2. **Rasterization into the Phase 2 mask.** For each polyline / Catmull-Rom spline, in 0.5–1 m steps along the curve:
   - **Bare core** — radius `width * 0.5`. coverage = 0, heightScale = 0. Also the test trees/rocks use to exclude.
   - **Short-grass shoulder** — out to `width * 0.5 + 0.6–1.0 m` (human a bit wider than animal). coverage rises, heightScale ~0.25–0.55.
   - **Noisy edge** — modulate the outer radius by `hash21` on a quantized xz (`seed ^ trailSalt`) so the border is irregular, not a stadium capsule. Amplitude ~0.15–0.3 m for humans, a little more for animal tracks.
   Use min-blend so a path carved through a canopy stays bare (path wins over grass, tree trunk stamps can still force a disk if a tree was kept off to the side).

3. **Spawn order (replaces the Phase 2 list):**
   1. Rasterize authored paths, then (if enabled) auto trails, into a fresh mask (255/255 plus path stamps).
   2. Accept trees and rocks with slope + spacing + **cover-core reject**.
   3. Stamp trunk/canopy (Phase 2) on top (min-blend).
   4. Accept flowers/tufts against the combined mask.
   5. Dirty procedural grass tiles.

4. **Auto game trails — least-cost on the heightfield, not the pawn pathfinder.** Build a 2–4 m cost grid over the working / world heightmap (must support Editor working maps up to `kMaxWorkingHeightMapSize` 4097, `HeightMap.h` 19). Per cell:
   - Block (or huge cost) if under water (`FoliageWaterVolume` / lake `surfaceY` / `seaLevel`), same test as `coveredByWater` (`FoliageSpawn.cpp` 41–50).
   - Slope cost: something like `(1 - n.y)^2` so gentle hills are cheap and something near `treeMinNormalY` is expensive; block above a trail max slope (human ~25–30°, animal a bit looser).
   - Optional: prefer dirt splat, lightly penalize deep forest once a tree-density or Phase 4 patch mask exists; first ship can use current splat weights (`w[0]` dirt cheap, `w[2]` rock expensive).
   - 8-connected A* / Dijkstra with a cap sized for a 2–4 km map at 4 m (order 10^6 cells worst case; this is an Editor / spawn-time job, not a frame job). Smooth with string-pull or Chaikin; animal trails get extra lateral noise.

5. **Points of interest.** Generate a small set, then connect them with a capped number of paths (`maxAutoPaths`):
   - **Water edges:** cells that sit just above a water volume's `surfaceY` (lake sheet and placed water already gathered in `startFoliageSpawn`, 1423–1449).
   - **Clearings:** large enough blobs of high cover / low tree probability (after a cheap density estimate, or splat grass with few trees).
   - **Forest edges:** cells adjacent to high vs low tree-probability.
   Prefer water ↔ nearest clearing, clearing ↔ clearing along the edge rather than straight through a grove, and a few narrow animal tracks that branch to water. Deterministic given `FoliageDensity::seed`.

6. **Optional splat dirt stamp — explicit, not silent.** `SplatMap::paintDisk(..., layer=0, amount)` (dirt is layer 0, `SplatMap.h` 21) can bake a dirt core so footsteps and NPC move speed match the path (`EditorTerrain.cpp` 1838 already documents rock/snow at 0.75× and per-surface footsteps). This **mutates the splat**. Gate it behind a checkbox / "Stamp trails into splat" button, keep an undo or "don't save until I say so", and never do it implicitly on every regen.

7. **Editor tooling.**
   - New "Trails" collapsing header: list of paths, kind, width, authored vs generated, visibility toggle.
   - In the 3D view (when the tool is active): click to add a control point on the terrain, drag to move, Del to remove, Enter to finish a path. Draw the spline and a width ribbon (LinePipeline / debug overlay is enough; no new GPU path).
   - Buttons: **Generate game trails** (leaves authored paths alone), **Clear generated**, **Re-stamp cover** (no tree re-place), **Place / Replace foliage** (full Phase 2+3 order).
   - Changing a point, width, or generating marks foliage stale and re-rasters cover so procedural grass updates immediately; trees still sit on the old path until re-place.

8. **Regeneration triggers.**
   | Event | Cover | Auto trails | Tree re-place |
   |---|---|---|---|
   | Edit authored control point / width | Re-stamp now | Keep existing generated | Stale; author runs Place |
   | Generate game trails | Re-stamp | Rebuild generated only | Stale |
   | `startFoliageSpawn` / Place | Full rebuild (paths + trees + canopy) | Recreate generated only if `autoGenerate` is true | Yes |
   | Water level / placed water change | Stale | Stale if auto | Stale |
   | Height sculpt / World Engine reload | Stale | Stale | Existing foliage stale behavior |
   | Camera move / play | Never | Never | Never |

   Default `autoGenerate` to **false** so loading HurricaneRidge does not surprise the author with a new trail network. Offer a prompt after the first Place on a map that has water but no paths.

### Unit tests

- **`TrailRasterLeavesBareCoreAndShoulder`** — flat cover 255, stamp a 10 m straight human path, width 1.6. Samples on the centreline are 0/0; a point 1.5–2 m off the centre has partial heightScale; a point 4 m away is still 255/255. A second path crossing the first stays bare at the junction (min-blend).
- **`NoisyEdgeIsDeterministic`** — same seed ⇒ identical edge samples; a different seed changes at least some shoulder texels.
- **`TreesAndRocksSkipPathCore`** — pre-stamp a path, `grassTreesPerM2 = 1`, `rockPerM2 = 1` on a mixed splat if needed. No tree or rock lands inside the core radius.
- **`LeastCostAvoidsWaterAndSteep`** — a heightmap with a lake rectangle (`FoliageWaterVolume`) and a 45° ridge. Path from one side to the other goes around the water and does not crest the ridge. A blocked map (all water) returns no path, no crash, no leftover stamps.
- **`AuthoredPathsSurviveRegenerate`** — one `authored: true` polyline plus `autoGenerate`; after generate, the authored point list is unchanged and still present in a JSON round trip.
- **`SceneFile` missing `trails` key** — loads as empty, `autoGenerate == false`.

### Verification

- **Editor:** Draw a human path across a meadow, stamp cover, confirm a bare ribbon with ragged edges and short grass on the shoulder (both tuft and procedural). Place foliage: no new trees in the ribbon; existing trees on the line disappear only after Place. Generate game trails on a map with a lake: a few routes appear from shoreline to flatter ground, animals narrower and wiggliest. Move a control point: grass updates, trees wait for Place. Enable splat stamp on a copy of the map and walk the path — dirt footsteps, no silent mutation when the box is off.
- **Sandbox:** Load the saved scene. Paths are in the cover sidecar (and optional splat), not as outliner entities. Play mode shows the same ribbons; no hitch as grass tiles stream in.
- Perf: Generate + stamp on a 2 km working map should stay on the background spawn thread (reuse `onProgress`, phases `"trails"` / `"cover"` / `"count"` / `"keep"`), never inside `onRender`.

---

## Phase 4 — Natural clustering

### Goal and payoff

Forests, meadows, clearings, tight groves, and understory rings that read as nature rather than a uniform hash field. Deterministic via `hash21` + `FoliageDensity::seed`. Works with slope (Phase 1), spacing and cover (Phase 2), and trails (Phase 3).

### Files to touch

- `Terrain/FoliageSpawn.cpp` / `.h` — patch masks in `consider()`, satellite spawning after parents are kept.
- `Terrain/FoliageFile.h` `FoliageDensity` (and `FoliageSceneDesc` / SceneFile / Editor sliders) — scale and strength knobs with sane defaults so old densities still mean "trees per m²" *on average* inside forest patches.
- Tests in `FoliageSpawnTests.cpp` (and a small cover test if satellites stamp canopy-adjacent flowers).

### Implementation details and gotchas

1. **Multi-scale masks, all from `hash21`.** Do not add a third-party noise library. Value noise / FBM can be a few octaves of hashed lattice; Worley is "hash the 3×3 neighboring cell seeds and take min distance". Suggested scales (world metres, expose on density):
   - Large (60–120 m): forest vs meadow vs clearing. Trees multiply by this; meadows almost zero trees, high flowers; clearings near zero everything except path-adjacent grass.
   - Medium (15–30 m): grove blobs inside the forest (raise local tree accept).
   - Fine (2–6 m): tuft / flower clumps.
   Multiply the existing `p` by `lerp(1 - strength, 1, mask)` so `strength = 0` reproduces Phase 2 results (important for tests and for authors who want the old look).

2. **Understory satellites.** After a tree is kept, spawn N children in an annulus (`inner = canopyRadius * 0.6`, `outer = canopyRadius * 1.3`, clamped e.g. 1.5–4 m). Count and kind are weighted (flowers / tufts first; ferns when Phase 5 adds a bush/fern variant). Skip if the sample fails slope, water, path-core, or trunk-core tests. Use salts distinct from `kSaltFlowerAccept` etc. (16–27) so satellites do not alias the lattice flowers. Cap children so a 2 km forest cannot blow `kMaxFoliageInstances` / `kMaxFoliagePerFile`.

3. **Pebbles downhill of large rocks.** If `rec.scale` is above a threshold (e.g. 1.2, rocks already scale 0.60–1.80 at spawn 227–230), spawn 2–5 smaller `FoliageKind::Rock` instances along `-normal.xz` (downslope) at 0.4–1.2 m, with smaller scale and the usual pitch clamp. Skip path cores and water if you want shore boulders to stay; default: skip path cores, allow dry slopes only.

4. **Determinism.** Same seed, same height, same splat, same trails ⇒ identical records, including satellites. Prefer a packed `(parentIx, childSlot)` hash, not `records.size()`, so a cap-thin (`kMaxFoliageInstances`, spawn 200–207) does not reshuffle children.

5. **Interaction with spacing.** Grove masks raise `p` but the 2–4 m tree hash still applies; groves should look tight, not intersecting. If a grove feels too thin, slightly lower spacing *inside* a strong grove mask rather than disabling the hash.

### Unit tests

- **`PatchMaskZeroStrengthMatchesBaseline`** — `strength = 0` on a flat grass map matches a stored count / first-N positions from the Phase 2 spawner (or run twice, with and without the mask code path).
- **`ForestMeadowSplitIsSeedStable`** — two maps, same seed, strong large-scale mask: tree positions identical; a seed change moves the forest. A 40 m disk in a known meadow (mask forced to 0 in a test hook or a tiny custom mask) has no trees and more flowers than the forest disk.
- **`UnderstoryLandsInAnnulus`** — one forced tree at the origin, canopy radius 3 m. Every flower/tuft satellite is in `[1.8, 3.9]` m, none in the trunk core, none on a 45° ramp (slope still wins).
- **`PebblesFollowDownslope`** — a gentle rock ramp, one large rock. Child rocks have smaller scale and their XZ is on the downhill side of the parent (dot with `-n.xz` > 0).
- **`SameSeedRepeats` still holds** with clustering on.

### Verification

- **Editor:** Re-place on HurricaneRidge (or the default 2 km terrain). You should read groves and openings, not a regular dotted grid. Clearings can sit next to Phase 3 trails without trees marching across the ribbon. Peek under a tree: a ring of flowers/tufts, then shorter grass, then a bare trunk disk. Large rocks collect a few smaller ones on the downhill side.
- **Sandbox:** Same seed as the saved scene. Walk a meadow → forest edge → grove; density change should be obvious at walking speed, not only from the air. Watch the 96 m gather (`kGatherRadiusM`) as you enter a grove — no one-frame hole from the 65536 cap on a default-density map (if a stress density hits the cap, the existing even thinning still applies; log `FoliageSpawn: cap thinned` / gather thinning).

---

## Phase 5 — Variety (species, tint, scale, draw split)

### Goal and payoff

More than one birch, one dandelion, one mossy rock, and one tuft. Weighted species lists, a variant byte on each record, multi-prototype extraction from the existing "set" glTFs, wider tree scale, and a cheap per-instance tint in `FoliageGBuffer.hlsl`. Stay inside the current **4 kinds × 8 parts** cull, and stay honest about the 65,536 / 96 m gather and 2,048 physics bodies.

### Files to touch

- `Terrain/FoliageFile.h` — `kFoliageVersion = 2`; document `pad[0] = variant`, `pad[1..2] = packed tint` (or leave tint hash-only; see gotcha 3). **Stop zeroing those bytes** in `FoliageFile.cpp` 79–84.
- `Terrain/FoliageFile.cpp` `loadFoliageTile` — accept version 1 and 2; v1 forces variant 0 and a neutral tint.
- `Terrain/FoliageDensity` / `FoliageSceneDesc` / `SceneFile.cpp` — replace (or supplement) the four single paths with a weighted list per kind: `{ path, weight, scaleMin, scaleMax, ... }`. Keep the old `treeModel` keys as a fallback when the list is absent so `level.json` still loads.
- `Render/FoliagePrototypes.cpp` — `makeFoliageShowcaseParts` / `grassTuft` gain a "extract every origin cluster, not only the nearest" path; `sync()` builds N prototypes per kind.
- `Render/FoliagePipeline.cpp` `ensureDrawSet` / `uploadKinds` / CPU and GPU cull — split the gathered list **per (kind, variant)** and issue one instanced draw per prototype, still with `kind` 0–3 for wind and shadow-skip.
- `content/shaders/FoliageGBuffer.hlsl` (and `FoliageDepth.hlsl` only if the packed buffer layout is shared) — multiply albedo by a per-instance tint. Today `color.rgb` is the *material* constant (`FoliageGBuffer.hlsl` 11, 101; filled with `1,1,1` in `FoliagePipeline.cpp` 429–432), not per instance.
- `Physics/FoliageCollision.cpp` — unchanged caps; variants still collide as tree/rock.
- Tests: `FoliageFileTests.cpp` (v1 load, v2 pad round-trip), `FoliageSpawnTests.cpp` (variant distribution, scale range), `FoliageShowcaseTests.cpp` (multi-cluster extract), SceneFile list round-trip.

### Implementation details and gotchas

1. **`saveFoliageTile` currently wipes `pad`.** `expectPadAndYaw` in `FoliageSpawnTests.cpp` 61–65 asserts zeros. Version 2 must write `pad` through, and v1 tests should still see zeros after a v1 load. Bump the version; do not silently overload v1 on disk.

2. **Do not grow `FoliageRecord` past 32 bytes.** `kMaxFoliagePerFile` is literally `(32 MiB - 32) / 32`. `pad[0]` is the variant index (0–255, plenty for a handful of glTFs per kind).

3. **Tint transport.** `gKinds` is already a `uint` per instance (`FoliagePipeline.cpp` 1407–1416; `FoliageCull.hlsl` 136). Pack `kind` in the low 8 bits (cull already does `kind >= kKindCount`), `variant` in the next 8, and an RGB565 tint in the top 16. In the G-buffer PS, decode and multiply `albedoS.rgb`. Depth / shadow can keep ignoring it. Alternative if you want a smaller shader diff: hash a tint from `(seed, ix, iz, variant)` only in the vertex shader — no extra bits, but you cannot author a specific color. Prefer the packed uint so it is reproducible and saveable.

4. **Showcase sets become several prototypes.** The RockMoss and grass_medium glTFs are authored as lineups. Change the cluster walk (`kCluster = 0.25` m, line 170) to group parts by origin and emit one model per group, still dropping `LOD1+` and seating Y=0. `grassTuft` becomes "all tufts", not "the nearest". Log the count (`FoliagePrototypes: rock showcase` already logs kept names at 136). If a path is a single plant, you still get one variant (today's behavior).

5. **Weighted pick at accept time.** `variant = weightedIndex(hash21(ix, iz, seed ^ kindSalt), weights)`, optionally biased by the Phase 4 patch (e.g. species A in deep forest, species B on meadow edges). Per-species `scaleMin/Max` replace the kind-wide ranges (trees should widen from 0.85–1.15 to something like **0.70–1.60**, log-uniform if it still looks even).

6. **Draw split, not a 5th cull kind.** Keep `kCullKindCount = 4` and `kCullPartsPerKind = 8` (`FoliagePipeline.h` 73–74). After gather, build an index list per `(kind, variant)` and draw each prototype with the same compact/args path, or subdivide the existing kind lists. Do **not** raise kind count just to add meshes — that touches `FoliageCull.hlsl`, wind `kindA/B/C[4]`, `WindKind`, and UAVs. Each prototype must still have ≤ 8 opaque parts.

7. **Performance envelope (do not pretend this phase removes it).**
   - Gather cap **65,536** in a **96 m** disk, thinned if exceeded (`FoliagePipeline.cpp` 1341–1378). More unique meshes do not raise this cap; denser Phase 4 groves plus tufts plus satellites make it easier to hit. Log and, later, add a mesh LOD / impostor *past ~40–60 m* (showcase LOD1+ is currently thrown away — that is the natural source). Out of scope for the first variety PR, but call it out in the PR notes.
   - Physics **2,048** bodies in 64 m (`FoliageCollision.h` 18–20). Variants do not change this; denser groves will hit the cap sooner (there is already a warn at `FoliageCollision.cpp` 367).
   - Upload / PSO: N variants × parts extra binds per frame; keep N small (3–6 trees, 3–5 rocks, 3 tufts, 2–3 flowers) for the first ship.
   - Flower/tuft ×10 scale (`kSmallFoliageScale`, 299–313) still applies per kind, not per variant, unless a species is authored at world scale.

8. **Missing glTFs.** `FoliagePrototypes::sync` already falls back to the procedural prototype and logs once (389–395). A list entry that fails to load should drop out and renormalize weights, not leave holes in variant indices already written to a bin. Changing a list under an existing `.foliage.bin` is a content compatibility issue — document "re-place after changing species lists" and/or treat out-of-range variant as 0.

### Unit tests

- **`FoliageFile_V1LoadsAsVariantZero`** — write a v1 bin (or load a fixture); records have `pad == {0,0,0}` and load succeeds under the new code. Saving them back writes v2.
- **`FoliageFile_V2RoundTripsVariantAndTint`** — `pad[0] = 3`, packed tint in `pad[1..2]`, survives save/load. `save` must not zero pad.
- **`SpawnPicksWeightedVariants`** — two fake species weights 3 and 1 on a large flat map; counts are roughly 3:1 (loose bound), and `SameSeedRepeats`.
- **`TreeScaleUsesWiderRange`** — with the new defaults, some tree `scale` is `< 0.85` or `> 1.15`, all inside the configured min/max.
- **`ShowcaseExtractsMultipleClusters`** — unit `makeFoliageShowcaseParts` (or a new helper) on two part groups 2 m apart returns two clusters; LOD1-named parts stay dropped (`FoliageShowcaseTests.cpp` already exists for the single-cluster path).
- SceneFile: a list of two trees with weights round-trips; a legacy file with only `treeModel` builds a one-entry list.

### Verification

- **Editor:** Point the tree list at the BirchTree plus at least one other path (or two clusters from one set). Re-place: mixed silhouettes, obvious size variation, slight color variation in the canopy, not a cloned army. Rock sets finally look like a set. Open a pre-phase-5 scene: v1 bins load, all variant 0, no crash.
- **Sandbox:** Same mix at play time. GPU cull still runs (no fallback spam `foliage cull unavailable`). Shadows still skip grass tufts. A stress Place at high density should log gather thinning rather than overflow. PIX / the existing foliage profile scopes should show N extra instanced draws, not a CPU per-instance loop.
- Visual sign-off needs Windows / D3D12 (`darkengine6-pc` once it is up). File I/O and spawn tests do not.

---

## Suggested sequence and sizing

| Phase | Depends on | Approx. shape | Risk |
|---|---|---|---|
| 1 Slope rules + Place button | — | 1 short PR | Low. Must rewrite `TreesStayUprightRocksFollowNormal`. Old bins unchanged until Place. |
| 2 Spacing + cover mask | 1 | 1 PR (new sidecar + grass dirty path) | Medium. Both grass systems and the Editor/runtime grid load path have to agree; missing cover must no-op. |
| 3 Trails + game paths | 2 (same mask) | 1–2 PRs (core rasterize + Editor tool / auto trails) | Medium. Auto pathing on large working maps must stay off the render thread. Splat dirt stamp is easy to do accidentally — keep it explicit. |
| 4 Clustering + understory | 2 (mask + spacing); 3 if paths should cut groves | 1 PR | Low–medium. Determinism and the instance cap are the sharp edges. `strength = 0` is the compatibility switch. |
| 5 Species, tint, scale | 1 (spawn still owns records) | 1 PR + content list | Medium. Version 2 + stop zeroing `pad`. Shader packing must not break `FoliageCull` kind tests. Visual check on D3D12. Impostor/LOD is explicitly later. |

Recommended order: **1 → 2 → 3 → 4 → 5**. Phase 4 can start after 2 if trail work slips, but groves will walk across bare ribbons until 3 lands. Phase 5 does not need 3 or 4, but looks much better on top of them.

Phases 1–4 are CPU-side and can be unit-tested without HLSL. Phase 5's tint and multi-draw need a Windows / D3D12 worker for visual sign-off. Nothing in this plan requires a GitHub PR until you ask for one.

---

## Open questions for Travis

1. **Default tree lean:** 7° cap, or ship 0 (stay upright) and let the slider opt in?
2. **Default tree spacing:** 3 m, or tighter/looser for the current BirchTree scale?
3. **Species / models to add** once Phase 5 lands? The checkout does not include BirchTree, Dandelion, RockMoss, or grass_medium — are those on the Windows machine, and which extra trees / bushes / ferns / rock sets should be first in the weighted lists? A fern/bush species is what makes understory satellites read as more than "more dandelions."
4. **Auto game trails on by default?** The plan defaults `autoGenerate` to **false** and offers a Generate button, so World Engine load does not invent a trail network. Should a first Place on a map that has water auto-prompt instead?
5. **Human vs animal default widths** (plan: 1.6 m and 0.8 m) — any reference you want to match?
6. **Stamp trails into the splat dirt channel** as part of Generate, or always a separate explicit button?
7. **Should procedural `kNormalYMin` (0.80) be raised to match tuft 0.82**, or left alone?
8. **Impostor / runtime LOD** for trees past ~40–60 m: schedule as a Phase 6 after variety, or only if the 96 m / 65,536 gather starts to bite on the default map?

---

## Out of scope (on purpose)

- Runtime mesh LODs / impostors for instanced foliage (called out as a follow-up in Phase 5; showcase LOD1+ is currently discarded).
- Raising `kCullKindCount` above 4, or growing `FoliageRecord` past 32 bytes.
- Moisture / biome maps, stream-aware erosion of trails, or winter/snow cover on paths.
- Making trails `SceneObjectType` entities, or driving pawn `AI::Pathfinder` along them (they can share the cover / dirt as a later hook).
- Changing footstep audio or NPC speed except via the optional splat dirt stamp.
- Replacing procedural grass cards with the glTF tuft mesh, or vice versa.
- Authoring a new HLSL grass species atlas (tint + heightMul is enough for Phase 5).
