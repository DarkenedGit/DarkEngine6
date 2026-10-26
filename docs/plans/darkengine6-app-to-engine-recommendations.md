# DarkEngine6 — App-to-Engine/ECS Migration Recommendations

Surveyed at `main` = `e1b59cb` via GitHub API (no clone, nothing built). Line numbers refer to that commit.
Files read in full or by range: `Sandbox/SandboxApp.h` (423 lines), `Sandbox/SandboxApp.cpp` (6,167), `Sandbox/PathChase.h/.cpp` (91/317), `Sandbox/DevToolsPanel.cpp` (1,805), `Sandbox/SandboxSave.cpp` (319), `Editor/EditorPlay.cpp` (1,459), `ECS/World.h`, `ECS/Components.h`, `Render/PassTable.h`, `cmake/DarkEngineTargets.cmake`.

Note: "PathChase" is not a separate app. It is `Sandbox/PathChase.{h,cpp}`, a helper class owned by `SandboxApp` (`m_chase`, SandboxApp.h:316).

---

## 1. Summary

`SandboxApp` is about 8,600 lines (SandboxApp.cpp 6,167, DevToolsPanel.cpp 1,805, SandboxSave.cpp 319, plus the header). It is really a whole game *and* a renderer front end. Specifically:

* **Gameplay state that belongs to entities lives in app-member arrays**, not components. This covers power boxes, circuit lights, guard torches, quest zones, conceal/grass boxes, the goal box, the carried body, loot-dropped flags, worn armor and player combat timers (SandboxApp.h:222–301, 373–375, 395–410). That state is invisible to the Editor, to net replication and to the save serializer registry, which is why `SandboxSave.cpp:242–319` has to copy app members into components by hand.
* **Rules systems are written as app methods**: power/circuits, guard investigation, takedown/carry, quest zones and rewards, hunter loot and stocking, spawners, crows, interaction picking, light level and guard flashlights (SandboxApp.cpp:1116–1665, 2141–2362).
* **About 600 lines are copy-pasted into `Editor/EditorPlay.cpp`**: spawners, crows, throwables, the weapon hit path, the impact-FX host, the jump-attack and hunter-cue callbacks. The Editor copy is missing power, quests, zones, carry, loot and guard flashlights, so **Editor Play can't test those features**.
* **`onRender` is a 1,200-line hand-rolled frame** (SandboxApp.cpp:4773–5972). The engine already has a declarative pass list (`Render/PassTable.h`) with per-app tables (`sandboxDeferredPasses`, `editorDeferredPasses`…). It is only used to check the order *after* the frame runs; it doesn't drive the frame.
* **`PathChase` mixes four concerns**: AI simulation ownership (`AiSystem m_ai`), the navmesh bake, a debug player cube ("walker"), and a D3D12 debug line renderer with its own upload ring (`% 2`, PathChase.cpp:211).

The engine already has most of the destinations: an `ECS/World` with component pools and `each<T>`, `Play/` (Spawner, Crow, HunterTick, WeaponFire, ImpactFx), `Gameplay/` (Quest, Inventory, ThrowableBelt), `Save/` with a serializer and `ProgressComponents.h`, `AI/AiSystem`, `Render/SceneRenderer` and `Render/PassTable`. Most of the work is moving code into them, plus adding about 10 components.

---

## 2. Principles

1. **The app is a thin host.** It owns the window, the input action map, scene choice/menu, the net role, dev-tool UI and game-specific rule glue that is truly one-off. Target: SandboxApp.cpp under about 1,500 lines.
2. **The engine holds reusable systems.** Anything both Sandbox and Editor Play run belongs in an engine module (`Play/`, `Gameplay/`, `AI/`, `Render/`), called the same way from both.
3. **The ECS holds per-entity state.** If a piece of data describes *one thing in the world* (a power box, a zone, a torch, a carried body), it's a component on that entity, authored from scene JSON. Systems are free functions or classes `tickX(World&, Ctx&, float dt)`, matching the existing `tickAnimGraphs`, `tickParticleEmitters`, `tickSoundEmitters`, `tickStatusFx` and `simulateCrows` style.
4. **Tuning goes in data.** Distances, timers and colours live in JSON under `content/`, read through `Core/Json.h` checked helpers (no exceptions).
5. **Callbacks stay as `void* user` thunks**, matching `setHunterCue` and `setJumpAttackHits`, and avoiding `std::function` churn.
6. **The Editor reuses the same systems.** Editor Play should call the same tick list as Sandbox, through a shared `PlaySession` (P4).
7. **Components register with the save registry** (`Save/Persist*.cpp`) when they're introduced, so they survive saves automatically.

---

## 3. Catalogue: what to move and where

Approximate LOC is computed from the line ranges below.

| # | Item | Current location | LOC | Target | Rationale | Depends on | Risk |
|---|---|---|---|---|---|---|---|
| 1 | Power boxes / circuits (`PowerBox`, `m_powerBoxes`, `m_circuitLights`) + `setCircuit`, `updatePower` | SandboxApp.h:251–266; SandboxApp.cpp:1160–1313; scene parse 4415–4416, 4461–4462 | ~170 | ECS: `PowerBoxComponent`, `CircuitMemberComponent` (on lights); system `Gameplay/PowerSystem.{h,cpp}` (`tickPower`, `setCircuit`) | Per-entity state is held as an app array, so the Editor can't run it, and saves can't persist on/off or a fixer mid-task. It's also where the `checker` one-frame bug I raised earlier lives (1256–1258). | `AI/Awareness` (`alertToInvestigate`), `AI::Walkability`, Audio cue, Quest event sink | Med: changes stealth behaviour, so regression-test guard investigations |
| 2 | Guard torches (`m_guardLights`) + `updateGuardFlashlights` | SandboxApp.h:267; SandboxApp.cpp:1447–1471; spawn 4551–4561 (hard-coded 1300 intensity, 18 m, 12/26° cones) | ~45 | ECS: `AttachedLightComponent{owner, offset, pitch, nightOnly}`; system in `Play/AttachedLights.cpp`; torch params into `content/ai/guard.json` (or the NPC archetype) | A generic "light follows owner" behaviour. `Play/PlayerFlashlight` already does the player's version. | Sky `Environment::sunElevation` | Low |
| 3 | Quest runtime: zones, `m_zonesInside`, banners, rewards, `questTarget`, `startQuest` | SandboxApp.h:284–299; SandboxApp.cpp:1473–1612; scene parse 4392–4426 | ~190 | ECS: `QuestZoneComponent{name}` + `TriggerVolumeComponent{AABox}`; system `Gameplay/QuestRuntime.{h,cpp}` that owns `QuestTracker`, listens to an event bus, and grants rewards via the inventory API. Banners go to `Ui/QuestHud` | Quest logic already sits in `Gameplay/Quest.cpp` with no engine dependencies; only the *glue* is in the app. Moving it makes quests work in Editor Play and saveable (open item from PR #30). | Inventory, event bus (§5), save registry | Med |
| 4 | Collect-objective progress (`m_quest.collect` in `giveItem`) | SandboxApp.cpp:2209–2233 (2219) | 25 | `Gameplay/QuestRuntime`: derive collect progress from `InventoryComponent` counts each tick, or on inventory-changed events | Fixes the farming bug still open from the PR #30 review. Loot-bag and reward items count correctly. | #3 | Low |
| 5 | Interaction picking (`InteractKind`, `pickInteract`, `pickUpNearestItem`) | SandboxApp.h:268–281; SandboxApp.cpp:1116–1158, 2156–2207 | ~95 | ECS: `InteractableComponent{kind, range, prompt}`; system `Play/Interaction.{h,cpp}` (`pickInteractable(world, eye, look, body)` → returns the entity; the host dispatches by kind) | Today it hard-codes 4 sources (bodies, power boxes, pickups, loot) with magic ranges 2.2/2.5. A component makes it data-driven and lets the Editor reuse it. | #1 (power boxes become entities) | Low–Med |
| 6 | Takedown / body carry (`takedownTarget`, `bodyNear`, `tryTakedown`, `toggleCarry`, `updateCarry`, `m_carried`, `m_carryFloor`, `m_grassBoxes`) | SandboxApp.h:282–283, 289; SandboxApp.cpp:1315–1445 | ~130 | ECS: `CarriedByComponent{carrier, floorOffset}` on the body; `ConcealVolumeComponent`; system `Play/Takedown.{h,cpp}` | It filters by `tag->name == "Wolf"` strings (1144, 1209, 1327, 1355), which should be an archetype/faction field. The body-in-grass check duplicates `AiSystem::setConcealVolumes` (4424). | `AiSystem`, `TagComponent` → faction | Med |
| 7 | Hunter loot / stocking (`stockHunter`, `updateHunterLoot`, `m_lootDropped`) | SandboxApp.h:374; SandboxApp.cpp:2313–2362 | ~50 | ECS: `LootTableComponent{items, dropOnDeath}` + `LootDroppedTag`; system `Gameplay/LootSystem.cpp`; the "medkit" stock (2316) goes into the NPC archetype JSON | Hard-codes `"medkit"` and keeps a `unordered_set` of ids. It iterates `m_chase` hunters only, so spawner wolves/hunters aren't covered uniformly. | Inventory, ArmorPieces | Low |
| 8 | Spawner glue: `tickSpawners`, `spawnSpawnerUnit`, `tickCrows` | SandboxApp.cpp:2235–2311 | ~77 | `Play/Spawner.cpp` → `tickSpawnerSystem(World&, SpawnHost&, dt)` plus a `SpawnHost` struct of callbacks/refs | Near-identical copy in `Editor/EditorPlay.cpp:1228–1309`. The diff of `tickSpawners` is only the guard line. | `CharacterHost`, `attachNpcVisual`, `PathChase::spawnListedHunter` → `AiSystem` | Low |
| 9 | Throwable flights: `releaseThrowable`, `tickThrowableFlights` | SandboxApp.cpp:2851–2941 | ~91 | `Play/ThrowableSystem.{h,cpp}` (wraps `Weapons/ThrowableFlight`) | Copy in `EditorPlay.cpp:626–716` (59 differing diff lines, mostly names) | WeaponFire, physics | Low–Med |
| 10 | Weapon hit/fire path: `makeWeaponQuery`, `onWeaponHit`, `firePossessedLoadout`, `resolveJumpAttackAndFx` | SandboxApp.cpp:2456–2563, 2771–2849, 2943–2998 | ~245 | `Play/WeaponFire` + `Combat/` → a `CombatHost` (one struct of world/physics/audio/fx refs) and free functions | Editor has a parallel set: `EditorPlay.cpp:480–577, 717–746` | Combat, ImpactFx | Med |
| 11 | Impact/blood/footmark FX forwarding (`impactHost` … `spawnDeathBloodDecal`, `spawnHunterBlood`) | SandboxApp.cpp:3378–3430 | ~53 | Already-existing `Play/ImpactFx` → make `ImpactFxHost` a member of the shared play session | Copied in `EditorPlay.cpp:578–625` (40 diff lines) | — | Low |
| 12 | Hunter cue/jump-hit callbacks + hunter hit-reaction tuning | SandboxApp.cpp:4490–4517 (lambda; stun 0.45, knockback 0.6); PathChase.cpp:93–98 (knockback 2.2, overwritten later) | ~35 | `Play/HunterTick.h` (`defaultHunterCue`), tuning to `content/ai/hunter.json` | Editor version `EditorPlay.cpp:541–551` already drifted: it lacks the `grunt`→jump-anim and the `pounce`/`pound` fallbacks. Two conflicting knockback constants. | — | Low |
| 13 | `PathChase` AI ownership + nav bake + obstacle list | PathChase.h:31–88; PathChase.cpp:81–196, 300–313 | ~150 | Move `AiSystem` ownership to an engine `Play/PlaySession` (or `AI/AiWorld`); nav bake to `AI/Walkability` (`bakeFromScene(terrain, water, obstacles)`); obstacles as `NavObstacleComponent` | The Editor already owns its own `m_ai` and `bakePlayWalkability` (`EditorPlay.cpp:248–301`), so there are two bake paths. | Scene parse (#16) | Med |
| 14 | `PathChase` walker (debug player cube, `pawn_x/z` axes, hard-coded `(-6,0,0)`, mesh colour 220/90/40) | PathChase.cpp:120–154, 173–191 | ~55 | App-only fallback pawn; spawn from a `PlayerStart` scene object as a normal entity with `PlayerMotorComponent` | Hard-coded debug pawn that doubles as "the player" for AI targeting (193). | #13 | Low |
| 15 | `PathChase::drawPaths` debug lines with own upload buffers | PathChase.cpp:41–79, 207–297; also SandboxApp skeleton lines SandboxApp.h:217–221, SandboxApp.cpp:3176–3303 | ~250 | `Render/DebugDraw` (one engine immediate-mode line batcher, ring sized from the renderer's frame count) + `AI/AiDebugDraw.cpp` that emits segments | Three separate line upload rings (`% 2`, the same slot-reuse concern as underwater). The FOV of 35°/12 m is hard-coded and doesn't read `AI/Sight` settings. | Renderer frame count | Med (D3D12; needs PC compile) |
| 16 | Scene object interpretation in `startScene` (zones, pickups, goal, circuits, conceal, obstacles, grass patches, boxes, lights, pawns, patrols) | SandboxApp.cpp:4391–4568 (inside `startScene` 3973–4631, 659 lines) | ~180 | `Scene/SceneSpawn.{h,cpp}`: `instantiateScene(SceneData, World&, SpawnHost&)` turns each `SceneObjectData` into entities + components | The Editor has its own instantiation (`EditorSceneFile.cpp`, `EditorSpawn.cpp`; not reviewed line by line). It's the root cause of most app arrays. The level pickup uses a throwaway `PhysicsWorld` (4406). | #1–#7 components exist | Med–High (largest behavioural surface) |
| 17 | Terrain/foliage/grass/water/stream setup in `startScene` | SandboxApp.cpp:~4040–4300 (foliage density copy 4076–4086, water 4260–4275, streams cap 16 at 4282) | ~260 | `Terrain/WorldTerrain` or `Scene/SceneEnvironment` owning `TerrainGrid`, `GrassField`, foliage records, `WaterWorld`, streams | Same members exist in the Editor (EditorTerrain.cpp). Per-app copies mean lifetime fixes (TerrainGrid, water rebuild) have to be done twice. | — | Med |
| 18 | Vision modes (night sight / IR / UV / echo toggles, blend timers, chirp state) | SandboxApp.h:377–391; SandboxApp.cpp:188–193 (blend consts), 523–625 (toggles + blend), 2081–2087 (respawn reset) | ~120 | ECS: `VisionModeComponent{mode, blend, echoChirpAge…}` on the player; system `Play/VisionModes.cpp`; blend seconds into `content/render/echolocation.json` and a new `vision.json` | Mutual exclusion and blends are per-player rules. Would let NPCs or split-screen reuse it; the renderer just reads the component. | Echo/Tonemap pipelines | Low |
| 19 | Player combat transient state (`m_swingLock`, `m_comboWindow/Step/Count`, `m_faceYaw/Left`, `m_blockRecoil`, `m_hitStop`, `m_shake`, charges, `m_jumpAttackBuffer`) | SandboxApp.h:223–244, 410; logic in `updatePossessed` 1721–2051 and `updateCombat` 2583–2769 | ~520 | ECS: `PlayerCombatStateComponent`; system `Combat/PlayerCombat.cpp`; camera shake into a `CameraShakeComponent` | Not saved (save copies only look/run state, SandboxSave.cpp:242–277). It's a single player only and can't be replicated. | Combat, HoldCharge, Shield | High (the feel of combat; needs play-testing) |
| 20 | Player look/swim edges/wet/submerged (`m_lookYaw/Pitch`, `m_lowerBodyYaw`, `m_playerWet`, `m_playerSubmerged`, `m_swimDive/StandEdge`, `m_underAmount`) | SandboxApp.h:395–402; SandboxApp.cpp:1721–1760, 1869–1870, 1941, 1012–1030 | ~60 | Extend `Character/PlayerMotorComponent` / `Play/PlayLook.h` with a `PlayerLookComponent` | The save host already has to marshal look state through `Save::BodyLookState` (SandboxSave.cpp:247–252); a component removes that. | Save registry | Low–Med |
| 21 | `playerLightLevel` (sun ray vs `m_chase.cubes()`, lamp sum) | SandboxApp.cpp:1614–1665 | 52 | `AI/LightExposure.{h,cpp}` `sampleLightLevel(world, env, occluders, pos)` | Stealth perception input that only works for the player and only against chase cubes. The AI should sample the same function. | Sky Environment | Low |
| 22 | Health packs (`placeHealthPacks`, `updateHealthPacks`) | SandboxApp.cpp:3432–3484 | ~52 | `Gameplay/HealthPack` already exists; have the scene place `HealthPackComponent`s and tick via a system | Placement is hard-coded in the app. | #16 | Low |
| 23 | Respawn / death timers (`respawnPlayer`, `m_playerDeadTimer`, `m_spawnAge`, `kDeathSeconds`, `kSpawnFocusSeconds`) | SandboxApp.h:405–408; SandboxApp.cpp:188–189, 2053–2106 | ~60 | `Save/ProgressComponents.h` already has `RunProgressComponent` (written back in SandboxSave.cpp:254–259), so make it the source of truth; system `Play/Respawn.cpp`; constants to JSON | State is duplicated between app members and the component. | — | Low |
| 24 | Inventory side-effects (`giveItem` auto-equip, `dropInventoryItem`, `applyInventoryArmor`, `ownsWeapon` with `"sword"`/`"rifle"` literals, `m_wornArmor`) | SandboxApp.h:375; SandboxApp.cpp:2141–2154, 2209–2233, 2364–2423 | ~100 | `Gameplay/Inventory.cpp` (`giveItem(world, e, stack)` with an event); weapon→item mapping into item JSON (`weaponKind` field) | The rules are reusable; only the sound cue is app glue. | Event bus | Low |
| 25 | HUD/overlay drawing (inventory, quest HUD, journal, stamina, breath, armor, pickup labels, guard awareness, defense text, target marker, loot, NPC info, pause) | DevToolsPanel.cpp:110–947 | ~840 | `Ui/` module (`Ui/GameHud.{h,cpp}`) reading components; keep `drawDevTools` (949–1805) and `drawProfiler` in the app | The Editor has `drawPlayHud` (`EditorPlay.cpp:1386+`), so Editor Play shows a different HUD. | #3, #18–#20 | Low (ImGui only) |
| 26 | Render orchestration (`onRender`) | SandboxApp.cpp:4773–5972 (1,200 lines): shadows ~4950–5040, cull 5043–5100, GBuffer 5136–5365, deferred resolve 5370–5420, forward ~5420–5520, local lights 5564–5580, SSR capture/water 5597–5675, clouds 5677–5688, camouflage 5690–5710, translucent 5720–5770, debug paths/decals 5772–5775, particles 5780–5795, underwater 5801–5815 | ~1,200 | `Render/SceneRenderer` → `renderFrame(FrameInputs&)` driven by the `PassNode` tables in `Render/PassTable.cpp` (a pass-callback array per `PassId`) | The pass table already exists but is only an after-the-fact order check. Editor `EditorRender3D.cpp` (1,103 lines) runs a parallel frame. App-specific draws (projectiles 3486–3615, debug paths) register as callbacks. | All pipelines | High (D3D12 barriers/descriptors; must compile and run on PC) |
| 27 | Hard-coded terrain layer names/tiling and the `level.json` default | SandboxApp.cpp:160–186 (`kLayerNames`, `kTiling` 162–163), 268–277, 4341 (`m_demoScene = filename == "level.json"`) | ~40 | `content/terrain/material.json`; demo flags as scene JSON fields | Data in code; name-based behaviour switch | — | Low |
| 28 | Net pawn helpers (`spawnOwnedPawn`, `findPawn`, `ensureLocalCube`, `onNetSpawn/Despawn/Peer`, `pawnPaletteColor`) | SandboxApp.cpp:341–349, 3643–3749 | ~110 | `Network/` prefab spawn registry (`NetPrefab` → spawn fn) | The Editor has `EditorNet.cpp` (302 lines, not reviewed line by line); likely to overlap | — | Med |
| 29 | Save host callbacks | SandboxSave.cpp:30–319 | ~290 | Shrinks to ~60 once #18–#20 and #23 are components; `saveRespawn` (148–214) moves to the archetype spawn registry shared with #8/#16 | Most of it marshals app members ↔ components | #19, #20, #23 | Low once deps land |
| 30 | Physics bind helpers (`createPhysicsWorld`, `addChaseObstaclesToPhysics`, `bindEntityPhysics`, `bindCharacterPhysics`) | SandboxApp.cpp:3765–3843 | ~80 | `Physics/PhysicsBind` (already exists) + `NavObstacleComponent` → static body | Same job in `Editor/EditorPhysics.cpp` (530 lines, not reviewed line by line) | #13 | Low–Med |

---

## 4. Duplicates across Sandbox / Editor

| Logic | Sandbox | Editor | Diff status (measured) |
|---|---|---|---|
| `tickSpawners` | SandboxApp.cpp:2277–2311 | EditorPlay.cpp:1274–1309 | Identical apart from the guard condition and whitespace |
| `spawnSpawnerUnit` | 2243–2275 | 1236–1273 | Same shape; Editor uses its own `m_ai` |
| `tickCrows` | 2235–2241 | 1228–1235 | Same `simulateCrows(...)` call |
| `releaseThrowable` + `tickThrowableFlights` | 2851–2941 | 626–716 | ~59 diff lines, mostly renames |
| Impact-FX forwarding (`impactHost`, `deferredDecals`, `isStepCue`, footmarks, impacts, blood decals) | 3384–3430 | 578–625 | ~40 diff lines |
| Weapon query + weapon hit | 2456–2563 | 480–534 | Parallel implementations |
| Jump-attack resolve | 2771–2849 | 552–577 (`resolvePlayHits`) | Sandbox adds FX/shake; Editor is a subset |
| Fire loadout | 2943–2998 | 717–746 | Parallel |
| Hunter cue callback | 4500–4517 (lambda) | 541–551 | **Drifted**: Editor lacks `grunt` jump-anim and `pounce`/`pound` fallbacks |
| Hunter hit-reaction / armor stocking | 2313–2324, 4490–4494 | 433–444 (`equipPlayArmor`) | Parallel |
| Walkability bake | PathChase.cpp:81–89 | EditorPlay.cpp:248–301 | Two bake paths |
| Pass tables | `sandboxDeferredPasses/ForwardPasses` | `editorDeferredPasses/ForwardPasses` (PassTable.h) | Parallel frames, both hand-rolled |
| HUD | DevToolsPanel.cpp:110–947 | EditorPlay.cpp:1386+ `drawPlayHud` | Different HUDs |

**Missing from Editor Play entirely** (the gap ECS + shared systems would close): power/circuits, guard torches, quests/zones/banners, takedown/carry, hunter loot, vision modes, light level, and the underwater pass (it's in the editor pass table but never drawn, as noted in the last review).

---

## 5. Proposed components and systems (sketches)

```cpp
// Gameplay/PowerComponents.h
struct PowerBoxComponent {
    std::string circuit;
    bool  on = true;
    float fixFor = 0.0f;
    Entity fixer{};
    Math::Vector3f service{};
    // investigation after a fix
    Entity checker{};
    Math::Vector3f checks[3]{};
    int   checkIdx = 0;
    float checkFor = 0.0f, checkTime = 0.0f;
};
struct CircuitMemberComponent { std::string circuit; };   // on LocalLight entities
struct PowerTuning {   // content/gameplay/power.json
    float alertRadius = 30.0f, fixSeconds = 1.5f, fixReach = 2.5f, spotReach = 1.5f;
    float checkHold = 2.5f, checkTimeout = 15.0f; int checkCount = 3;
};
// Gameplay/PowerSystem.h
void setCircuit(World&, Entity box, bool on, bool alertGuards, const PowerTuning&, const AI::Walkability&, GameEvents&);
void tickPower(World&, float dt, const PowerTuning&, const AI::Walkability&, GameEvents&);

// Play/AttachedLights.h
struct AttachedLightComponent {
    Entity owner{};
    Math::Vector3f offset{0, 0.75f, 0.35f};
    float pitch = -0.28f;
    bool  nightOnly = true;   // enabled when sunElevation < threshold and owner alive
};
void tickAttachedLights(World&, const Sky::Environment&);

// Gameplay/Triggers.h
struct TriggerVolumeComponent { Math::AABox3f box; float padBelow = 1.5f, padAbove = 2.5f; };
struct QuestZoneComponent     { std::string name; bool inside = false; };
struct ConcealVolumeComponent {};          // grass patches; AI reads these instead of setConcealVolumes(vector)
struct NavObstacleComponent   { Math::AABox3f box; };
struct GoalVolumeComponent    {};

// Gameplay/QuestRuntime.h
class QuestRuntime {             // owns QuestTracker; one per session
public:
    void start(std::string_view id);
    void onEvent(const GameEvent&);          // power_off, takedown, body_hidden, item_changed
    void tick(World&, Entity player, float dt);  // zones, collect-from-inventory, rewards
    std::span<const QuestNoteEvent> notes() const;
    bool target(World&, Entity player, Math::Vector3f& out) const;
};

// Core/GameEvents.h  (tiny, no allocation per frame)
enum class GameEventKind : uint8_t { PowerOff, Takedown, BodyHidden, ItemChanged, ZoneEnter, ZoneExit };
struct GameEvent { GameEventKind kind; Entity a{}, b{}; uint32_t nameHash = 0; };
struct GameEvents { std::vector<GameEvent> queue; void push(GameEvent e){ queue.push_back(e);} };

// Play/Interaction.h
enum class InteractKind : uint8_t { Body, Power, Item, Loot };
struct InteractableComponent { InteractKind kind; float range = 2.2f; };
Entity pickInteractable(World&, Entity body, const Math::Vector3f& eye, const Math::Vector3f& look, float minAim = 0.94f);

// Play/Takedown.h
struct CarriedByComponent { Entity carrier{}; float floorOffset = 0.0f; };
struct FactionComponent   { uint8_t faction = 0; bool canBeTakenDown = true; bool carryable = true; }; // replaces tag->name == "Wolf"

// Gameplay/LootSystem.h
struct LootTableComponent { std::vector<ItemStack> stock; bool dropOnDeath = true; bool dropped = false; };
void tickLootDrops(World&, PlayHost&);

// Play/VisionModes.h
enum class VisionMode : uint8_t { None, NightSight, Infrared, Ultraviolet, Echo };
struct VisionModeComponent {
    VisionMode mode = VisionMode::None;
    float night = 0, ir = 0, uv = 0, echo = 0;   // blends
    float echoChirpAge = 0; bool echoHaveChirp = false;
    Math::Matrix4f echoChirpViewProj{}; Math::Vector3f echoChirpPos{};
};

// Combat/PlayerCombat.h
struct PlayerCombatStateComponent {
    float swingLock = 0, comboWindow = 0, blockRecoil = 0, faceYaw = 0, faceLeft = 0;
    int   comboStep = 0, comboCount = 0;
    float jumpAttackBuffer = 0;
    Combat::HoldCharge attackCharge{}, blockCharge{};
    bool  swingCharged = false;
};
struct CameraShakeComponent { float amount = 0, time = 0, hitStop = 0; };
struct PlayerLookComponent  { float yaw = 0, pitch = 0.18f, lowerBodyYaw = 0; };

// Play/PlaySession.h  — the shared host Sandbox and Editor Play both own
struct PlayHost { World& world; Renderer& renderer; AssetManager& assets; AssetPinTable& pins;
                  Audio::AudioSystem& audio; Physics::PhysicsWorld& physics;
                  const Terrain::TerrainGround* ground; Terrain::TerrainGrid* terrain; };
class PlaySession {
public:
    bool begin(PlayHost, const SceneData&);   // instantiateScene + nav bake + AI wiring
    void tick(float dt, Entity player, bool playerInWater); // fixed order below
    void end();
    AiSystem& ai(); QuestRuntime& quest(); GameEvents& events(); ImpactFxHost& fx();
private:
    // tick order: spawners → crows → AI hunters → combat → physics → statusFx →
    // healthPacks → loot → power → quest → carry → attachedLights → particles → anim
};

// Render/DebugDraw.h
class DebugLines { public: bool create(Renderer&); void line(Vec3, Vec3, uint32_t rgba);
                   void flush(ID3D12GraphicsCommandList*, Renderer&, const Matrix4f& viewProj); };
// ring sized by Renderer::framesInFlight(), not % 2
```

Every new component should be registered in the save serializer registry (`Save/Persist*.cpp`) and in scene JSON parse/write (`Scene/SceneFile.cpp`, Editor `EditorSceneFile.cpp`) using `Core/Json.h` checked helpers.

---

## 6. Phased migration plan

Each phase is a separate PR that leaves **Sandbox, Editor, Sandbox2D and UnitTests building**. "Compile" means MSVC on the Windows PC worker; Linux cloud agents can't verify these.

| Phase | Scope (items) | Value | Risk | Verification |
|---|---|---|---|---|
| **P0 – Shared play host** | Add `Play/PlaySession.h` with `PlayHost`, and move the identical functions first: spawners/crows (#8), impact-FX forwarding (#11), hunter cue (#12, take the Sandbox version and fix the Editor drift). Both apps call the shared code. | High (removes ~200 duplicate lines, fixes drift) | Low | MSVC compile; play both apps, check spawner caps and footmarks |
| **P1 – Event bus + quest runtime** | `GameEvents`, `QuestRuntime` (#3, #4), `TriggerVolume/QuestZone` components; scene parse creates zone entities; save registration for quest state | High (fixes collect farming, quests persist, Editor gets quests) | Med | MSVC + `UnitTests/Gameplay/QuestTests.cpp` extended (CPU only: a cloud agent can write tests, the PC must build) |
| **P2 – Power, torches, interaction, carry** | #1, #2, #5, #6, #7, `FactionComponent` replacing `"Wolf"` string checks | High (stealth loop becomes ECS/Editor-testable; fixes the `checker` frame bug in passing) | Med | MSVC; play-test guard fix/check, takedown, body-in-grass quest event |
| **P3 – Player state components** | #18, #19, #20, #23, #24; shrink `SandboxSave.cpp` (#29) | Med–High (saves stop marshalling app members) | High for #19 (combat feel) | MSVC; save→load round trip mid-combo, mid-swim, mid-echo |
| **P4 – Scene instantiation** | #16, #22, #27, #30: `Scene/SceneSpawn` used by Sandbox *and* Editor Play; removes the throwaway physics world for pickups | High (single source of truth for what a scene object means) | Med–High | MSVC; load `level.json` and other scenes in both apps, diff entity counts |
| **P5 – AI ownership / PathChase dissolve** | #13, #14: `AiSystem` moves into `PlaySession`, nav bake in `AI/Walkability`, walker becomes a fallback player-start; delete `Sandbox/PathChase.*` | Med | Med | MSVC + `UnitTests/AI/*` |
| **P6 – Debug draw + HUD** | #15 (`Render/DebugDraw`, frame-count ring), #25 (`Ui/GameHud`) shared with Editor Play | Med | Med (D3D12 buffers) | MSVC + run with D3D12 debug layer **and GPU-based validation** |
| **P7 – Render frame driven by pass table** | #26: turn `PassNode` tables into executable passes with callbacks; `SceneRenderer::renderFrame`; Sandbox keeps only app draw callbacks (projectiles, debug lines). Then do the same for `EditorRender3D.cpp`. Also: Editor draws underwater | High long-term | **High** | MSVC + HLSL (DXC) + GBV + PIX capture compare before/after for both deferred and forward paths |
| **P8 – Environment owner** | #17 terrain/grass/foliage/water/streams in `Scene/SceneEnvironment`, shared with Editor; fixes the double lifetime-fix problem | Med | Med (GPU lifetimes) | MSVC + GBV; resize/rebuild tests |
| **P9 – Net prefab registry** | #28 | Low–Med | Med | MSVC + host/join loopback |

Suggested order for value/risk: P0 → P1 → P2 → P4 → P3 → P5 → P6 → P8 → P7 → P9. P7 is the largest and riskiest; it's last so the frame inputs are already simple components by then.

---

## 7. Open questions

1. **Editor feature parity:** should Editor Play run the *full* Sandbox rule set (power, quests, carry), or should some stay Sandbox-only game rules? That decides whether #1–#7 go to the engine (`Gameplay/`, `Play/`) or a game-specific library (e.g. a new `Game/` folder/target linked by both apps).
2. **Target library:** `Play/` currently compiles into the `DarkEngine` umbrella (`DE_ENGINE_REST_FOLDERS`, DarkEngineTargets.cmake:118–134), and `Gameplay/` into `DarkGameplay` (111–115). Should power/quest runtime live in `DarkGameplay` (no renderer dependency) with the visual glue in `Play/`?
3. **Archetypes:** is there (or should there be) an NPC archetype JSON (hunter/wolf/guard) to hold torch params, loot stock, hit reaction and faction? Today these are spread across PathChase.cpp:93–98, SandboxApp.cpp:2313–2324, 4490–4494, 4551–4561.
4. **Event bus vs direct calls:** is a small per-frame `GameEvents` queue acceptable, or does Travis prefer the existing `void* user` callback style for quest hooks?
5. **ECS scheduling:** `ECS/World.h` has pools and `each<T>` but no system scheduler. Is a fixed tick order inside `PlaySession::tick` enough, or is a declared system list wanted?
6. **Multiplayer:** do vision mode, combat state and carry need replication? That affects whether they become replicated components (`Network/Replication.h`).
7. **Not reviewed line by line:** `Editor/EditorRender3D.cpp`, `EditorSpawn.cpp`, `EditorSceneFile.cpp`, `EditorPhysics.cpp`, `EditorNet.cpp`, `Render/PassTable.cpp` and `Render/SceneRenderer.cpp`. Their overlap with items #16, #17, #26, #28 and #30 is inferred from names and sizes and should be confirmed before P4/P7/P8.
8. **Sandbox/CMakeLists.txt** lists only `main.cpp` and `SandboxApp.cpp`, while `DarkEngineTargets.cmake:168` globs the whole `Sandbox/` folder. Which one is authoritative? It matters when new files are added or removed during the migration.
