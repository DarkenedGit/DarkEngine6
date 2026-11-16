# DarkEngine6 review: `de155eb` vs `a08ee91`

**Tip:** [`de155eb`](https://github.com/DarkenedGit/DarkEngine6/commit/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87) "No more duplicate crap", committed Nov 15, 2026 at 3:37 PM PST.
**Method:** I read the code only, from the tarball at that SHA, and diffed it against `/workspace/de6-a08ee91`. Nothing was compiled or run, because there's no Windows/D3D12/HLSL toolchain here. **Confirmed** means I followed the code path by reading it. **Suspected** needs a debugger, a test or a visual check.
Line links point at `de155eb`. Sandbox docking is off on purpose and isn't flagged.


---

## 1. Commits since `a08ee91`

| Commit | Time (PST) | Files | What changed |
|---|---|---|---|
| [`e2c5985`](https://github.com/DarkenedGit/DarkEngine6/commit/e2c5985) "No more SceneObjectData" | Nov 15, 1:16 PM | 31 (+1954 / −1800) | `SceneObjectData` is now transform, colour, `archetype` and `overridesJson` [SceneTypes.h L52-L60](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Scene/SceneTypes.h#L52-L60). The typed readers and writers in `SceneFile.cpp` are gone (−711 lines). `content/SceneObjectSpawn.cpp` (+946) gets JSON accessors and the `sceneWrite*Diff` writers. The cloud, particle, stream, spawner and movie paths move to JSON. Tests were updated. |
| [`1edf910`](https://github.com/DarkenedGit/DarkEngine6/commit/1edf910) "Duplicate code removal" | Nov 15, 2:06 PM | 26 (+604 / −89) | The archetype `Health` block becomes the only source of health: `healthMax` was removed from the combat JSON and `archetypeHealthSettings` was added. `attachHunter` no longer creates health. Adds 112 lines to `SceneFileTests`. |
| [`de155eb`](https://github.com/DarkenedGit/DarkEngine6/commit/de155eb) "No more duplicate crap" | Nov 15, 3:37 PM | 82 (+2240 / −1026) | Adds a combat profile table (`Combat.profile`, `CombatProfileComponent`), `ParticleEmitterJson` (one particle serializer), `Play/SpawnHost` (`makeSpawnHost`), `Network/ArchetypeNet.h`, `content/env/weather.json`, `kUnitCubeModel`, and an `ai.crow` behavior. It removes `kItems`, the loadout and throwable fallbacks, `seedLegacyAliases`, `SpawnerUnit`, `ccRoot`/`ccStun`/`ccKnockdown`, and the tooltip `if` chain. Sandbox save respawn now goes through `respawnSavedArchetype`. Adds a Settings page with control lists (`Scene/ControlGuide`). Tests added: `ControlGuideTests` and more in `ArchetypeSpawn` and `ContentDb`. |

No shaders changed in these commits.

---

## 2. Status of earlier findings

### 2a. Migration bugs and suspected items from the `a08ee91` audit

| # | Finding | Status | Evidence |
|---|---|---|---|
| 1 | Archetype `Health.max` ignored for hunters and wolves | **Fixed** | `attachHunter` no longer creates health [AiSystem.cpp L236-L250](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/AI/AiSystem.cpp#L236-L250). `attachKind` assigns the profile, then `ensureHumanoidBody(bodyHealth(params))` [ContentBuiltin.cpp L300-L320](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/content/ContentBuiltin.cpp#L300-L320). `spawnHunter` does the same [AiSystem.cpp L320-L334](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/AI/AiSystem.cpp#L320-L334). **Note:** the wolf archetype is now `max 48, regen 5, delay 4`, the value that actually applied before. It isn't the 60 that was written. Bump it if you meant 60. |
| 2 | Player health set in 3 places with different values | **Fixed** | `healthMax` was removed from `EntityCombat` and from `player.entity.combat.json`. The Editor and Sandbox both use `archetypeHealthSettings("core:player")` [EditorPlay.cpp L127](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Editor/EditorPlay.cpp#L127), [SandboxApp.cpp L1348](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Sandbox/SandboxApp.cpp#L1348). |
| 3 | Editor save drops override fields that `SceneObjectData` doesn't model | **Fixed** (one edge case, see N-3) | The loader keeps the whole `overrides` bag [SceneFile.cpp L1257-L1340](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Scene/SceneFile.cpp#L1257-L1340). `sceneWriteComponentDiff` merges into the existing block, so `Model.anim` and `Mesh.primitive` survive [SceneObjectSpawn.cpp L707-L745](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/content/SceneObjectSpawn.cpp#L707-L745). |
| 4 | Scenes write full blocks, not diffs | **Fixed** | The diff baseline is component defaults plus archetype params [L320-L328](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/content/SceneObjectSpawn.cpp#L320-L328), and `jsonSame` compares floats at float precision (L263-L274). Every Editor capture path uses `sceneWrite*Diff` [EditorSceneFile.cpp L112-L200](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Editor/EditorSceneFile.cpp#L112-L200). |
| 5 | Sandbox can't respawn `core:` or mod archetypes from a save | **Fixed** | It falls through to `respawnSavedArchetype` [SandboxSave.cpp L226-L262](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Sandbox/SandboxSave.cpp#L226-L262), [ContentBuiltin.cpp L903-L918](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/content/ContentBuiltin.cpp#L903-L918). `healthpack` and `tree` keep their own branches. |
| 6 | Save ID keyed on the ECS entity index (suspected) | **Fixed** | It now uses a per-boot `archetypeStamp` counter [SandboxBoot.cpp L1122-L1126](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Sandbox/SandboxBoot.cpp#L1122-L1126). Latent issue: the stamp is 0 when `m_boot` is null. Only boot calls this today. |
| 7 | `core:crow` placed directly is an empty entity (suspected) | **Fixed** | `crow.archetype.json` has `Model`, `Health` and `ai.crow`, and `attachCrowBody` fills an existing entity [Crow.cpp L1012-L1086](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Play/Crow.cpp#L1012-L1086). |
| 8 | Rename `light.searchlight` to `light.local` | **Fixed** | Renamed. The old name stays as a warned alias [ContentBuiltin.cpp L765, L801](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/content/ContentBuiltin.cpp#L760-L801). |
| 9 | `seedLegacyAliases` repeats `legacy_aliases.json` | **Fixed** | The C++ seed is gone. The JSON has every alias, and `null` or `""` removes one, so mods can drop aliases [ContentDb.cpp L319-L335](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/content/ContentDb.cpp#L319-L335). |

### 2b. Duplication items (D1-D13)

| # | Item | Status | Notes |
|---|---|---|---|
| D1 | Particle fields in 5 copies | **Fixed** | There is one `particleToJson`/`particleFromJson` [ParticleEmitterJson.cpp](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Particles/ParticleEmitterJson.cpp), used by the component reader, the scene accessors and the diff writer. |
| D2 | Light defaults in 6 copies | **Partly** | `applyTypeLightDefaults`, `fillDefaultLocalLight` and the `SceneObjectData` copies are gone. Three remain: the `LocalLightComponent` defaults, the archetype JSONs, and the `SceneSpawn.cpp` fallbacks 1885 / 8 / 25 [SceneSpawn.cpp L170-L172](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Scene/SceneSpawn.cpp#L170-L172). |
| D3 | Archetype vs `.entity.json` vs C++ model fallbacks | **Partly** | The C++ `models/human.gltf` fallbacks are gone, replaced by `archetypeModelPath`. `player`/`hunter`/`wolf` `.entity.json` still repeat `gltf`/`anim`/`physics` from the archetypes. |
| D4 | CC durations in 3 copies | **Partly** | The `EntityCombat` cc fields are gone, and CombatSystem reads `statusDef(...)->defaultDuration` [CombatSystem.cpp L307-L325](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Combat/CombatSystem.cpp#L307-L325). The compiled `kStatusCatalog` durations still shadow `statuses.json`, so there are 2 copies. The values match today. |
| D5 | `kItems` and the loadout fallback | **Fixed** | Both were removed. A missing file only logs a warning; it doesn't fail loudly (see N-9). |
| D6 | Spawn host written 5 times | **Partly** | `makeSpawnHost` exists, but each of the 5 callers still fills `SpawnServices` by hand, and `SpawnServices` mirrors `ArchetypeSpawnHost` field for field (see §4). Respawn is now unified. |
| D7 | Weather presets and time of day | **Partly** | `kDefaultTimeOfDay = 16.2f` is the single default [Environment.h L12](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Sky/Environment.h#L12). The preset numbers moved to `env/weather.json`, but the names are still a fixed 4-way `if` in [SceneFile.cpp L120-L149](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Scene/SceneFile.cpp#L120-L149), so the JSON can't add a preset (see N-8). |
| D8 | HLSL `FrameConstants` in 9 layouts, BonePalette b1/b2 | **Still open** | No shader changes. |
| D9 | Audio blip params repeated | **Still open** | e.g. [SandboxApp.cpp L310](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Sandbox/SandboxApp.cpp#L310). There's no `audio/cues.json`. |
| D10 | Upgrade stat names parsed twice | **Still open** | `Upgrades.cpp` is unchanged. |
| D11 | `toolbarH` in 2 places, raw colours | **Still open** | [EditorUi.cpp L257](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Editor/EditorUi.cpp#L257), [EditorPlay.cpp L1128](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Editor/EditorPlay.cpp#L1128). The new Settings page adds about 15 more raw `ImVec4`/`IM_COL32` colours. |
| D12 | Camera FOV and far plane | **Still open** | [EditorAppInit.cpp L280](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Editor/EditorAppInit.cpp#L280) is 1e4 and [SandboxBoot.cpp L1270](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Sandbox/SandboxBoot.cpp#L1270) is 1e5. |
| D13 | `unit_cube` literal ×10, throwable defaults | **Fixed** | There's one `kUnitCubeModel` [AssetHandle.h](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Assets/AssetHandle.h), and the throwable C++ defaults are gone. |

### 2c. Hard-coded content (H1-H15)

| # | Item | Status | Notes |
|---|---|---|---|
| H1 | About 40 archetype-name compares | **Partly** | The same `rg` pattern finds 55 hits, down from 62. About half of those are now legitimate `Mesh.primitive` string checks. The rough count of real **name or tag dispatch** left is about 20: [EntityCombat.cpp L311, L318, L344](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Scene/EntityCombat.cpp#L305-L362), [CharacterAttach.cpp L303, L435](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Play/CharacterAttach.cpp#L424-L445), [SpawnListed.cpp L66-L68](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Play/SpawnListed.cpp#L60-L115), [AiSystem.cpp L368, L1213](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/AI/AiSystem.cpp#L1210-L1215), [HunterJump.cpp L70, L97](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/AI/HunterJump.cpp#L65-L100), [Crow.cpp L508](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Play/Crow.cpp#L508), [SandboxSave.cpp L185, L204](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Sandbox/SandboxSave.cpp#L185-L210), [EditorUi.cpp L940-L951](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Editor/EditorUi.cpp#L940-L951), and the pre-DB bare-name fallbacks in [SceneObjectSpawn.cpp L1106-L1126](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/content/SceneObjectSpawn.cpp#L1098-L1127). The tooltips moved to `editor.tooltip`, and `entityTypeKey` now reads `Physics.file`. |
| H2 | Character and UI sound cues | **Still open** | |
| H3 | Status mechanics compiled in | **Still open** | `StatusId`, `StatusCatalog.cpp` and `StatusParticlePresets.cpp` are unchanged. |
| H4 | `SpawnerUnit` enum | **Partly** | The enum is gone, and units are archetype ids (`unitArchetype`). But `spawnListedUnit` still branches on `core:crow`/`core:wolf`/`core:hunter` (N-4), and the Editor combo only offers 3 ids (N-10). |
| H5 | Combat profiles keyed by 3 names | **Partly** | There's a `Combat.profile` path and a table keyed by archetype id, cleared on reload [EntityCombat.cpp L305-L385](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Scene/EntityCombat.cpp#L305-L385). But the wolf tag beats the stored profile (N-2), the lookup does string work on every call (N-5), and the seed defaults still only know player, hunter and wolf. |
| H6 | Main menu content (title, entries, 0.85 volume) | **Still open** | [SandboxApp.cpp L649-L650](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Sandbox/SandboxApp.cpp#L645-L652), [L1881](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Sandbox/SandboxApp.cpp#L1881) |
| H7 | Status and HUD particle presets | **Still open** | This is now easy, since D1's serializer exists. |
| H8 | Quality presets and camera | **Still open** | |
| H9 | Weather presets | **Partly** | Same as D7. |
| H10 | Editor templates and tooltips | **Partly** | The tooltips moved to archetype JSON. The new-scene template and Glow Prop are still code. |
| H11 | Hard-wired keys | **Still open** | The new `ControlGuide` lists these keys by hand, which documents them but doesn't make them rebindable. |
| H12-H15 | Biomes, AI tuning, upgrade stats, localisation | **Still open** | Not touched. |

### 2d. Still-open items from the `093f812` UI review

None of these were touched. `ImGuiHost.cpp`, `DialogueHud.h`, `SandboxHud.cpp` and `MainMenu.cpp` are byte-identical to `a08ee91`.

| Item | Status | Link |
|---|---|---|
| Dialogue choice can commit twice (add `NoNav`) | **Still open** | [DialogueHud.h L114](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Play/DialogueHud.h#L114) |
| `ImGuiHost::init` leaks on early failure | **Still open** | [ImGuiHost.cpp L304-L313](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Ui/ImGuiHost.cpp#L304-L313), [L394](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Ui/ImGuiHost.cpp#L392-L396) |
| Key repeat on Esc, Enter, Space and A | **Still open** | [ImGuiGameChrome.cpp L752, L772](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Ui/ImGuiGameChrome.cpp#L752-L772) |
| Jump-to-Level wheel scroll snaps back | **Still open** | [ImGuiGameChrome.cpp L686](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Ui/ImGuiGameChrome.cpp#L683-L687) |
| Min-width card asserts (`InvisibleButton` with zero size) | **Still open** | [ImGuiGameChrome.cpp L55-L64](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Ui/ImGuiGameChrome.cpp#L55-L64) |
| Card drag lags by about 6 px | **Still open** | [ImGuiGameChrome.cpp L65-L70](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Ui/ImGuiGameChrome.cpp#L65-L70) |
| NEW GAME / CONTINUE stubs and the hard-coded subtitle | **Still open** | [SandboxApp.cpp L649-L650, L750](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Sandbox/SandboxApp.cpp#L645-L752) |
| Master volume not saved | **Still open** | The new Settings page edits it, but boot still resets it to 0.85 ([SandboxApp.cpp L1881](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Sandbox/SandboxApp.cpp#L1881)). |
| 128 px title font in all 3 apps (atlas cost) | **Still open** | [ImGuiHost.cpp L180-L236](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Ui/ImGuiHost.cpp#L180-L236) |
| Pause card doesn't re-centre after a resize | **Still open** | [SandboxHud.cpp L1187](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Sandbox/SandboxHud.cpp#L1187) |
| Settings and Credits do nothing in the sprite fallback | **Still open** | `MainMenu.cpp` unchanged |

**Score:** 12 fixed, 10 partly fixed, 26 still open, and 1 regression (N-1, from the phase-1 placeholder work).

---

## 3. New bugs in this push

### Confirmed

**P2**

- **N-1 (regression): "Missing" placeholders are invisible and can't be clicked in the Editor.**
  - `editorShownInMode` now returns `false` when the archetype isn't found ([EditorObject.h L65-L71](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Editor/EditorObject.h#L65-L71)). The old bare-name fallback returned `true` in 3D.
  - Every draw and 2D-pick loop filters on it [EditorRender3D.cpp L79, L267, L711](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Editor/EditorRender3D.cpp#L75-L85), [EditorScene.cpp L51](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Editor/EditorScene.cpp#L46-L55), [EditorRender.cpp L123](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Editor/EditorRender.cpp#L123). So the magenta marker that `spawnPlaceholder` creates never draws.
  - The object is still saved, but you can't see, move or delete it. This is the case that matters for mods that aren't installed.
  - **Fix:** when the archetype is missing, return `true` for placeholders (`if (!archetype) return sceneIsPlaceholder(object);`) so they show in whichever mode the scene is in.

- **N-2: Mod wolves can't have their own combat profile.**
  - `configureNpc` renames the tag to `"Wolf"` for every `NpcKind::Wolf` ([CharacterAttach.cpp L435](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Play/CharacterAttach.cpp#L424-L445)), and every `ai.wolf`/`ai.animal` archetype becomes that kind.
  - `entityCombatProfile` checks the wolf tag **before** `CombatProfileComponent` ([EntityCombat.cpp L358-L372](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Scene/EntityCombat.cpp#L358-L372)).
  - So `mymod:direwolf` with its own `Combat.profile` silently uses `core:wolf`'s profile. `applyNpcMovementFile(..., "wolf")` and `applySkillProfile(..., "wolf")` are hard-coded the same way.
  - **Fix:** check the stored component first and drop the tag test. Read the movement and skill files from archetype params.

- **N-3: A model override is erased if the model didn't load when you save.**
  - When `ModelComponent` is missing or its asset didn't resolve, `modelPath` stays empty ([EditorSceneFile.cpp L186-L199](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Editor/EditorSceneFile.cpp#L186-L199)).
  - `sceneWriteModelDiff("")` then writes `gltf: null`, which erases the override ([SceneObjectSpawn.cpp L846-L855](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/content/SceneObjectSpawn.cpp#L846-L855)).
  - So a placement that points at a mod or asset missing right now loses its `gltf` on the next save.
  - **Fix:** skip the Model diff when no asset is resolved. Only clear it on an explicit user action.

**P3**

- **N-4: The spawner decides unit handling by exact id.**
  - `crow`/`wolf`/`hunter` come from comparisons with `core:*` ids ([SpawnListed.cpp L66-L115](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Play/SpawnListed.cpp#L60-L115)).
  - A mod crow with `ai.crow` needs `host.ai`/`host.play`, goes through `noteHunter` (into the chase hunter list) and gets the human physics bind.
  - A mod hunter variant misses `stockHunter` and the dialogue.
  - **Fix:** decide by behavior (`ai.crow`) or by component (`AiAgentComponent`, `InteractKind::Guard`).

- **N-5: The unknown-archetype warning logs a moved-from string.** `outScene.objects.push_back(std::move(o));` comes right before `DE_LOG_WARN(..., o.archetype)` ([SceneFile.cpp L1226-L1227](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Scene/SceneFile.cpp#L1216-L1228)). The log shows `''` instead of the id. Log before the move.

- **N-6: Placeholder round-trip isn't byte-identical.**
  - The save path always calls `sceneSetInt(d, "emissiveMesh", -1)` and `sceneSetInt(d, "pickupCount", …)` ([EditorSceneFile.cpp L113, L185-L186](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Editor/EditorSceneFile.cpp#L112-L187)). These inject `"emissiveMesh": -1, "pickupCount": 1` into the raw JSON of an unknown object.
  - The raw path also writes `rawJson` verbatim ([SceneFile.cpp L666-L678](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Scene/SceneFile.cpp#L666-L678)), so moving a placeholder isn't saved. That's moot until N-1 is fixed.
  - **Fix:** skip the bag writes when `sceneIsPlaceholder(d)`. Optionally patch `position`/`rotation`/`scale` into the raw object.

- **N-7: Combat profile lookup does string work on every call.**
  - Each `entityCombatProfile` call builds a `std::string`, runs `canonicalContentId`, hashes, and does an `unordered_map` lookup.
  - It's called several times per AI agent per frame and on every hit [AiSystem.cpp](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/AI/AiSystem.cpp#L418) L418, L512, L573, L608, L726, L991, L1024, L1628, L2396, plus `CombatSystem`, `PlayerCombat` and `ContactGrind`.
  - It's correct, but it's the per-frame string lookup the audit warned about.
  - **Fix:** cache an index on `CombatProfileComponent`, checked against a table generation counter that `clearCombatProfileTable` bumps.

- **N-8: The weather table is a static that loads once.**
  - `weatherRows()` ([Environment.cpp L75-L130](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Sky/Environment.cpp#L75-L130)) is a function-local static. It never hot-reloads.
  - `Environment::weather = WeatherState::PartlyCloudy()` triggers it when `m_env` is constructed in each app. If that happens before mod overlay roots are registered, a mod's `weather.json` is ignored (suspected; depends on init order).
  - If the file is missing, all 4 presets collapse to `WeatherState{}`, and `weatherNameFor` saves every scene as `"clear"`.
  - Preset names stay fixed in `SceneFile.cpp` (D7).

- **N-9: Missing core JSON now runs silently empty.**
  - `items/*.json` ([Item.cpp L190](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Gameplay/Item.cpp#L186-L191)), `player/loadout.json` ([PlaySession.cpp L483](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Play/PlaySession.cpp#L480-L484)) and `throwables.json` each only log a warning. Play starts with no weapons.
  - Also, `findItemDef` now warns on every miss ([Item.cpp L230](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Gameplay/Item.cpp#L221-L232)). It's called from the HUD, Facility, the flashlight and quests, so a probe for an id that doesn't exist could spam the log every frame (suspected).
  - **Fix:** surface the missing file once (a content issue or a red banner), and use a quiet `tryFindItemDef` for probes.

- **N-10: The spawner "Unit" combo only knows Hunter, Wolf and Crow.** A mod unit shows as "Hunter" and is overwritten with `"hunter"` if you touch the combo ([EditorUi.cpp L940-L950](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Editor/EditorUi.cpp#L940-L950)). Build the list from archetypes that have an AI behavior.

### Suspected

- **S-1 (P3): Editor NPC profile order.** `attachEditorNpc` calls `attachHunter` before any profile is assigned and before the wolf tag is set ([EditorPlay.cpp L139-L144](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Editor/EditorPlay.cpp#L139-L144)). A wolf without `ArchetypeParamsComponent` gets its **poise** from the hunter profile. The ContentBuiltin path assigns the profile first, which is the right order.
- **S-2 (P3): The replication cap is used offline.** Sandbox boot now registers every replicated archetype (hunters, wolves, any `net.replicate` mod archetype) with `NetworkSystem` ([SandboxBoot.cpp L95-L104, L1123, L1508](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Sandbox/SandboxBoot.cpp#L95-L104)). That happens even in the Idle role, and the cap is `kNetMaxReplicated = 32`. Today's scenes top out at about 10 hunters, but a mod scene with ~30 NPCs would make the host's `PlayerPawn` registration fail.
- **S-3 (P3): The physics fallback guesses by kind.** Sandbox respawn picks `"human"` or `"wolf"` physics with `isInteractGuard` ([SandboxSave.cpp L251](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Sandbox/SandboxSave.cpp#L247-L253)). It's mostly moot because `Physics.file` is loaded first, but it's another name heuristic.
- **S-4 (P3): Settings page input.**
  - The control lists and the volume slider sit in `NoNavInputs` windows ([ImGuiGameChrome.cpp L354-L358](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Ui/ImGuiGameChrome.cpp#L341-L388)), so keyboard and gamepad users can't scroll the list or change the volume.
  - `settingsControls()` caches the guide in a `static` that never reloads after a rebind ([L309-L330](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Ui/ImGuiGameChrome.cpp#L309-L330)).
- **S-5 (P3): `healthFromArchetypeFile` ignores the namespace.** When the DB doesn't know an id, it reads `archetypes/<bare>.archetype.json` ([CharacterAttach.cpp L208-L231](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Play/CharacterAttach.cpp#L208-L231)). So `mymod:wolf` could silently get core wolf health. It's also a second archetype parser.

### Checks with no findings
- **ImGui pairing:** the new Settings page balances `BeginChild`/`EndChild`, `BeginTable`/`EndTable`, `PushFont`/`PopFont`, and the style pushes (5 colours, 4 vars).
- **No-exceptions:** every new `json::parse` uses `(nullptr, false)`, and every `get<std::string>()` is guarded by `is_string()`. I found no `throw`, `try` or `.at(`.
- **D3D12 lifetime:** no new GPU resources. `attachCrowBody` reuses the pinned crow pose models, and a failed spawn goes through `destroySpawn`.
- **HLSL/C++ layouts:** no shader or constant buffer changes.
- **Hot reload:** `reloadContentDatabase` clears the entity master cache and the combat profile table. No code holds an `EntityCombat&` across frames. The item and status catalogs reload from the Editor poll.
- **Threading:** the profile table and the weather and controls statics are touched from the main thread only, as far as I can see.

---

## 4. New duplication from this push

1. **`SpawnServices` mirrors `ArchetypeSpawnHost` field for field** [SpawnHost.h L9-L33](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Play/SpawnHost.h#L9-L33) vs `ContentDb.h`. It's still filled by hand in 5 places [SandboxBoot.cpp L1099, L1144](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Sandbox/SandboxBoot.cpp#L1099-L1160), SandboxSave L226, SpawnListed L74, EditorSpawn L455, each with the same `const_cast<AiSystem*>`. **Fix:** a `SandboxApp::spawnServices()` and an `EditorApp::spawnServices()` that return one filled struct, or just pass `ArchetypeSpawnHost`.
2. **Hard-coded `Health` 48 / 5 / 4 for adopted legacy `.entity.json` archetypes** ([ContentDb.cpp L416-L423](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/content/ContentDb.cpp#L416-L423)) repeats the hunter and wolf archetype values in C++.
3. **Three JSON number helpers**: `numberAt` ([ParticleEmitterJson.cpp L11](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Particles/ParticleEmitterJson.cpp#L11)), `cloudJsonNumber` ([CloudVolume.cpp L190](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Sky/CloudVolume.cpp#L190)) and `doubleAt` in SceneObjectSpawn. There are also two diff-baseline implementations: `diffBaseline` and `sceneWriteCloudDiff` ([CloudVolume.cpp L276](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Sky/CloudVolume.cpp#L276)). Move them to `Core/Json.h`.
4. **`ControlGuide` keeps the action descriptions in a C++ table** ([ControlGuide.cpp L198](https://github.com/DarkenedGit/DarkEngine6/blob/de155eb1f11bcfe2d29ebc3b6e8376f66d878d87/Scene/ControlGuide.cpp#L198)). They belong in the input JSON as `"label"`, so mod actions get text.
5. **Layering:** `Sky/CloudVolume.cpp` now includes `Content/SceneObjectSpawn.h`, and `Scene/EntityCombat.cpp` includes Content, ECS and Character. The Scene and Sky layers now depend on Content.

---

## 5. Suggested next patch

1. N-1, N-3 and N-5 (small, data safety).
2. N-2 and N-4 (mod correctness: capability checks instead of ids and tags).
3. N-7 (cache the profile index).
4. The 093f812 UI batch: `NoNav` on dialogue, `repeat=false` on Esc and Enter, scroll only on selection change, a min-width guard, the drag fix, saving master volume.
5. Then D8 (shared HLSL headers) and H2/H3 (audio cues, status rows).

*Static review of the tarball at `de155eb`. Nothing was compiled or run.*
