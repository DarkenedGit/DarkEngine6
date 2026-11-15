# DarkEngine6 — Data-driven content and modding plan

Checked against `main` @ [`4431dcd`](https://github.com/DarkenedGit/DarkEngine6/tree/4431dcd57d64a72954c65b7828d5e61edb6a50e2) ("Better foliage", Nov 14 2026 8:30 PM PST). Static read only; nothing compiled or run.

Link base: `B = https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2`

---

## 1. TL;DR

* `SceneObjectType` is a 19-value `uint8_t` enum ([Scene/SceneTypes.h L47-69](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Scene/SceneTypes.h#L47-L69)). Outside tests it is referenced **305 times in 25 source files**: **7 `switch` statements (71 `case` labels)**, **128 `==`/`!=` comparisons** in if-chains, and a 19-branch string parser.
* Its partner `SceneObjectData` ([L232-345](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Scene/SceneTypes.h#L232-L345)) is a **"fat union" DTO**: about 110 fields covering particle, light, cloud, stream, AI, pickup, movie and spawner data on every object, gated by `hasX` flags and the type.
* **Two parallel spawn paths** each re-implement the type dispatch: the Editor's `spawnObject` / `placeAtWorld` ([Editor/EditorSpawn.cpp L222-540](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Editor/EditorSpawn.cpp#L222-L540)) and the game's boot classifier ([Sandbox/SandboxBoot.cpp L1016-1033](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Sandbox/SandboxBoot.cpp#L1016-L1033)). Sandbox2D has a third one ([Sandbox2D/Sandbox2DApp.cpp L196-206](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Sandbox2D/Sandbox2DApp.cpp#L196-L206)).
* **Worst failure mode for mods:** an unknown `type` string is **skipped on load** ([Scene/SceneFile.cpp L1433-1437](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Scene/SceneFile.cpp#L1433-L1437)). If you open a mod's scene in the Editor and save it, the objects the Editor doesn't recognise are **silently deleted**.
* **Good news, there are already foundations to build on:**
  * Saves already key respawn on an **archetype string**, not the enum ([Save/PersistentId.h L26](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Save/PersistentId.h#L26), [Save/SaveSystem.cpp L610](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Save/SaveSystem.cpp#L610)).
  * Components already have a **reflection-lite save hook**, `PersistFns`, auto-detected via `T::kPersist` ([ECS/Persist.h](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/ECS/Persist.h), [ECS/World.h L216-221](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/ECS/World.h#L216-L221)). 84 components have a `kTypeName`; 42 have `kPersist`.
  * There is an `EntityMaster` record per type, `content/entities/<type>.entity.json`, with 30 files ([Scene/EntityMaster.h](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Scene/EntityMaster.h)).
  * There is a **multi-root content resolver** (`contentRootCandidates()`, first match wins: [Core/ContentRoots.h](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Core/ContentRoots.h)). That is the seed of a mod overlay.
  * Upgrades, quests, throwables, skills, AI awareness and attacks, physics surfaces and buildings are already JSON.
* **Still hard-coded:** the item table (`kItems[]`, [Gameplay/Item.cpp L8-22](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Gameplay/Item.cpp#L8-L22)), status effects (`kStatusCatalog`, [Combat/StatusCatalog.cpp L7](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Combat/StatusCatalog.cpp#L7)), the upgrade effect verbs ([Gameplay/Upgrades.cpp L113-121](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Gameplay/Upgrades.cpp#L113-L121)), spawner units (`SpawnerUnit` Hunter/Wolf/Crow, [Play/Spawner.h L20-25](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Play/Spawner.h#L20-L25)) and network prefabs (`NetPrefab`, 6 values, [Network/NetTypes.h L65-73](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Network/NetTypes.h#L65-L73)).
* **Proposal:** JSON **archetypes** (prefabs) built from a **component registry**, plus a **behavior registry** for native C++ logic referenced by name. **Content packages** go in `mods/<id>/` with a manifest, load order and overrides. **Stable hashed IDs** cover saves and network. The Editor palette and inspector are generated from the registry. The enum survives only as a legacy alias table.
* **Effort:** about 6 phases. P0-P2 (S/M/M) already unblock "add a new prop/creature/item with no recompile". The full set is roughly L overall.

---

## 2. What `SceneObjectType` touches today

### 2.1 Definition and helpers — `Scene/SceneTypes.h`
| What | Where |
|---|---|
| Enum (19 + `Count`) | [L47-69](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Scene/SceneTypes.h#L47-L69) |
| `isScene3DType` / `isPawnType` / `usesModelBounds` / `isGlobalLightType` / `isScene2DType` | [L71-99](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Scene/SceneTypes.h#L71-L99) |
| `toString` switch | [L101-126](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Scene/SceneTypes.h#L101-L126) |
| `tryParseSceneObjectType`, a 19-branch if-chain with aliases (`enemy`→Hunter, `sun`→DirectionalLight, `gltf`→Model…) | [L128-226](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Scene/SceneTypes.h#L128-L226) |
| `SceneObjectData` fat DTO: particle (~45 fields), light, cloud, stream, AI (patrol/squad), gameplay (conceal/goal/circuit/zone/pickup/dialogue), movie, spawner | [L232-345](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Scene/SceneTypes.h#L232-L345) |

### 2.2 Use counts (non-test, non-doc source)
| File | Refs | Role |
|---|---:|---|
| Scene/SceneTypes.h | 49 | definition, string map, predicates |
| Editor/EditorSpawn.cpp | 44 | Editor factory (`spawnObject`, `placeAtWorld`, default palette) |
| Editor/EditorUi.cpp | 36 | create menu + per-type inspector |
| Editor/EditorApp.cpp | 28 | `meshForType` switch, icons |
| Editor/EditorAssetBrowser.cpp | 24 | palette tiles, model-stem → pawn guess |
| Editor/EditorInternals.cpp | 22 | 2D defaults, net prefab maps, replicated-prop predicate |
| Editor/EditorTypeIcon.h | 15 | icon switch |
| Scene/SceneFile.cpp | 14 | JSON load/save gating |
| Sandbox/SandboxBoot.cpp | 12 | game boot classifier |
| Editor/EditorSceneFile.cpp | 11 | load into Editor, hotkeys 1-8 |
| Editor/EditorPlay.cpp, EditorPhysics.cpp | 10 each | play-in-editor, physics key |
| Scene/SceneSpawn.cpp | 7 | shared runtime instantiation |
| others (Building, WalkObstacles, Sandbox2D, EditorTerrain, EditorRender*, EditorSave, EditorNet, EditorModel, EditorObject.h, EditorApp.h) | 1-4 each | |
| **Total** | **305** in 25 files | plus **159 refs in 10 test files** (SceneFileTests alone has 102) |

### 2.3 Switches (7) and big if-chains
| # | Switch | Link |
|---|---|---|
| 1 | `toString` | [SceneTypes.h L103](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Scene/SceneTypes.h#L103) |
| 2 | `defaultScale2D` | [EditorInternals.cpp L62](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Editor/EditorInternals.cpp#L62) |
| 3 | `prefabFromType` (enum→NetPrefab) + inverse `typeFromPrefab` | [EditorInternals.cpp L218-251](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Editor/EditorInternals.cpp#L218-L251) |
| 4 | `defaultColor2D` | [EditorInternals.cpp L271](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Editor/EditorInternals.cpp#L271) |
| 5 | `iconForType` | [EditorTypeIcon.h L13](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Editor/EditorTypeIcon.h#L13) |
| 6 | `meshForType` | [EditorApp.cpp L366](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Editor/EditorApp.cpp#L366) |
| 7 | `entityTypeKey` (type→EntityMaster key; Hunter is missing, so it falls back to the model stem) | [EditorPhysics.cpp L182-190](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Editor/EditorPhysics.cpp#L182-L190) |

The largest if-chains:
* `tryParseSceneObjectType` (19 branches)
* `spawnObject`, with 7 `xxxType` bools and per-type attach calls ([EditorSpawn.cpp L245-421](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Editor/EditorSpawn.cpp#L245-L421))
* `placeAtWorld`, which sets per-type defaults ([L484-538](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Editor/EditorSpawn.cpp#L484-L538))
* the inspector chain Pawn/Model/Spawner/Movie/Water/Stream/Cloud ([EditorUi.cpp L803-1125](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Editor/EditorUi.cpp#L803-L1125))
* the create menu (16 hard-coded `MenuItem`s, [EditorUi.cpp L205-295](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Editor/EditorUi.cpp#L205-L295)) and the asset browser palette (16 `drawPrimitive` calls, [EditorAssetBrowser.cpp L317-354](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Editor/EditorAssetBrowser.cpp#L317-L354))
* the scene JSON write/read gating ([SceneFile.cpp L777-874](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Scene/SceneFile.cpp#L777-L874), [L1542-1709](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Scene/SceneFile.cpp#L1542-L1709))
* the game boot classifier ([SandboxBoot.cpp L1016-1033](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Sandbox/SandboxBoot.cpp#L1016-L1033))

### 2.4 By subsystem
| Subsystem | Coupling |
|---|---|
| **Scene JSON** | Parser rejects unknown types (skip + warn). Payload blocks are written when `hasX \|\| type==X`. Defaults depend on type (spot intensity, directional `π`). |
| **Save game** | Already string-based: `PersistentIdComponent::archetype[24]`, respawned through `SaveHost::respawn(archetype)`. The Editor host maps it back with `tryParseSceneObjectType` ([EditorSave.cpp L43-44, L164-169](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Editor/EditorSave.cpp#L160-L175)). The 23-char cap truncates silently ([SaveSystem.cpp L193-194](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Save/SaveSystem.cpp#L193-L194)), which would break namespaced mod IDs. |
| **Spawning/factories** | 3 separate dispatchers (Editor, Sandbox boot, Sandbox2D) + shared `instantiateSceneObjects`/`applySceneActor` with type checks ([SceneSpawn.cpp L38-200](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Scene/SceneSpawn.cpp#L38-L200)). Hunter dialogue is looked up by the literal `"hunter"` in 4 places. |
| **ECS setup** | No archetype concept. Components are attached imperatively per type (`attachEditorPlayer/Hunter/Wolf`, light/emitter/cloud setup). `EditorObjectComponent` stores the enum ([EditorObject.h L19](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Editor/EditorObject.h#L19)). |
| **Editor UI** | Create menu, palette, hotkeys 1-8 ([EditorSceneFile.cpp L650-664](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Editor/EditorSceneFile.cpp#L650-L664)), icon array `m_typeIcons[Count]` ([EditorApp.h L500-503](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Editor/EditorApp.h#L500-L503)), inspector chain, and the model-filename guess (`wolf`/`human`/`skeleton` → pawn, [EditorAssetBrowser.cpp L83-105](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Editor/EditorAssetBrowser.cpp#L83-L105)). |
| **Network** | `NetPrefab` is a fixed `uint8_t` enum in the wire format ([Protocol.h L139](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Network/Protocol.h#L139)). Only Cube/Sphere/Platform/Coin replicate (`isReplicatedProp`). Everything else is sent as `Cube`. |
| **Physics** | Solid = Cube/Sphere/Model by type ([EditorPhysics.cpp L113, L363](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Editor/EditorPhysics.cpp#L113)). Physics file comes from EntityMaster when the key maps. Walk obstacles are classified by type ([WalkObstacles.h L25-40](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Scene/WalkObstacles.h#L25-L40)). |
| **AI / combat** | Not keyed on `SceneObjectType` directly, but the pawn kind is: Hunter/Wolf decide brain, attacks file, combat JSON (`content/ai/hunter.attacks.json`, `wolf.attacks.json`). `SpawnerUnit` duplicates the creature list. |
| **Items / loot** | `pickup` is a free string on any object → `findItemDef` ([SceneSpawn.cpp L63](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Scene/SceneSpawn.cpp#L63)), which is good. But the item table itself is compiled in. |
| **Assets** | Model path is per-object; pawns resolve gltf/anim/physics via EntityMaster, which is re-read from disk on every call (no cache). |

### 2.5 What each type implies today
| Type | Components / assets / behavior it implies |
|---|---|
| Cube, Sphere | Mesh (built-in), prop material, box/sphere physics body, walk obstacle, net prefab, `block` flag |
| Platform, Coin, Spawn | 2D only; default scale and colour; Box2D in Sandbox2D; net prefab |
| ParticleEmitter | `ParticleEmitterDesc` from ~45 DTO fields; optional light |
| Point/SpotLight | `LocalLightComponent`; spot gets default rotation; `searchlight` → SecurityCamera |
| Ambient/DirectionalLight | global singletons, no mesh, not stamped for save |
| Player | player EntityMaster, input/movement/combat JSON, play start, camera |
| Hunter | EntityMaster, HSM, attacks, dialogue (`hunter`), patrol/squad, awareness |
| Wolf | EntityMaster, animal brain, attacks |
| Model | any glTF + sidecar physics |
| Water, Stream, CloudVolume | terrain/water/sky subsystems, own payloads |
| Spawner, Movie | `SpawnerDesc` / movie cue disc |

---

## 3. How gameplay content is defined today
| Content | Source | Data-driven? |
|---|---|---|
| Items | `kItems[]` C++ table, 13 rows, `ItemType`/`EquipSlot` enums ([Item.cpp L8-22](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Gameplay/Item.cpp#L8-L22), [Item.h](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Gameplay/Item.h)) | **No** |
| Upgrades | `content/upgrades.json` | Data yes, **effects no**: 5 verbs hard-coded in `combineUpgrades` |
| Quests | `content/quests/*.quest.json` (reach/collect objectives) | Yes (objective types are fixed) |
| Throwables | `content/items/throwables.json` | Yes |
| Skills | `content/skills/*.json` | Yes |
| AI | `content/ai/*.json` (HSM, attacks, awareness, barks) | Yes |
| Status effects | `kStatusCatalog` + `StatusId` enum (9) | **No** |
| Dialogue | `content/dialogue/*.dlg.json` | Yes |
| Buildings | `content/buildings/*.building.json` + kit | Yes, but expands to `Cube`/`Hunter` objects ([Building.cpp L94, L415, L443](https://github.com/DarkenedGit/DarkEngine6/blob/4431dcd57d64a72954c65b7828d5e61edb6a50e2/Scene/Building.cpp#L94)) |
| Entity masters | `content/entities/*.entity.json` (gltf/anim/physics/hsm/dialogue) | Yes, but only looked up for a few hard-coded keys |
| Spawner units | `SpawnerUnit` enum | **No** |
| Starting loadout | literal `findItemDef("sword")`, `("rifle")`, `("flashlight")` (PlaySession.cpp, PlayerFlashlight.cpp) | **No** |

---

## 4. Proposed design

### 4.1 Concepts
1. **Archetype** (prefab): a JSON file with a namespaced string ID. It lists the components to attach, with default values, a list of behaviors, Editor metadata, and optional `base` inheritance.
2. **Component registry:** one `ComponentInfo` per *data-exposed* component. It holds the name, a JSON read/write function pointer, an Editor inspect function, an optional net-replication function, and an `emplace` thunk. It extends the existing `PersistFns` pattern rather than adding a second reflection system.
3. **Behavior registry:** native C++ systems/attachers registered by name (`"ai.hunter"`, `"ai.wolf"`, `"player.controller"`, `"light.searchlight"`, `"pickup"`, `"spawner"`). An archetype references them by name and passes parameters. Lua or another scripting layer can come later and register through the same table.
4. **Content package:** `content/` (the base game) plus `mods/<id>/`, each with a `package.json` manifest. Packages are layered in load order. They can add new IDs or override existing ones (whole-file replace in v1, JSON merge-patch in v2).
5. **Stable IDs:** `ContentId = FNV-1a/xxHash64("<package>:<name>")`, computed at load time. Saves and scenes store the **string**. The network sends the **64-bit hash**, or a session-local `uint16` index agreed at handshake.

### 4.2 File shapes (sketch)
```jsonc
// content/archetypes/wolf.archetype.json
{
  "id": "core:wolf",
  "version": 1,
  "base": "core:pawn_base",
  "editor": { "label": "Wolf", "category": "Creatures", "icon": "bug", "modes": ["3d"] },
  "components": {
    "Model":     { "gltf": "models/wolf.gltf", "anim": "models/wolf.anim.json" },
    "Physics":   { "file": "models/wolf.physics.json" },
    "Combat":    { "attacks": "ai/wolf.attacks.json" },
    "Health":    { "max": 60 }
  },
  "behaviors": [ { "name": "ai.animal", "params": { "pack": true } } ],
  "net": { "replicate": true }
}
// mods/better_wolves/package.json
{ "id": "better_wolves", "version": "1.2.0", "engine": ">=6.0", "depends": ["core"], "loadAfter": ["core"] }
```
Items get the same treatment: `content/items/*.items.json`, one array of `ItemDef`s with `type` and `slot` strings.

Scene objects change from `"type":"wolf"` plus a fat field set to `"archetype":"core:wolf"` plus an `"overrides":{ComponentName:{…}}` block. The reader keeps accepting the old shape (see §4.6).

### 4.3 Runtime shape (C++23, no exceptions)
```cpp
struct ContentId { uint64_t hash; };                 // trivially comparable
struct ComponentInfo {
    std::string_view name;   uint16_t version;
    bool (*read)(World&, Entity, const json&, ContentError&);   // from archetype / override
    void (*write)(const World&, Entity, json&);                  // scene save, only diff vs archetype
    void (*inspect)(World&, Entity);                             // Editor, optional
    bool (*has)(const World&, Entity);
};
struct BehaviorInfo { std::string_view name; bool (*attach)(World&, Entity, const json& params, ContentError&); };
struct Archetype { ContentId id; std::string name; ContentId base; std::vector<CompiledComponent> comps; std::vector<CompiledBehavior> behaviors; EditorMeta editor; uint32_t packageIndex; };
class ContentDb {  // built once at boot / on hot reload
    std::expected<const Archetype*, ContentError> find(ContentId) const;   // flat hash map
    const Archetype* find(std::string_view) const;                          // load/Editor only
};
```
* **Error handling:** `std::expected<T, ContentError>` (C++23) or `bool` + `ContentError&` out-param, matching the existing `parseX(..., std::string* error)` style. nlohmann is already used with `parse(..., false)`. All failures are reported in a load report and the **Editor shows it**. One broken mod archetype disables only that archetype, not the package and not the game.
* **Performance:** strings are resolved **only at load/spawn**. Archetypes are compiled to a flat array of `{ComponentInfo*, preparsed blob}`, so instancing is a memcpy-ish `emplace` per component. No string lookups per frame; systems keep iterating typed pools as today. Cache `EntityMaster`/archetype lookups. Today `loadEntityMasterType` hits the disk on every call.
* **Unknown data is never lost:** an object whose archetype is missing loads as an inert **placeholder entity** that keeps its raw JSON, is drawn with a "missing" icon, and is written back unchanged. This fixes the delete-on-save problem.

### 4.4 Behaviors and what stays native
* Mods **can**: add new props, creatures, lights and pickups by **combining existing components and behaviors**; retune any data; add items, upgrades, quests, dialogue, throwables, AI HSMs and attacks; add scenes; and override base content.
* Mods **cannot** (without code): add a **new component type**, a new behavior, a new upgrade effect verb, a new quest objective type or a new status effect mechanic. Those need C++ registered in the registry. Later options:
  * (a) **Lua** (sol2 has exceptions, so check that it builds with `/EHs-c-` and `SOL_NO_EXCEPTIONS`, or use plain Lua C API / luau) as a scripted behavior type;
  * (b) native mod DLLs. Not recommended: ABI fragility and security.
* Upgrade effects become **stat modifiers** (`{"stat":"move.speed","op":"mul","value":1.1}`) against a registry of named stats. That removes the hard-coded verb chain.

### 4.5 Packages, load order, versioning, hot reload
* **Mount order:** `content/` (core), then `mods/*` sorted by the manifest's `loadAfter`/`depends` (topological), then the user's `mods.json` enable list. This extends `contentRootCandidates()` and `AssetManager::mountContentRoots()`, which already do "first root wins". Mod roots need to be **prepended**, and the authoring root kept separate.
* **Override rules:**
  * Same ID in a later package replaces the earlier one, logged as an override.
  * A new ID must be namespaced (`modid:name`).
  * Core IDs can be written without a namespace (`wolf` → `core:wolf`) for back-compat.
* **Versioning:**
  * Each archetype and component has a `version`. `ComponentInfo::read` gets the version and migrates, the same as `PersistFns::apply` does today.
  * Packages declare `engine` compatibility.
  * Saves record the active package list and version. On load, a missing package gives a warning, and its entities load as placeholders.
* **Hash collisions:** at `ContentDb` build time, detect two different strings with the same hash, refuse the second, and report it. Use 64-bit to make collisions astronomically unlikely, but still check.
* **Hot reload (Editor):** watch `content/` and `mods/`, rebuild `ContentDb`, then re-instantiate entities by archetype while keeping per-instance overrides. Play mode doesn't hot reload in v1.

### 4.6 Backward compatibility
* Keep `SceneObjectType` as a **legacy alias table**: `"cube"→core:cube`, `"enemy"`→`core:hunter`, etc. That's the current `tryParseSceneObjectType` strings, moved into `content/archetypes/legacy_aliases.json` or a static table.
* **Scene v2 → v3 reader:** the old `type` + flat fields are read into the matching archetype plus overrides. The flat fields map onto component names (particle → `ParticleEmitter`, `hasLight` → `LocalLight`, …). Write v3 only, and add a one-shot "Upgrade scenes" Editor command plus a CLI converter.
* **Saves:** `archetype` is already a string. Accept both `"wolf"` and `"core:wolf"`, and raise the 24-char cap: make it `std::string` or a `ContentId` + string in the JSON row.
* **Network:** bump the protocol version. Replace `uint8 prefab` with a `uint16` session archetype index exchanged at join; reject a join if the archetype tables differ, i.e. the mod sets don't match.

---

## 5. Phased migration
| Phase | Scope | Effort | Unblocks |
|---|---|---|---|
| **P0 — Stop losing data** | Unknown `type` → placeholder that round-trips raw JSON. Cache `EntityMaster`. Raise the save archetype cap. Fix `entityTypeKey` missing Hunter. | **S** | Safe to open mod/newer scenes |
| **P1 — Items, statuses, upgrades to data** | `kItems` → `content/items/*.items.json`. `ItemType`/`EquipSlot` stay enums but are parsed from strings. Upgrade verbs → named stat modifiers. `SpawnerUnit` → archetype ID string. Starting loadout → JSON. | **M** | New items, loot, upgrades, spawnable creatures with no recompile |
| **P2 — Component registry + archetypes (runtime)** | `ComponentInfo` for the ~15 components the 19 types use. `ContentDb`, `content/archetypes/*.json` for all 19 legacy types, alias table. One `instantiateArchetype()` replaces the SandboxBoot classifier, `instantiateSceneObjects` and Editor `spawnObject` dispatch (behaviors: `ai.hunter`, `ai.wolf`, `player.controller`, `light.searchlight`, …). | **L** | New props/creatures from JSON |
| **P3 — Scene format v3** | Archetype + overrides per object. v2 reader kept, converter, `SceneFileTests` updated (102 refs). Building kit emits archetype IDs. | **M** | Clean files; fat DTO retired |
| **P4 — Editor from registry** | Create menu, palette, icons, hotkeys and inspector generated from `ContentDb` + `ComponentInfo::inspect`. Delete the 7 switches and the inspector chain. Model-stem guess → `editor.matchModel` in archetypes. | **M** | Mod content appears in the Editor automatically |
| **P5 — Packages + net + hot reload** | `mods/` mount + manifest + load order + overrides, load report UI, save package list, net archetype table at handshake, Editor hot reload. | **M-L** | Real modding / expansions |
| **P6 (optional) — Scripting** | Lua/luau behaviors with a no-exceptions binding. | **L** | Mod logic without C++ |

Recommended order: P0 → P1 → P2 → P3 → P4 → P5. P0 and P1 can ship independently this week; P2 is the big one and should land behind a flag (`--archetypes`) with both paths kept until tests match.

### Risks
* **Behavior parity** in P2: the Editor and Sandbox spawn paths differ subtly today (Editor stamps, net registration, 2D defaults). Mitigate with golden tests that spawn each legacy type both ways and compare component sets.
* **SceneFileTests churn** (102 enum refs) makes P3 noisy. Convert the tests to archetype IDs mechanically.
* **Inspector UX** regresses if every type gets a generic property grid. Keep custom inspect functions for Water/Stream/Cloud/Spawner (the Phase 4 `ImGuiWidgets` property grid from the UI plan fits here).
* **Network:** mismatched mod sets → desyncs. Hard-reject at join.
* **Boot cost:** parsing many archetype JSONs. Fine at this scale; add a cooked binary cache later if needed.
* **Building kit and terrain foliage** also emit hard-coded kinds (foliage kinds are a separate system and are out of scope here, but worth the same treatment later).

### Tests to add
* `ContentDbTests`: load, override order, namespacing, hash collision rejection, missing base, cyclic base, bad component field → error, not crash.
* `ArchetypeSpawnTests`: each of the 19 legacy types spawns the same component set via the legacy and archetype paths.
* `SceneMigrationTests`: every file in `content/scenes/` loads v2 → saves v3 → reloads identical, and unknown archetypes round-trip byte-for-byte.
* `SaveCompatTests`: an old save with `"archetype":"wolf"` respawns as `core:wolf`, a long namespaced ID isn't truncated, a missing package gives a placeholder.
* `ItemCatalogTests` and `UpgradeModifierTests`: JSON items match the old `kItems`, and upgrade stacking matches `combineUpgrades`.
* `NetArchetypeTableTests`: handshake rejects mismatched tables (FakeTransport).

---

## 6. Open questions for Travis
1. **Mod scope:** data-only mods first (recommended), or do you want scripting (Lua/luau) on the roadmap now?
2. **Where mods live:** `<exe>/mods/` or `%LOCALAPPDATA%/DarkEngine6/mods/`, or both? Is there a Steam Workshop-style goal?
3. **Override granularity:** is whole-file replace OK for v1, or do you need JSON merge-patch (change one field of `core:wolf`) from day one?
4. **Scene format:** OK to write v3 only and auto-upgrade v2 on load, or must the Editor keep writing v2 for a while?
5. **Network:** must modded clients interoperate with unmodded hosts, or is "same mod set or no join" fine?
6. **Items:** should items become full archetypes (world pickup model + inventory def in one file) or stay a separate item catalog referenced by pickups?
7. **Sandbox2D:** migrate it to archetypes too, or leave the 2D types as legacy?
8. **Save compatibility:** do existing save files need to keep loading, or can saves be reset when P2 lands?

---
*Static analysis only. Counts are from `rg` over the tarball at `4431dcd` (third_party excluded); switch counts cover `switch` on a `SceneObjectType` value plus the NetPrefab inverse map.*
