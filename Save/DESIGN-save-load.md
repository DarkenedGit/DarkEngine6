# Save / load — progress overlay discovered from the ECS

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-10-03 |
| **Supersedes** | Rev 1 of this file (registry of serializers, scene-file v3 ids, host/systems blobs) |
| **Tip baseline** | `78bd84ea9cddc86b80992ca38415ef2e37b4b520` (`main`, "Pix Profiling Enabled"). Implemented against later `main`. |
| **In-tree home** | `Save/DESIGN-save-load.md`. The draft remains at `docs/plans/darkengine6-save-load-design.md`. |
| **Depends on** | `ECS/World`, `ECS/Component.h`, `Core/UUID`, `Core/Paths`, `Scene/SceneFile` (read the start state only), `third_party/nlohmann/json.hpp`, `Core/Version.h`, `NetworkSystem::role()` |
| **Out of this document** | No code. Phases below land as small sequential PRs. |

## Purpose

A save records **progress of a run**. It is the state of the simulation after the player has moved, fought, looted, and left things dead or moved.

`SceneFile` / `level.json` is a different file with a different job. It is the level editor's authored **starting point**: what Play and a new game build before anyone has played. Scene JSON stays version 2. The save system never writes it, never upgrades it, and never stores meshes, terrain, entity masters, `*.hsm.json`, or `*.anim.json`.

Load means: build that starting point the way a new game already does, then **overlay** the progress file on top.

## Why discovery, not a catalog

Rev 1 owned a `SaveRegistry` and a table of component keys (`Transform`, `Health`, `Brain`, …) that hosts registered by hand, plus a `systems` blob and a host blob for fields that are not components. Every new piece of progress meant editing that catalog.

That does not match this ECS.

- A component is a plain value type. `ECS/Component.h` says no inheritance. There is no component vtable to call.
- `ComponentPool<T>` already exists for every type that has been `emplace`d. `World::forEachPool` walks them.
- `IComponentPool` today can only `remove`, and report `typeName` / `count` / `stride` / bytes. It cannot see values. `componentID<T>()` is a process-wide first-use counter, so it must not appear in a file. `kTypeName` is already the stable name (`"Health"`, `"AiAgent"`, …).
- `World::emplace` replaces in place. Load relies on that.
- Pools are not thread-safe. Capture stays on the main thread. The worker only sees JSON.

So the save system does not keep a list of types. A component **opts in beside its own type**. The pool installs that opt-in when it is created. Save walks whatever pools the live world actually has. A type with no opt-in is skipped, which is how editor metadata, GPU ids, and cosmetic runtime stay out. A type that opts in writes **only the fields it wants**; the rest stay at the values the starting point just installed.

Adding `Coin` progress later is a `kPersist` on `CoinComponent` and a `.cpp` next to it. `SaveSystem` does not change.

## Engine facts that constrain the mechanism

| Fact | What the design does with it |
|---|---|
| `EntityID` is index + generation, and slots are recycled (`ECS/World.cpp`) | Files store a persistent id, never an `EntityID`. |
| `componentID<T>()` depends on first use (`ECS/Component.h`) | Files store `kTypeName` strings. |
| `AssetID` is `AssetManager::m_nextID++` | Files store content paths only if a component chooses to write one. They never store an `AssetID`. |
| `IComponentPool` has no value iterator | The pool gains a type-erased `visit` and a `const PersistFns*`, set from `T::kPersist` inside `getOrCreatePool<T>`. |
| `SceneObjectData` has no id (`Scene/SceneTypes.h`, file version 2). `emissiveMeshIndex` is an index into `objects[]` | The scene loader stamps a persistent id when it creates the entity. It does not write that id back into the scene. |
| Sandbox pawns and props are created in code (`SandboxApp::onInit`, `PathChase`), not from `level.json` | Those spawn sites stamp a persistent id, the same way they already `emplace` a transform. |
| `Brain`, `WeaponLoadout`, and `HsmGraphInstance` sit behind `unique_ptr` and are not copied (`AI/AiComponents.h`, `Weapons/WeaponLoadoutComponent.h`) | Capture reads fields into JSON. Load calls a restore method on the live object. The pool is never memcpy'd. |
| `LiveProjectile` lives in `ProjectileWeapon::m_shots` inside the loadout | The weapon component writes the shots. They are not entities and need no id. |
| Look, offhand, cloud time, and the AI clock are still app or system members (`SandboxApp::m_lookYaw`, `m_lowerBodyYaw`, `m_offhand`, `m_cloudTime`; `EditorApp` has the same shape; `AiSystem::m_time`, `m_packAttackGap`, `m_jumpAttackToken`) | The walker cannot see them. Progress that should survive moves onto a component the walker already finds. See *State that is not a component*. |
| `AnimPlayer::m_lowerBodyYaw` is private on the graph instance | `AnimGraph` save/load reads and writes it. The app member is a cache of that value, not a second source of truth. |
| `tickAnimGraphs` skips `dt == 0` | After overlay, the host evaluates the pose once without advancing time. |
| `forEachPool` and `destroyEntity` already walk every pool | Save uses the same walk. Destroyed baseline entities are removed with the normal `destroyEntity` path (and `unregisterEntity` when `NetworkedComponent` is present). |
| nlohmann `get<T>()` throws `type_error` unless `JSON_NOEXCEPTION` is set. The build does not set it. `SceneFile` still calls `get<float>()` in places. | `Save/` and every `loadProgress` go through `SaveReader`, which checks the type first. No exceptions (`AGENTS.md`). |
| `executableDirectory()` gives up when `GetModuleFileNameW` fills the buffer (`Core/Paths.cpp`) | The path PR grows the buffer (retry while the result equals the buffer size, up to 32 768). |
| Editor F5 / Ctrl+S calls `saveSceneWithDialog()` with no `!m_playMode` guard (`EditorSceneFile.cpp`) | Play mode must not write the authored scene. Progress save is a different command. Fix the guard in the same PR that binds play quicksave. |
| Save is host-authoritative (`Network/DESIGN.md`, `kNetMaxReplicated = 32`) | `Client` / `Joining` cannot save. Load while hosting requires `peerCount() == 0`. |

## Goals (v1)

1. Walk the live world. For each pool whose type opted in, write the fields that type asked to keep. Apply them back onto a freshly built starting world.
2. One JSON file per save under `<exeDir>/Saves/`, from `executableDirectory()`, never the process CWD. Each save has its own file name. Manual saves are not deleted automatically.
3. Join entities by persistent id. Entity references in components are persistent ids in the file and `Entity` handles again after load.
4. Every call returns `SaveResult`. JSON readers never throw. Writes are atomic.
5. Capture on the main thread at a sim boundary. Encode and I/O run on one worker thread.
6. Older component payloads still load. A file written by a newer engine is refused. Unknown component keys and unknown fields are kept as warnings, not failures.

## Non-goals (v1)

- Replacing `SceneFile`, bumping scene JSON to v3, or writing ids back into `level.json`.
- Bit-exact replay. A load should look and play the same. Later frames may diverge.
- Saving editor-only components (`EditorObject`, `EditorAuthoredPose`), GPU caches, `AssetID`s, voice ids, particle simulations, or network interp state. Those types simply do not opt in.
- Terrain sculpt, terrain paint, and water edits as progress. Terrain comes from the starting scene.
- Cross-scene inventory, streaming multi-level worlds, client-owned peer pawns, save/load with peers connected.
- Tamper-proofing. The checksum detects truncation and corruption.
- Thumbnails, cloud sync, and a binary codec. The envelope reserves a thumbnail field. See *Later*.

## Revision from rev 1

Rev 2 keeps rev 1's file hygiene (unique names, atomic replace, checksum, safe JSON, host authority, PIE must not call `saveSceneToJson`) and drops the parts that fought the engine:

- The serializer catalog, `Serializers/*.cpp` per subsystem, and `ISaveHost::saveHost` field lists. Opt-in lives on the component. `SaveSystem` walks pools.
- Scene-file `"id"` writeback and the v2→v3 scene upgrade. Identity is assigned in memory at spawn.
- The special `payload.host` and `payload.systems` objects. Singleton progress is a component on a session entity, found by the same walk.
- Spawn-factory tables inside `Save/`. An entity that did not exist at the start is recreated by the host function that already knows how to spawn that archetype.

## Persist contract

`ECS/Persist.h` (DarkFoundation, no JSON, no `Save/`) holds the type-erased record the pool stores:

```cpp
namespace Dark
{
    struct PersistFns
    {
        const char* key;       // must match T::kTypeName, never empty, never "Unnamed"
        uint16_t    version;   // T::kSaveVersion
        int         order;     // lower runs first on load; 100 if the type does not care
        // null include = always save this component when its entity is in the progress set
        bool (*include)(const void* component);
        void (*capture)(const void* component, void* writer);
        // version is the "v" stored in the file, which may be older than PersistFns::version
        void (*apply)(void* component, void* reader, uint16_t version);
        // null unless the component stores Entity handles
        void (*bindRefs)(void* component, void* reader);
    };
}
```

A component opts in by declaring the member. Types that do not declare it are invisible to save and load.

```cpp
struct HealthComponent
{
    static constexpr const char* kTypeName = "Health";
    static const PersistFns kPersist;   // defined in Character/Health.cpp
    Health health;
};
```

`ComponentPool<T>` stores `const PersistFns* m_persist`. `World::getOrCreatePool<T>()` sets it when `T` has `kPersist`:

```cpp
if constexpr (requires { T::kPersist; })
    pool.setPersist(&T::kPersist);
```

The same `.cpp` that defines `kPersist` also registers it in a process-wide map keyed by `key`, in a function-local static initialized on first registration and from a namespace-scope static in that `.cpp`. Registration is how load can resolve `"Health"` even on an entity that does not have the component yet. The map is filled by the component's translation unit. Nothing in `Save/` lists keys.

Duplicate keys, an empty key, or a key that does not equal `kTypeName` log `DE_LOG_ERROR` and are ignored. `DE_ASSERT` in debug.

`IComponentPool` gains:

```cpp
const PersistFns* persist() const;
void visit(void (*fn)(EntityID id, void* component, void* user), void* user);
```

`ComponentPool<T>::visit` walks its dense arrays and passes `&m_components[i]`. `World::forEachPool` stays the only iteration of pools. Save never names `T`.

### What a component writes

`capture` / `apply` are compiled in the component's `.cpp`, so they can call private restore methods. They use `SaveWriter` and `SaveReader` (`Save/SaveJson.h`). They do not include a JSON DOM in the component header, and they do not call `get<>`, `at()`, or `operator[]` on JSON.

`SaveSystem` writes the component object and its `v`. The component writes the other keys into that object.

```cpp
// Character/Health.cpp — writes current HP only. maxHp and regen stay on HealthSettings,
// which the starting spawn already applied.
void captureHealth(const void* p, void* writer)
{
    const Health& health = static_cast<const HealthComponent*>(p)->health;
    auto& out = *static_cast<SaveWriter*>(writer);
    out.f32("hp", health.hp());
    out.f32("sinceDamage", health.timeSinceDamage());
}

void applyHealth(void* p, void* reader, uint16_t version)
{
    (void)version;
    Health& health = static_cast<HealthComponent*>(p)->health;
    auto& in = *static_cast<SaveReader*>(reader);
    float hp = health.hp();
    float since = health.timeSinceDamage();
    in.f32("hp", hp, 0.0f, health.maxHp());
    in.f32("sinceDamage", since, 0.0f, 3600.0f);
    health.restore(hp, since);
}
```

Rules for every opt-in:

- Write runtime progress. Leave tuning, asset bindings, and derived caches on the object the start state built (`HealthSettings`, sight cone, weapon desc, graph asset, `restPos`).
- A missing key leaves the value the start state (or the default `emplace`) already set. That is how an older save loads.
- A present key of the wrong type or out of range marks **that component on that entity** `ComponentInvalid`, logs, and continues. The file fails only when the envelope is bad (magic, parse, checksum, schema too new, scene identity).
- Enums, status names, HSM state names, and anim state names are strings. A content reorder must not remap a save. An unknown name restores that component's default for the field and logs once.
- `Entity` fields go out as persistent-id strings through `SaveWriter::entity`, and come back in `bindRefs` after every component `apply` has run. An unresolved id becomes `Entity{}` and logs once per component key.
- `include` can drop a single instance (for example a finished one-shot). It is not how whole categories are excluded. A category that is not progress omits `kPersist`.

`order` matters only when `apply` reads a sibling component. The default is 100. `Transform` uses a low order so the pose exists before anything that reads it. Reference fixup does not depend on `order`; it is always the later `bindRefs` pass.

### First types that opt in

This list is the v1 gameplay set, not a registry the save code switches on. Each row is work inside that component's own files. A row that is missing on a given PR simply is not in the file yet; load leaves the start-state value.

| Type | Writes | Leaves on the start state |
|---|---|---|
| `Transform` | position, rotation, scale | |
| `Health` | `hp`, `sinceDamage` | `HealthSettings` |
| `HitReaction` | stun, knockdown timers, knock direction | settings |
| `Poise` | poise, `sinceDamage`, hyper-armor | max, regen |
| `Defense` | stamina, blocking, parrying, charged parry, iframe and parry timers | arcs, costs, team |
| `StatusEffect` | active slots (status **name**, magnitude, remaining, hard, category, stacks, tick accumulator, source id), DR counts, `now` | `fx[]` ring. After apply, push an `Applied` fx event per slot so `tickStatusFx` rebuilds particles and audio |
| `PlayerMotor` | locomotion state, velocity, air time, coyote, jump flags, dodge time and wish | jump buffer and tap timers (input). Crouch restored as a state; the host treats crouch as held until the next edge so the first tick does not stand the player up |
| `JumpAttack` | `cooldownLeft`, and only from phase `Idle` | a save requested during `Telegraph` / `Leap` / `Connected` / `Pound` is refused by the host gate |
| `WeaponLoadout` | slot, melee and projectile cooldowns, live shots (pos, prev, vel, age, traveled, damage scale) | impact emitter, audio, listener. `restoreShots` clamps to `desc.maxLive` |
| `AiAgent` | the gameplay fields, including `lastSeen` / `hasLastSeen` and wolf stalk | `footstepAcc` |
| `PathAgent` | nothing in v1 (no `kPersist`, or `include` always false). The next tick repoths | the baked path |
| `Brain` | active state path by **name**, memory timer, history names | the graph asset. Restore sets the active path **without** enter/exit actions |
| `AnimGraph` | state name, lock, float and bool params by name, clip name, clip time, speed, loop override, `lowerBodyYaw` | triggers, the in-progress blend (snaps), pose, listeners, `prevWorldValid` |
| `Skill` | rank level and xp per skill id **name**, `grantMask` | `shootLock`, `seeArmed` (input latches). This is the progress record. Ranks are not written into `SceneObjectData` |
| `HealthPack` | `active`, `respawnIn` | `restPos`, spin, bob |
| `PhysicsBody` | linear and angular velocity when the body is dynamic | body id, shape. Pose came from `Transform` |
| `HsmGraph` on the player | nothing. After load the existing `syncPlayerHsm` redrives it from the motor | |
| `Coin` | collected flag, when that flag exists | the rest of the placement |

Types that stay out until some real progress field appears: `Mesh`, `Model`, `Camera`, `Tag`, lights, `Sight` (tuning), `EditorObject`, `EditorAuthoredPose`, `Networked`, `ParticleEmitter`, `StatusFx`, `HudTag`, `SoundEmitter`, `SoundBank`, `AudioListener`, `CloudVolume`. `LocalLightComponent::emissiveMesh` is rebuilt by the scene loader, not by the save.

`PersistentIdComponent` is infrastructure. The walker reads it as the join key. It is not also dumped through `kPersist`.

## Identity

```cpp
namespace Dark
{
    enum class PersistOrigin : uint8_t { Authored = 0, Procedural, Spawned };

    struct PersistentIdComponent
    {
        static constexpr const char* kTypeName = "PersistentId";
        UUID          id{ 0ull };          // 0 = unset. Reuse Core/UUID
        PersistOrigin origin = PersistOrigin::Authored;
        char          archetype[24]{};     // Spawned only. Entity-master / spawn name ("wolf", "hunter")
    };

    // FNV-1a 64. Top bit set so a procedural id is distinct from a random UUID in files.
    // Same key → same id every run.
    UUID makeProceduralId(std::string_view key);
}
```

Serialized as 16 lowercase hex digits, never a JSON number.

| Origin | Who stamps it | Value |
|---|---|---|
| Authored | The existing scene-instantiation path, once per object, after it creates the entity | If the object ever grows an editor-authored id, use that. Until then `makeProceduralId("scene/<scenePath>/<index>/<type>")`. Reordering `objects[]` changes ids. That is acceptable until the editor stores its own ids, and those ids would still be authored data, not progress |
| Procedural | The spawn that already creates the entity | A fixed key such as `"sandbox/pathchase/hunter/0"`, `"sandbox/healthpack/3"`, `"session"` |
| Spawned | The runtime spawn, via `stampSpawned(world, e, archetype)` | Random `UUID`, plus the archetype string |

An entity **without** `PersistentIdComponent` is not progress (camera rig, muzzle, debug draw, status-fx carrier, editor gizmo). The walk ignores it even if it has a `Transform`.

Stamping an id is part of creating a gameplay entity, same as attaching a transform. It is not a list of fields to serialize. The scene loader stamps every authored object in one place. Each procedural spawn stamps itself because it does not go through that loader.

At the moment play becomes live, `SaveSystem::captureBaseline()` copies the set of authored and procedural ids currently alive. That set is memory for this run, not a scene file.

- A baseline id that is not alive at save time is written to `removed[]`. Load, after rebuilding the start state, destroys those entities.
- A saved entity whose id is not in the fresh baseline is `Spawned` (or the start state no longer contains that authored id). Spawned entities are created by one host callback, then overlaid. An authored id the new start state did not build is skipped and logged. The save does not invent level geometry.
- A live `Spawned` entity whose id is absent from the save is destroyed. The save is the authority for what the run had created.

```cpp
// Host implements this next to the spawn code it already has. Save/ does not switch on archetype.
Entity (*respawn)(World& world, std::string_view archetype, const TransformComponent& xf);
```

`respawn` uses the existing master / `spawnHunter` / health-pack path, then `stampSpawned` is **not** called again: the overlay assigns the saved id before component `apply`. A null entity from `respawn` logs `SpawnFailed` for that id and continues.

## State that is not a component

The walk sees pools only. These values are real progress or real clocks, and they live outside `World` today. Each one moves onto a component the owner already updates, and that component opts in. After that, save finds them with no further save-system work.

| Today | New home |
|---|---|
| `AiSystem::m_time`, `m_packAttackGap`, `m_jumpAttackToken` | `AiClockComponent` on the session entity. `AiSystem` reads and writes that component. Wolf notice salt uses the persistent id's low bits instead of `EntityID`, which changes across a load when slots are recycled |
| `SandboxApp` / `EditorApp` look yaw, look pitch | Fields on the player entity (a small `LookComponent`, or the motor if that stays the owner). The app reads them to aim the camera |
| `m_lowerBodyYaw` | Already on `AnimPlayer`. `AnimGraph`'s opt-in writes it. The app member copies it out for the camera, it is not saved separately |
| `m_cloudTime`, sky time-of-day, water time | One `WorldClockComponent` on the session entity, written by the same tick that advances those clocks today |
| `m_offhand.lightOn` | A field on the player entity the offhand code already owns |
| `m_playerSpawn`, dead timer, spawn age | The player entity or the session entity, whichever already owns that lifecycle |

Reset on load, and not stored, because they are input edges or one-shots: attack and block charge, charge windup, fire latches, jump-attack buffer, shield-held phase, footstep accumulator, hurt-sound timer, `PlayerMotor` jump buffer.

`preySense` stays derived. The host already recomputes it each frame.

The session entity is created by the host at play start with procedural id `"session"` and whatever singleton components that host uses. Sandbox and Editor play can attach different sets. The walker does not care.

There is no `ISaveHost::saveHost` JSON object. The host seam is only:

```cpp
struct SaveHost
{
    const char* id;   // "sandbox" or "editor-pie". Stored in the envelope. Cross-host load is refused.
    bool (*canSaveNow)(SaveResult& why);
    void (*beginLoad)();   // pause, stop voices, clear status fx and one-shot particles, clear input edges
    void (*endLoad)();     // warm anim pose, push physics, place the camera from LookComponent, restart music
    Entity (*respawn)(World&, std::string_view archetype, const TransformComponent& xf);
};
```

## File layout

```
<exeDir>/Saves/
    save_2026-10-03_18-01-05_ab12.json     manual, never auto-deleted
    quick_2026-10-03_18-04-40_9f03.json    quicksave ring, 3 files
    auto_2026-10-03_18-10-00_51c7.json     autosave ring, 5 files
    index.json                             cache for the menu, rebuilt when stale, never authoritative
    *.json.tmp                             in-flight; startup deletes these if older than 1 minute
```

- Name = `<kind>_<YYYY-MM-DD>_<HH-MM-SS>_<4 hex>.json`. Kind is `save`, `quick`, or `auto`. The suffix is the low 16 bits of a new `UUID`. Create with `CREATE_NEW`. On `ERROR_FILE_EXISTS`, roll a new suffix, at most 8 times. Sort by `savedAtUtc` in the envelope, not by the file name.
- Directory = `executableDirectory() / "Saves"`, created on first save. If the directory cannot be created or a probe write fails, fall back to `%LOCALAPPDATA%\DarkEngine6\Saves` and log once.
- `.gitignore` gains `Saves/`. `cmake/CopyContent.cmake` does not copy it.
- The menu lists by scanning names that match the pattern, size under the cap, and peeking the envelope. `index.json` is an atomic cache of that scan.

### Envelope

| Key | Role |
|---|---|
| `format` | `"DarkEngine6.Save"`. Anything else is `BadFormat` |
| `schema` | Envelope version. v1 is `1`. A greater value is `SchemaTooNew` and does not touch the world |
| `engine` | `{ "version", "git" }` from `kEngineVersion` and `kEngineGit` |
| `host` | `SaveHost::id` |
| `kind` | `manual`, `quick`, or `auto` |
| `saveId` | UUID of this save |
| `displayName` | ≤ 64 UTF-8 bytes. Never used as a path |
| `savedAtUtc` | Sort key |
| `playTimeSec` | From `WorldClockComponent` when that component is present |
| `scene` | `{ "path", "hash", "procedural" }`. `path` is a content-relative virtual path, empty when the host is fully procedural. `hash` is FNV-1a 64 of the scene bytes, or empty. `procedural` is a host layout tag such as `"sandbox.pathchase.v1"`, or empty when the world came only from the scene |
| `thumbnail` | `null` until a later sidecar exists |
| `counts` | `{ "entities", "components" }` for the UI and a sanity check |
| `payload` | The progress document below |
| `integrity` | Last. `{ "algo": "fnv1a64", "payload": "<16 hex>", "bytes" }` over the compact `ordered_json` dump of `payload` |

A scene `hash` mismatch logs and continues: the level was edited after the save, and ids still line up for objects that were not reordered. A `path` or `procedural` tag that is not the one the host just built is `SceneMismatch`. v1 does not unload and switch levels inside the load.

```json
{
  "format": "DarkEngine6.Save",
  "schema": 1,
  "host": "sandbox",
  "kind": "manual",
  "saveId": "5f1e9c0a2b7d4e61",
  "displayName": "Ridge, two wolves down",
  "savedAtUtc": "2026-10-03T06:01:05Z",
  "playTimeSec": 1834.25,
  "scene": { "path": "", "hash": "", "procedural": "sandbox.pathchase.v1" },
  "thumbnail": null,
  "counts": { "entities": 4, "components": 11 },
  "payload": {
    "removed": ["8000000000000a13"],
    "entities": [
      {
        "id": "a1f4c2d09e3b7710",
        "origin": "procedural",
        "archetype": "",
        "components": {
          "Transform": { "v": 1, "pos": [14.25, 31.9, -6.5], "rot": [0.9659258, 0, 0.258819, 0], "scl": [1, 1, 1] },
          "Health":    { "v": 1, "hp": 62.5, "sinceDamage": 1.75 },
          "Skill":     { "v": 1, "ranks": [ { "id": "shoot", "level": 2, "xp": 40 } ] }
        }
      }
    ]
  },
  "integrity": { "algo": "fnv1a64", "payload": "0c9f6a1e33d2b8a4", "bytes": 900 }
}
```

Component objects in the example are illustrative. The real keys are whatever those types' `capture` wrote.

### Safe JSON

`SaveReader` / `SaveWriter` wrap `nlohmann::ordered_json`. Readers check the type, then the range. Parse with `allow_exceptions = false` and a depth callback that rejects depth > 32.

```cpp
bool f32(const char* key, float& io, float lo, float hi);   // missing keeps io; bad type returns false
bool u8(const char* key, uint8_t& io, uint8_t maxInclusive);
bool boolean(const char* key, bool& io);
bool vec3(const char* key, Math::Vector3f& io, float absMax);
bool quat(const char* key, Math::Quaternion& io);           // normalizes; rejects near-zero length
bool pid(const char* key, UUID& io);                        // 16 hex, or null → 0
bool enumName(const char* key, std::string_view const* names, int count, int& io);
```

Writers emit finite floats only. A non-finite value logs, writes the field's default, and bumps `counts.sanitized`. `ordered_json` keeps key order stable for diffs.

`Save/` and component `loadProgress` files are grepped for `get<`, `.at(`, `try`, `catch`, and `throw`. `scripts/check-no-exceptions.ps1` gains `Save`.

## Save flow

```
main thread, end of the sim block                SaveIoWorker
request → host.canSaveNow → walk pools → ordered_json → dump → hash → .tmp → flush → MoveFileExW
```

- F5, the menu, or the autosave timer sets a request. It does not write.
- `SaveSystem::service()` runs once a frame at the end of the sim block, after animation and AI ticks, before render reads the world. A paused sim is also a boundary, and saving while paused is allowed.
- `canSaveNow` refuses the main menu, a load already in progress, a dead player, `PlayerMotor` in `Dodge`, any `JumpAttack` outside `Idle`, and `NetRole::Client` / `Joining`. A manual or quick save at a bad moment returns `UnsafeMoment`. An autosave waits up to 10 seconds of frames, then skips with a log.
- Capture builds the id → entity map from `PersistentIdComponent`, then `forEachPool`. A pool with a null `persist()` is skipped. For each visited component, skip the entity if it has no persistent id, or if `include` returns false. Write `components[key]`. Also write `removed` from the baseline set. The DOM owns all of its memory. Target under 2 ms for a few hundred entities. If a capture ever exceeds that, the same `PersistFns` can later fill a POD buffer and the worker can build JSON. The file stays the same.
- One worker thread, one job at a time, same shape as `EditorApp`'s generation thread. A second manual request while busy returns `Busy`. A second autosave replaces the pending autosave.
- The main thread polls `pollCompleted()` for a toast.

### Atomic replace and rings

```
tmp = path + ".tmp"
CreateFileW(tmp, CREATE_ALWAYS) → write all bytes → FlushFileBuffers → CloseHandle
MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
on failure: DeleteFileW(tmp), log GetLastError, return the Io* code
```

Unique save names mean `REPLACE_EXISTING` matters for `index.json`, not for the save itself. Quick ring size is 3, auto ring size is 5. Delete the oldest ring file only after the new file has been renamed into place. The UI delete command deletes one validated file name.

If the newest file of a kind fails to parse, the UI offers the next newest of that kind and renames the bad file to `*.corrupt`.

Startup removes `*.json.tmp` older than one minute.

## Load flow

1. **Worker.** Check the file name, size ≤ 16 MiB, read, parse, check `format`, `host`, `schema`, integrity, run envelope migrations. Return a DOM or a `SaveResult`.
2. **Main thread gate.** Role is `Idle`, or `Host` with `peerCount() == 0`. Not already saving.
3. **Snapshot.** Capture an in-memory progress DOM of the current world with the same walk. It was written by this build, so applying it back does not depend on older versions.
4. **`beginLoad`.** Pause, `audio().stopAll()`, `clearAllStatusFx`, clear one-shot blood and weapon transients, clear input edges.
5. **Start state.** The running world must already be the start state for this `scene` / `procedural` tag (v1 loads in place, from the world `onInit` or Play just built). A mismatch returns `SceneMismatch` and skips the overlay. Rebuild the baseline id set from the entities that exist right now.
6. **Remove.** Destroy every live baseline entity whose id is in `removed[]`, and every live `Spawned` entity whose id is not in the file. Networked entities go through `unregisterEntity` only.
7. **Respawn.** For each saved entity that is not alive, call `host.respawn`. Assign the saved persistent id before any `apply`.
8. **Apply.** This pass follows the file, not the live pools. Group the file's component objects by `PersistFns::order`. Look each key up in the binding map. A missing key is skipped and logged once. Otherwise, if that entity does not have the component yet, call the binding's `emplaceDefault(world, entity)`, then `apply` on the new component. `emplaceDefault` is `[](World& w, Entity e){ w.emplace<T>(e); }`, stored on the binding by `bindPersist<T>()` from that component's `.cpp`, one line beside `kPersist`. `SaveSystem` does not name `T`. Capture is the only pass that uses `visit`.
9. **`bindRefs`.** Second pass. Persistent ids become `Entity` values.
10. **`endLoad`.** `syncPlayerHsm`, status fx replay, one zero-dt anim evaluation, clear `prevWorld` / TAA history, `physics.pushPoses`, camera from the look component, music, terrain streaming at the player.

If a step after `beginLoad` fails, apply the snapshot from step 3 and `endLoad`. The world is not left half-overlaid.

Component `v` newer than `PersistFns::version`: skip that component, log, continue. Older `v`: `apply` receives it and upgrades in place. Unknown keys: skip, log once per key. Re-saving drops unknown keys. That is logged.

Envelope `schema` migrations, when they exist, are pure DOM-to-DOM steps in `Save/SaveMigrations.cpp` and run before any `apply`. A component field change does **not** bump `schema` and does **not** get a row in that file. The component's own `apply` handles it.

### Emplace thunk

`PersistFns` lives in DarkFoundation and cannot mention `World` in a way that pulls `Save` upward, and it cannot be a template. The emplace thunk and the JSON function pointers therefore live in a second record, `SaveBinding`, in `Save/SaveBinding.h`, which points at the `PersistFns` and adds `emplaceDefault`. The pool stores `const PersistFns*` for the walk. The process-wide map stores `const SaveBinding*` for load. `bindPersist<T>()` fills both from `T::kPersist` plus a lambda that emplaces `T`.

`World.h` stays free of JSON and of `Save/`. The concept check in `getOrCreatePool` only takes the address of `T::kPersist`.

## Limits

Constants in `Save/SaveTypes.h`:

| Limit | Value |
|---|---|
| File size | 16 MiB, checked before the read |
| JSON depth | 32 |
| Entities | 16 384 |
| Strings | 256 bytes |
| Status slots | `StatusEffectComponent::kMaxStatus` (12) |
| DR entries | `kCcCategoryCount` |
| Projectiles on one loadout | `desc.maxLive` (16) |
| HSM path | 16 names |
| Anim params | 64 |
| Positions | finite, absolute value ≤ 1e6 |
| Timers | ≥ 0 and ≤ 3600 |
| `hp` / stamina | inside the live max from the start state |

Load and delete accept only a bare name matching `^(save|quick|auto)_\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2}_[0-9a-f]{4}\.json$`, joined onto the saves directory. Reject separators, `..`, drive letters, alternate data streams, reserved device names, and `FILE_ATTRIBUTE_REPARSE_POINT` on the file or on `Saves/`. `scene.path` is resolved only through `resolveContentFile`. After `weakly_canonical` it must stay under that content root.

## Networking and Editor play

- `requestSave` checks `network().role()`. `Client` and `Joining` return `NotHost`.
- Entities whose `NetworkedComponent` says the owner is not the host are skipped. Peer pawns come back from the existing net spawn on rejoin. `NetId` is never written.
- `respawn` of a replicated archetype calls `network().registerEntity` inside the host's existing spawn, not inside `Save/`.
- Load while hosting requires zero peers.
- Editor play uses `SaveHost::id` `"editor-pie"` and only while `m_playMode` is set. Files go next to `Editor.exe`. Stop Play still restores authored poses through `restoreAuthoredPoses()` and `resetPlayCombat()`. Progress save never calls `saveSceneToJson`.
- Editor scene save/load keep F5 / F9 / Ctrl+S / Ctrl+O. Play quicksave and quickload are **Shift+F5** and **Shift+F9**, plus Play menu items. The unguarded F5 scene write during play gets a `!m_playMode` guard in that PR.
- Sandbox binds quicksave to F5 and quickload to F9. Both are free in `registerDefaultActions` today.

## Testing

| Test | What it proves |
|---|---|
| `PersistDiscoveryTests.cpp` | A test component with `kPersist`, compiled into the test binary and never named by `SaveSystem`, appears in a captured DOM. A second test component with no `kPersist` does not. A third writes one field of two; after overlay onto a fresh default, the omitted field is unchanged |
| `SaveJsonTests.cpp` | Missing key, wrong JSON type, non-finite number, out of range, bad pid. No throw |
| `PersistentIdTests.cpp` | `makeProceduralId` is stable (pinned hex). Duplicate id on capture is reported. Baseline minus alive equals `removed` |
| `ComponentRoundTripTests.cpp` | One test per opted-in type, living next to that type's concerns: non-default values, capture, fresh start-state component, apply, exact `==` on the floats (the text round trip is lossless). Includes a status `source` id that rebinds, a brain path restored by name, an anim clip time, a projectile still in the loadout |
| `SaveFileGoldenTests.cpp` | `golden/v1_minimal.json`: load, save, byte-equal payload dump. Checksum matches. `dump(parse(s)) == s` for the payload |
| `SaveMigrationTests.cpp` | An older component `v` takes the upgrade path inside `apply`. A newer component `v` skips that component. `schema > kSchema` returns `SchemaTooNew` and does not change a fixture world |
| `SaveCorruptionTests.cpp` | Truncate a golden file, flip fixed-seed bytes, wrong JSON types, depth bomb, 17 MiB file. Expect `ParseFailed`, `ChecksumMismatch`, `TooLarge`, or `ComponentInvalid`. No crash |
| `AtomicFileTests.cpp` | Round trip. A failed rename leaves the previous file and no `.tmp` |
| `SavePathTests.cpp` | Reject `..\\x.json`, `C:\\x.json`, an alternate data stream name, `CON.json`. Long `executableDirectory` buffer |
| `SaveLoadOrderTests.cpp` | `removed` destroys a baseline entity. `respawn` is called for an unknown id and not for a known one. Unresolved ref becomes a null entity. A failed apply rolls back to the pre-load snapshot |
| `SaveNetGateTests.cpp` | `FakeTransport`: client save returns `NotHost`. Host with a peer cannot load |

Manual check, Sandbox: F5 with the player hurt and crouched, a wolf mid-stalk, a status ticking, a shot in the air. Move and take another hit. F9 restores HP, status time, the shot, the wolf's stalk fields, crouch, and the crouch clip phase. The scene file's bytes are unchanged. Editor: Shift+F5 during play, then Stop, and the authored poses return; the `level.json` on disk is identical.

## Phased PRs

| PR | Scope |
|---|---|
| **P0** | This document |
| **P1** | `Save/` on `DE_ENGINE_REST_FOLDERS`. `SaveResult`, limits, `SaveReader` / `SaveWriter`, paths, name checks, `AtomicFile`, FNV-1a 64, long-path `executableDirectory`, `.gitignore`. Tests: JSON, atomic file, paths. No world walk yet |
| **P2** | `ECS/Persist.h`, pool `persist()` + `visit`, `PersistentIdComponent`, `makeProceduralId`, baseline set. `SaveSystem` capture and in-place overlay through the binding map. `bindPersist<T>` template. Discovery test with a fixture component only. Scene loader and Sandbox/Editor spawn sites stamp ids. No scene-file format change |
| **P3** | Opt-in next to the type: `Transform`, `Health`, `HealthPack`, `Skill`. Sandbox `SaveHost`, F5/F9 quick ring, session entity, `WorldClockComponent` if the clock is required for those types. Golden `v1_minimal` |
| **P4** | Combat opt-ins: `Defense`, `Poise`, `StatusEffect` (fx replay), `HitReaction`, `JumpAttack` cooldown, `WeaponLoadout` including live shots. Host `canSaveNow` for dodge and jump-attack phase |
| **P5** | `PlayerMotor`, `LookComponent` (look yaw/pitch moved off the app), crouch latch, offhand light field |
| **P6** | `AiAgent`, `Brain` restore-by-name, `AiClockComponent` moved out of `AiSystem` members, wolf salt uses the persistent id. `AnimGraph` restore, zero-dt warm, `syncPlayerHsm` |
| **P7** | `removed` / `respawn` / rollback snapshot, `PhysicsBody` velocity, net gate |
| **P8** | Manual name, autosave, rings, `index.json`, `*.corrupt`, local-appdata fallback, main-menu Continue / Load, toasts |
| **P9** | Editor play host, Shift+F5 / Shift+F9, Play menu, `!m_playMode` guard on scene save |

Each PR stays inside `scripts/check-no-exceptions.ps1` and the JSON-access grep. A PR that adds an opt-in does not edit a central table.

## What must keep working

- Scene version 2 files load exactly as they do now. No new required fields.
- `World::emplace` replace-in-place, `EntityID` packing, and replication caps.
- Editor Stop restores authored poses. Editor scene save/load keys outside play.
- Sandbox and Editor start with no `Saves/` directory. The folder is created on the first save. Missing saves are not an error.

## Later, same walk

- **Binary codec.** `capture` already writes through `SaveWriter`. A second writer can emit CBOR or msgpack from the same calls. Golden tests stay on JSON.
- **Thumbnails.** Readback at capture, sidecar PNG, fill `thumbnail`.
- **Steam Auto-Cloud** on the install directory, pattern `Saves/*.json`, excluding `*.tmp`, `*.corrupt`, and `index.json`. Unique names avoid cross-machine clobber.
- **Level switch on load.** When `scene.path` differs, unload, `loadSceneFromJson`, stamp ids, then run the same overlay.
- **Editor-authored object ids** inside `level.json`, as an editor feature, so reordering objects does not change identity. The save format already stores opaque ids. The scene loader would stamp the authored id instead of the index hash. The save system still would not write the scene.

## Acceptance

- [ ] F5 in Sandbox writes `Saves/quick_<ts>_<hex>.json` beside `Sandbox.exe` when the CWD is somewhere else.
- [ ] Three manual saves are three files. Loading the oldest restores the oldest overlay.
- [ ] A component that opts in is present in the file without any edit under `Save/` other than what that component's `.cpp` registers. A component that does not opt in is absent.
- [ ] HP, skill ranks, AI leaf and `lastSeen`, wolf stalk, anim state and clip time, status slots and DR, poise, stamina, crouch, and a projectile in flight round-trip. Tuning (`maxHp`, sight cone, weapon desc) stays whatever the start state set, even if the save is old.
- [ ] Killing the process during the `.tmp` write leaves the previous saves loadable.
- [ ] One flipped payload byte returns `ChecksumMismatch`, the UI offers the previous file, and the bad file is renamed `*.corrupt`.
- [ ] An unknown component key loads with a warning and the start-state value for that type.
- [ ] `schema` newer than this build returns `SchemaTooNew` and does not change the world.
- [ ] A client quicksave returns `NotHost`. A host with a peer cannot load.
- [ ] Editor play quicksave does not change the scene file. Stop restores authored poses.
- [ ] No `try` / `catch` / `throw` under `Save/` or in `capture` / `apply` functions.
