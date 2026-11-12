# DarkEngine6 deep review — gameplay, shaders and subsystems (tip 35cf523)

Read-only review. Nothing compiled or run. Part A: gameplay. Part B: shaders and subsystems.

---

# DarkEngine6 gameplay review: main @ 35cf523 (Nov 11, 2026)

**Tip:** [`35cf523ec6b276e90bf769e1070de365f6c79251`](https://github.com/DarkenedGit/DarkEngine6/commit/35cf523ec6b276e90bf769e1070de365f6c79251) "Bug fix and refactoring", committed Nov 11, 7:47 PM PST.
**Scope:** gameplay code that earlier reviews didn't cover. That covers AI (perception, HSM brain, awareness, pathing, guards, hunters, wolves, spawners), combat (CombatSystem, parry/block, poise, status/CC, hit reactions, jump attacks, charged attacks), weapons (melee, projectile, throwables, hit queries), character (motor, stealth, exposure, health) and the save/play code that touches these systems.
**Method:** I read the source from the GitHub tarball for that SHA. **Nothing was built, run or profiled.** Each CONFIRMED item had its call path traced by reading the code: callers, guards and data defaults. Items that depend on runtime data, timing or content are listed under RISKS. Links point to the full SHA. All times are PST.

## 0. Commits since the last review (14945a3)

| SHA | When (PST) | Title |
|---|---|---|
| 385e7fe | Nov 9, 11:34 PM | Fix un-needed GPU drain |
| 20492d1 | Nov 10, 9:33 PM | Garbage collection between levels |
| 24d6562 | Nov 10, 9:45 PM | Fix Movie Playback request |
| 410f86a | Nov 10, 10:19 PM | Fix threading issues |
| 3fac168 | Nov 10, 10:49 PM | Fix locks |
| 35cf523 | Nov 11, 7:47 PM | Bug fix and refactoring (77 files; adds AI/HunterJump, Play/PlayBody, PlayFeedback, SpawnListed, Scene/WalkObstacles, ECS/FrameEntitySet, PoolIterate, tests) |

## 1. Status of the prior review's findings (14945a3)

The status comes from `docs/plans/2026-11-10-review-followup.md` in the repo. I spot-checked the main fixes in the source (eviction, movie UAV flags, pool-iterate guard, async-load generation, foliage phase lock, prev-world pruning, HunterJump data overlay, SpawnListed).

| Prior finding | Status at 35cf523 |
|---|---|
| P2-1 No production eviction | **Fixed**. Garbage collection runs between levels. This also closes R2. |
| P2-2 Movie BGRA typed-UAV | **Fixed** |
| P3-1 Particle pool `waitForGpu` mid-frame | **Fixed**. `prepare` now uses deferRelease, and `destroy` still waits, which is fine. The linear scan in `isAccepted` remains. |
| P3-2 `m_prevWorldByEntity` growth | **Fixed** |
| P3-3 Log capture race | **Fixed**, with a small accepted leftover |
| P3-4 Foliage phase race | **Fixed** |
| P3-5 Per-frame heap churn | **Fixed** on the hot path |
| R5 Pool mutation during `each` | **Partial**. There is a guard, but it asserts in Debug only. See R1 below for the related cached-pointer risk. |
| R6 Stale async load | **Fixed** with a generation stamp |
| R1 shadow worker, R3 heap Reset, R4 12-bit generation, R7 speech cancel, R8 single condvar, R9 log I/O under lock | **Deferred / open** |
| Upload ring / D3D12MA, runtime GPU stalls (camouflage, echo, underwater) | **Open** |
| `unordered_map` sparse set, host lifetime tokens | **Open** |
| §3.3 / §6 Editor-vs-Sandbox divergences 1–7 | **Fixed**: `spawnListedUnit`, `stepPossessedBody`, `PlayFeedback`, `WalkObstacles`, `QuestBanner`, `ActionMap::keyBracket`, `hunterJumpDefFromPattern`, dead Sandbox CMake removed, defense timer moved into update |
| Recommendations 1, 2, 6, 13 | **Partial** (PlayHost split, PlayActors) |
| Recommendations 3, 4 (GameData), 5, 7–12, 14, 15 | **Open** |
| Manual Editor play pass | Still needed. Nothing has been run. |

## 2. Confirmed bugs

### P1 — crash or corruption

**P1-1. Loading a save can empty the projectile pool, and the next rifle shot then writes out of bounds.**
- Save side: [`Save/PersistCombat.cpp#L359-L361`](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Save/PersistCombat.cpp#L359-L361) writes `projectile().live()`. Load side: [`#L468-L469`](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Save/PersistCombat.cpp#L468-L469) calls `restoreShots(shots)` whenever the array is present.
- [`Weapons/ProjectileWeapon.cpp#L237-L249`](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Weapons/ProjectileWeapon.cpp#L237-L249) `restoreShots` clears `m_shots` and pushes back only the alive shots. If no bullet was in flight at save time, the most common case, `m_shots` ends up empty.
- [`spawnShot` (#L114-L143)](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Weapons/ProjectileWeapon.cpp#L114-L143) assumes the pool was sized by `setDesc` (`maxLive` = 16, [ProjectileWeapon.h#L56](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Weapons/ProjectileWeapon.h#L56)). With an empty vector the free-slot loop never runs, `idx = oldest = 0`, and it writes `m_shots[0]`. That is heap corruption on the first non-instant shot after loading. `setDesc` is only called from the constructor, so nothing re-sizes the pool.
- If k shots were alive at save time, the pool is permanently shrunk to k, and later shots recycle live bullets early.
- **Fix:** in `restoreShots`, `resize(maxLive)` first, then copy the alive rows into slots. Add a unit test for save → load → fire.

### P2 — visibly wrong behaviour or exploits

**P2-1. Hunter and wolf ground pounds damage their own pack, and the result goes through the *player's* hit handler.**
- [`JumpAttack::tryPound` (Combat/JumpAttack.cpp#L416-L470)](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Combat/JumpAttack.cpp#L416-L470) excludes only the attacker. Its targets come from [`AiSystem::collectJumpTargets` (#L1075-L1095)](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/AiSystem.cpp#L1075-L1095), which returns every `HittableComponent`. Hunters and wolves get one ([#L255-L260](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/AiSystem.cpp#L255-L260)).
- Teams are never set: `teamFilter` defaults to 0 ([JumpAttackDef.h#L52](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Combat/JumpAttackDef.h#L52)). The team check in [CombatSystem.cpp#L67-L72](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Combat/CombatSystem.cpp#L67-L72) also needs a `DefenseComponent`, and only the player has one ([Shield.cpp#L137](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Combat/Shield.cpp#L137)).
- Every pounce lands into Pound ([AiSystem.cpp#L1504-L1518](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/AiSystem.cpp#L1504-L1518)), so pack-mates within 3.5 m take 14 damage plus HardCc.
- The hits are routed to `PlaySession::onJumpHits` ([PlaySession.cpp#L1438](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Play/PlaySession.cpp#L1438)) → [`resolvePlayerJumpHits` (PlayerCombat.cpp#L405-L485)](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Play/PlayerCombat.cpp#L405-L485). For a hunter victim, that handler calls `onHunterAttacked(victim, playerPos)`, which pulls allies toward the *player*. On a kill it calls `onHunterKilled`, which makes witnesses flee from the player. Crows are Hittable too and get hit as well.
- **Fix:** give hunters, wolves and the player a team id and pass it as `teamSource`, or filter `collectJumpTargets` by faction. Split the AI jump-hit callback from the player's.

**P2-2. Poise never regenerates, so poise breaks only once per life.**
- [`PoiseComponent::tick` (Combat/PoiseComponent.h#L42)](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Combat/PoiseComponent.h#L42-L52) has no production caller. A repo-wide grep finds only its definition.
- `applyPoiseDamage` ([#L29-L40](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Combat/PoiseComponent.h#L29-L40)) reports a break only on the transition from up to broken. After the first break, no hit upgrades to Critical severity again.
- Blocked hits still deal poise damage ([CombatSystem.cpp#L281-L284](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Combat/CombatSystem.cpp#L281-L284)).
- Hunter respawn doesn't reset poise ([AiSystem.cpp#L465-L529](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/AiSystem.cpp#L465-L529)).
- **Fix:** tick poise for every `PoiseComponent` once per frame, for example in `CombatSystem::update`, and reset it on respawn.

**P2-3. Weapon hit queries ignore world geometry: bullets and swords pass through walls, buildings and closed doors.**
- [`WeaponWorldQuery` (Weapons/Weapon.h#L59-L76)](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Weapons/Weapon.h#L59-L76) only has terrain raycast, terrain height and targets.
- [`weaponRaycastClosest` (Weapon.cpp#L327-L385)](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Weapons/Weapon.cpp#L327-L385) and [`weaponSweepClosest` (#L387-L486)](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Weapons/Weapon.cpp#L387-L486) test only targets and terrain. [`makePlayerWeaponQuery` (PlayerCombat.cpp#L132-L144)](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Play/PlayerCombat.cpp#L132-L144) binds only terrain hooks.
- By contrast, throwables do collide with scene cubes and spheres ([#L158-L165](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Play/PlayerCombat.cpp#L158-L165)). Hunter melee ([`releaseMelee`, AiSystem.cpp#L389-L440](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/AiSystem.cpp#L389-L440)) also has no LOS check.
- **Fix:** add `solids` (cubes, spheres, physics bodies, doors) to `WeaponWorldQuery`, shared with `ThrowableFlight`, and clip the hit distance against it.

**P2-4. Melee reach is horizontal only, so you can hit from directly above or below.**
- [`weaponPickCapsuleReach` (Weapon.cpp#L264-L325)](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Weapons/Weapon.cpp#L264-L325) gates on horizontal distance and facing only. The vertical delta is computed but never used.
- The `MeleeWeapon` contact fallback ([MeleeWeapon.cpp#L83-L100](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Weapons/MeleeWeapon.cpp#L83-L100)) flattens the distance.
- Hunter `releaseMelee` uses XZ distance with `minDot = -1`, so it hits at any angle, including behind.
- Result: you can swing from a roof or ledge at a hunter below, and hunters below can hit you. `JumpAttack::contactBody` does check vertical slop ([JumpAttack.cpp#L59-L147](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Combat/JumpAttack.cpp#L59-L147)), so the gate exists elsewhere and can be shared.

**P2-5. Corpses (and the first 8 agents, alive or dead) permanently block A* paths.**
- [`AiSystem::repath` (#L901-L917)](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/AiSystem.cpp#L901-L917) stamps the first 8 entries of `m_scratch` as obstacles. There is no alive check and no nearest-first ordering.
- The pathfinder *skips* agent cells instead of giving them a cost ([Pathfinder.cpp#L204-L210](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/Pathfinder.cpp#L204-L210)). So a dead hunter lying in a doorway or a 1-cell corridor makes every path through it fail. The agent sets `givenUp` and falls back to straight-line seeking into the wall.
- **Fix:** skip dead or carried bodies, pick the nearest N agents, and cost agent cells instead of skipping them.

**P2-6. Contact damage ("grind") comes from stunned, knocked-down or mid-recovery hunters, through walls, and from below.**
- [`applyContactGrind` (Play/ContactGrind.h#L23-L68)](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Play/ContactGrind.h#L23-L68) damages the player at 12 DPS from *every* alive, non-passive `AiAgentComponent` within 2.25 m in XZ.
- It never checks the hunter's `HitReaction` stun, StatusEffect CC, jump phase or parry stagger. It also has no LOS or vertical test.
- So a hunter you just parried or knocked down still hurts you, and several hunters stack. `passive` is only true for unalerted guards ([AiSystem.cpp#L1724](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/AiSystem.cpp#L1724)), so calm wolves grind too.
- **Fix:** skip agents that are stunned, CC'd or in Recover, and add a vertical and occlusion gate.

**P2-7. Takedowns ignore the player's own state.**
- [`tryTakedown` is polled before and outside the `canSteer` gate (PlaySession.cpp#L670-L671)](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Play/PlaySession.cpp#L670-L671). [`findTakedownTarget` (Interaction.cpp#L123-L152)](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Play/Interaction.cpp#L123-L152) checks only the guard: alive, not Alert, behind it, within 2.5 m and 1.2 m of height.
- So a player who is hidden in a hide spot, hard-CC'd or stunned, swimming, mid-air, or inside the death timer can instakill an unaware guard. Nothing checks for a wall between them.
- **Fix:** require player alive, grounded, not hidden, not CC'd, and a clear segment.

**P2-8. Wolves use the hunter's jump attack, so `wolf.attacks.json` pounce values are ignored.**
- [`attachHunter` (AiSystem.cpp#L294-L300)](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/AiSystem.cpp#L294-L300) always calls `setDef(hunterJumpDefFromPattern(m_hunterAttacks))`. Wolves are spawned through the same path ([SpawnListed.cpp#L66-L70](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Play/SpawnListed.cpp#L66-L70)).
- So the wolf's pounce windup of 0.28 s, damage 16 and stun 0.45 never apply. The hunter's 0.40 telegraph and 22 damage are used instead.
- The pattern path ([AiSystem.cpp#L1871-L1905](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/AiSystem.cpp#L1871-L1905)) then mixes wolf ranges with the hunter definition.
- `stunSeconds` and `poise` from JSON are dropped for every jump ([HunterJump.cpp#L52-L64](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/HunterJump.cpp#L52-L64)). The follow-up doc acknowledges this for hunters only.
- **Fix:** pass the agent's own `attackPatternFor()` into `hunterJumpDefFromPattern` and map stun and poise.

### P3 — minor, performance or UB that is benign today

**P3-1. `m_scratch` is cleared and refilled while it is being range-iterated (UB).**
- `tickHunters` iterates `for (Entity e : m_scratch)` ([AiSystem.cpp#L1443](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/AiSystem.cpp#L1443)).
- On the pound path from P2-1, `resolveJumpHits` → the player callback → [`onHunterAttacked` / `onHunterKilled` (#L2404-L2447)](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/AiSystem.cpp#L2404-L2447) → `collectHunters()` (`m_scratch.clear()` + refill).
- Today the refill has the same contents and capacity, so it is benign, but it is formally UB and fragile. Fixing P2-1 removes the trigger. Iterating over a copy, or using a separate scratch vector for the reentrant callers, makes it robust.

**P3-2. A failed repath retries A\* every frame with no backoff.** On failure, [`repath`](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/AiSystem.cpp#L926-L931) clears the points and doesn't push `repathAt`. Chase repaths whenever `points.empty()`. While the player is unreachable (another island, a roof, the P2-5 corpse block), each hunter runs a full-grid-allocating A\*, possibly with a full-grid `snapDest` scan, every frame.

**P3-3. Every hunter that picks a wander destination in the same frame picks the same offset.** [`pickWanderDest` (#L939-L947)](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/AiSystem.cpp#L939-L947) seeds `mt19937` from `m_time*1000 + 17` alone. Agents re-targeting together walk in parallel "formation". Mixing in the entity id fixes it.

**P3-4. God mode doesn't stop drowning.** [`PlaySession.cpp#L925-L926`](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Play/PlaySession.cpp#L925-L926) calls `hp->applyDamage(8*dt)` directly, bypassing CombatSystem's god/reaper handling. Player weapon hits on hunters use `applyHunterDamage` ([PlayerCombat.cpp#L359-L360](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Play/PlayerCombat.cpp#L359-L360)), and hits on others use raw `applyDamage` (#L392-L402). Both skip armor, status, poise and iframes. See Duplication.

**P3-5. The delayed melee strike uses where the swing started, not where the player is now.** `MeleeWeapon` stores the request at swing start and resolves it `hitDelay` (0.12 s) later with the old `ownerPos` ([MeleeWeapon.cpp#L25-L39, #L161-L173](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Weapons/MeleeWeapon.cpp#L161-L173)). When sprinting, that is about 0.6–0.8 m of error.

**P3-6. Wolves (and hunters when awareness is off) respawn somewhere random within ±22 m of the world origin.** [`tickHealthAndRespawn` (#L484-L501)](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/AiSystem.cpp#L484-L501) ignores spawners and home points. It doesn't reset poise, armor or the `bodyFound` / `missingReported` flags.

**P3-7. A HitReaction overwrites the current stun.** `m_stunLeft = settings.stunSeconds` ([HitReaction.cpp#L38](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Character/HitReaction.cpp#L38)), so a light hit can *shorten* a long stun from a parry. It should be `max(...)`.

**P3-8. Gunfire is silent to AI.** The only `addNoise` call in the codebase is footsteps ([Exposure.cpp#L100](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/Exposure.cpp#L100)). Rifle shots, bullet impacts, melee clashes and bodies dropping emit no stimulus, and the muzzle light is excluded from the exposure sample. If this isn't intentional, it is a stealth exploit.

## 3. Risks and suspicions (not proven reachable, or dependent on data or timing)

- **R1. Cached component pointers across world mutation.**
  - `tickPawn` caches `xf`, `hc`, `hr`, `pmc`, `jac`, `status` and `poise` ([PlaySession.cpp#L610-L661](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Play/PlaySession.cpp#L610-L661)). It then calls `tryPickup` → `destroyPickup` → `destroyEntity` (swap-remove), `tryTakedown` and `pickInteract`, and keeps using `xf` until about L1005.
  - `lootHunters` reads `xf->position` after `removeArmor` destroys entities (#L1269-L1270).
  - This is only UB if a pool of the same type reallocates or swaps the owner's slot. Pickups probably don't own a TransformComponent pool slot that moves the player's slot, but it's unproven. The safe options are to re-fetch after mutations or use a deferred-destroy queue.
- **R2. Shield blocking is free and total.** `syncShieldDefense` sets `blockStaminaCost = 0` ([Shield.cpp#L115-L121](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Combat/Shield.cpp#L115-L121)), and `equipPlayerShield` sets `blockMitigation = 0` (#L132-L143). A raised shield takes 0 damage with no stamina drain or guard break from every CanBlock hit in a 120° arc, which is all hunter attacks. Every raise opens a parry window with no cooldown ([PlaySession.cpp#L712-L713](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Play/PlaySession.cpp#L712-L713)). This may be a design choice, but it's exploitable as written.
- **R3. Dodge has no i-frames.** `DefenseComponent::beginIFrame` ([DefenseComponent.h#L52](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Combat/DefenseComponent.h#L52-L56)) has no callers, so the iframe branch in `resolve` is dead.
- **R4. Awareness `falloff = 0` produces NaN or infinity.** It computes `exp(-d / falloff)` ([Awareness.cpp#L38](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/Awareness.cpp#L38)), and validation allows 0 (#L163).
- **R5. NaN damage kills.** The `amount <= 0` guard in `Health::applyDamage` ([Health.cpp#L42-L52](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Character/Health.cpp#L42-L52)) and `resolveDirect`'s `amount < 0` don't reject NaN, which can come from bad JSON or a zero-length normalize upstream.
- **R6. Pathfinder cost.** Every request allocates g, parent and closed arrays the size of the whole grid ([Pathfinder.cpp#L170-L173](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/Pathfinder.cpp#L170-L173)). `snapDest` scans the full grid for an unwalkable destination (#L82-L121). With the 4096-expansion cap, long paths fail and fall back to straight-line seeking.
- **R7. AnimGraph triggers never expire.** They are consumed only when a transition fires ([AnimGraph.cpp#L278-L287, #L379-L386](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Animation/AnimGraph.cpp#L278-L287)), so a "hit" or "swing" latched in a state without that transition fires later, out of context.
- **R8. The Leap phase has no timeout** ([JumpAttack.cpp#L332-L334](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Combat/JumpAttack.cpp#L332-L334)). If `onLanded` never arrives (stuck on geometry, falling into water), the agent stays in Leap and holds its attack token.
- **R9. Sight and hiding.** Non-guard agents (wolves, and hunters with awareness off) use a flat-cone fallback that skips the heightmap LOS ([AiSystem.cpp#L1563-L1578](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/AiSystem.cpp#L1563-L1578)). Hiding only zeroes the exposure ([Exposure.cpp#L87](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/Exposure.cpp#L87-L93)), so those agents can still track a hidden player. Melee windups don't re-check hidden or LOS before release.
- **R10. Determinism.**
  - `m_time` is an accumulating float, and duel pauses and wander use `fmod(m_time…)`, `e.id()` hashes and time-seeded `mt19937` (L487, L941, L1846, L2107, L2349).
  - `Math::RandF` uses global `rand()`, which is shared and not thread-safe, and its range is [0,1] inclusive despite a comment saying it is half-open.
  - AI assumes one player (`m_prey`, `m_playerExposure`).
  - None of this replays or networks deterministically.
- **R11. Spawning has no walkability or overlap test.** `spawnListedUnit` places units at terrain + 0.5 in a disc (`sampleDisc`, [Spawner.cpp#L41-L49](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Play/Spawner.cpp#L41-L49)), so a unit can land inside a building or in water.
- **R12. Immune CC applications still extend the diminishing-returns window** ([StatusEffectComponent.h#L114-L125](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Combat/StatusEffectComponent.h#L114-L125)), so constant pressure can keep a target immune indefinitely.
- **R13. Agent height is terrain + 1.** Hunter Y comes from terrain height ([AiSystem.cpp#L555, #L590](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/AI/AiSystem.cpp#L555)). There are no floors, roofs or interiors, which matters for P2-4 and P2-6.
- **R14. Crow simulation allocates every frame.** It builds vectors and gathers perches each frame ([Crow.cpp#L843-L913](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Play/Crow.cpp#L843-L913)).
- **R15. Rifle origin.** It starts 2.2 m ahead of the camera along the look direction ([WeaponFire.h#L20](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Play/WeaponFire.h#L20)). With a long camera boom, a target touching the player can sit behind the origin. With a short boom (camera against a wall), the origin can be past the target.

## 4. Hard-coded gameplay values (new; the prior review §5.1 already lists the item, status, armor, jump and contact tables)

| Area | Where | Values |
|---|---|---|
| Hunter stats | AiSystem.cpp L238-247 | HP 48, regen 5/s, regen delay 4 s |
| Duel / standoff | L1439, L1843-1846 | standoff 2.25, engage 4, ring 3.2, pause 1.5–3 s |
| Jump ranges | L1871-1874 | 4 / 9 m (hunter, also used for wolves) |
| Respawn | L484-499 | 8 s, ±22 m square around origin |
| Missing / body | L1715, L2221-2229 | body alert 15 m, missing 30 s / 40 m |
| Patrol, melee | L2046, L402 | patrol wait 1.5 s, reach +0.35 |
| Eye / ground | L555, 590, 999, 1555 | eye +0.5, ground +1.0 |
| Hunter defense (`respondToMelee`) | L2341-2372 | block chance 0.35, guard 0.8/1.0, 3-hit poise, window 1.5, recover 0.7 |
| Wander / flee | L942, L974-975 | ±18 m, min 4 m; flee 8–22 m, laterals ±8 |
| Parry | CombatSystem.cpp L137-168 | stun 0.9/1.8, knockback 2.4/0.8, 0.22/0.12 s |
| CC defaults | CombatSystem.cpp L301-356 | 0.8/1.2/1.4 s, shock hitch 0.25 |
| Hit feel | PlayerCombat.cpp L86-107, L348-373 | hit-stop 0.05–0.12, shake 0.06–0.09, block recoil 0.55, face 0.15, parry interrupt 1.2 |
| Combo | PlayerCombat.cpp L233-236 | combo window 0.9, swing lock 0.45 |
| Movement mods | PlaySession.cpp L736-738, L978 | swing slow 0.4/0.45/0.8, carry 0.6, footstep 0.35/0.55 |
| Drowning | PlaySession.cpp L926 | 8 hp/s |
| Takedown | Interaction.h L54, Interaction.cpp L143-146 | 2.5 m, 1.2 m height, back dot −0.2 |
| Player jump / sword | CharacterAttach.cpp L277-294 | cooldown 1.25, hitDelay 0.12 |
| Shield | Shield.h / Shield.cpp | 0.2 / 0.55 / 120°, stamina 0, mitigation 0 |
| Light sampling | Exposure.cpp L20-58 | sun occlusion 0.3, 80 m, ambient floor 0.12 |
| Weapon query | PlayerCombat.cpp L142, WeaponFire.h L20 | max range 90 m, muzzle offset 2.2 m |
| Pathfinder | Pathfinder.cpp | agent radius 0.8, expansion cap 4096, 8 agent stamps |
| Crows | Crow.cpp | fall 9 m/s |

All of these should go into the proposed `content/tuning/*.json` (prior review §5.7), and the AI ones should go into the `*.attacks.json` / `*.hsm.json` the code already loads.

## 5. Duplication

1. **Two damage pipelines.** Player melee and projectiles go through `applyHunterDamage` or raw `applyDamage`. Jump, pound, hunter melee and contact go through `CombatSystem::resolve`. The existing `Combat/WeaponHitAdapter.h` (`damageEventFromWeaponHit`) and `AttackDef` would unify them, but nothing calls them (EditorPlay only `#include`s the header). `Combat/SpellCaster` is only used in tests.
2. **Two stun systems.** `HitReaction` stun and StatusEffect hard CC each gate movement and attacks separately. `punishParry` saves, overrides and restores HitReaction settings to fake a longer stun (CombatSystem.cpp L157-168).
3. **Target gathering.** `WeaponTargetList::gather` duplicates `AiSystem::collectJumpTargets` plus the lambda query (AiSystem L1404-1437).
4. **Jump attack setup.** The hunter-default path and the pattern path are near-duplicates (AiSystem L1882-1904 vs L1929-1952).
5. **Armor handling** is duplicated across `stockHunter`, `lootHunters` and `removeArmor`.
6. **Vertical/LOS gates.** One exists in `JumpAttack::contactBody`; melee, contact grind and takedown each have their own or none.

## 6. Gaps against modern practice

| Area | Today | Modern target |
|---|---|---|
| AI decision | `tickHunters` is about 800 lines of hand-written logic around a 5-leaf HSM | Behaviour tree or utility scorer with blackboard; HSM leaves as BT subtrees; perception as a stimulus system (sight/sound/damage/ally events) with decay |
| Perception | Footsteps are the only noise; flat cone fallback; single player | Typed stimuli (gunfire, impact, body, door), per-agent memory, multi-target |
| Navigation | 2D grid A\*, y = terrain + 1, agents as hard blockers, no backoff | Recast/Detour-style navmesh with off-mesh links (ledges, doors), crowd avoidance (RVO), time-sliced path queue |
| Hit detection | 7 capsule hurtboxes, but weapons use reach-plus-dot and ignore geometry | Animation-driven weapon hitbox sweeps (per-frame swept volumes from bone sockets) against hurtboxes, occluded by the world; per-swing hit lists |
| Combat model | Unified `CombatSystem` exists but the player bypasses it; no teams; poise never regenerates; no i-frames, guard break or stamina | Single DamageEvent pipeline for every source; factions; poise/stamina economy; i-frame/hyper-armor windows authored on animation |
| Abilities | Tuning in C++; `AttackDef` and SpellCaster unused | Data-driven ability/attack assets (GAS-style: costs, cooldowns, tags, effects) loaded from JSON |
| Animation | AnimGraph with latched triggers and no expiry; gameplay timing separate from animation | Trigger lifetimes, anim notifies driving hit windows and footstep noise, root motion for lunges and leaps |
| Determinism / net | Float accumulated time, `rand()`, time-seeded RNG, id hashes | Fixed-step simulation tick, per-system seeded RNG, double or integer time, authority/replication boundaries |

## 7. Suggested fix order

1. **P1-1**: `restoreShots` resizes to `maxLive`. Add a save → load → fire test.
2. **P2-1 + P3-1**: factions/teams for jump and pound, a separate AI jump-hit callback, and a reentrancy-safe `m_scratch`.
3. **P2-2**: tick poise and reset it on respawn.
4. **P2-3 + P2-4**: world solids in `WeaponWorldQuery`, plus a shared vertical/LOS gate. Also apply it to contact grind (P2-6), takedown (P2-7) and hunter melee.
5. **P2-5 + P3-2**: path stamps skip dead agents, agents become costs instead of hard blocks, and failed repaths back off.
6. **P2-6 / P2-7**: state gates on contact grind and takedown.
7. **P2-8**: per-agent jump def from its own attack pattern, including stun and poise.
8. Route **all** damage (player weapons, drowning) through `CombatSystem` via `WeaponHitAdapter` (fixes P3-4). Merge the stun systems (P3-7).
9. Design pass: shield stamina and guard break, parry cooldown, dodge i-frames (R2/R3), and gunfire/impact noise (P3-8).
10. Safety: re-fetch pointers or defer destroys (R1), NaN guards (R4/R5), Leap timeout (R8), trigger expiry (R7).
11. Performance and quality: pathfinder scratch reuse, a navmesh, data-driven tuning (§4), and determinism (R10).

---
*Read-only review. Nothing was built, run or profiled. No repo was cloned and nothing was posted to GitHub.*

---

# DarkEngine6 — Exhaustive Review #2 (shaders + untouched subsystems)

- **Repo:** DarkenedGit/DarkEngine6, `main`
- **Tip SHA:** `35cf523ec6b276e90bf769e1070de365f6c79251`, "Bug fix and refactoring". Committed Nov 11, 2026 at 7:47 PM PST.
- **Scope:**
  - All 73 HLSL files (8,557 lines) under `content/shaders`, checked against their C++ pipelines.
  - Networking, Audio, UI/ImGui, Input, and the Terrain/Water/Foliage/Cloud CPU code.
- **Not repeated:** anything already in `DarkEngine6-review-14945a3.md`. That covers threading, memory, architecture, FXC/DXC, the PSO cache, the `FailedHr` copies, fullscreen root-signature duplication, audio R2, hard-coded ports and cue banks.
- **Method:** read-only, from a source tarball. **Nothing was compiled or run.**
  - Every cbuffer was checked twice: an HLSL packer script computed per-member offsets, and these were compared with the C++ struct offsets.
  - Each CONFIRMED item was traced through its callers and the C++ fill code.
- **Priorities:**
  - P1 = crash, corruption or security
  - P2 = visibly wrong
  - P3 = minor or perf

Link base: `https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/`

---

## 1. Confirmed bugs

### P1
None found. The network parsing paths are bounds-checked (Packet, parseDatagram, Reliability receive), and so are all terrain and foliage sidecar loaders.

### P2 (visibly wrong)

**P2-1. Water cbuffer layout mismatch between HLSL and C++**
- **Where:**
  - [Water.hlsl#L66-L73](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/content/shaders/Water.hlsl#L66-L73)
  - [WaterPipeline.h#L23-L75](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Render/WaterPipeline.h#L23-L75)
- **What happens:**
  - `float3 lightColor` would start at byte 328 and straddle a 16-byte boundary, so HLSL moves it to 336. C++ keeps it at 328.
  - Every member after it is shifted by 8 bytes. HLSL reads:
    - `lightColor` = {C++ lightColor.z, heightOriginX, ambient.x}
    - `ambientColor` = {ambient.z, heightOriginZ, heightCellSize}
    - `heightOriginX` = ambient.y
    - `heightOriginZ` = heightWorldSizeX
    - `heightCellSize` = heightWorldSizeZ
    - `heightWorldSizeX/Z` = padding, which is always 0
- **Visible effects:**
  - `SampleBed` always fails, so lakes get no shore foam, shoaling or bank chop.
  - The water light and ambient tint is wrong.
  - The volumetric-fog terrain lookup on water is broken, and the underwater ceiling fog is wrong.
- **Why tests missed it:** the static_asserts and unit tests only check C++ offsets, never HLSL packing.
- **Fill sites:**
  - [Water.cpp#L368-L387](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Water/Water.cpp#L368-L387)
  - [WaterBody.cpp#L508-L519](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Water/WaterBody.cpp#L508-L519)
  - [WaterStream.cpp#L373-L384](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Water/WaterStream.cpp#L373-L384)
  - [WaterPipeline.cpp#L654-L660](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Render/WaterPipeline.cpp#L654-L660)
- **Fix:** insert an explicit `float2 pad` after `volumetricHeight` in both HLSL and C++, or reorder the members. Then add an automated HLSL-reflection offset test (see §5).

**P2-2. TAA, SSR and GTAO history reprojection includes the projection jitter**
- **Where:**
  - [SceneRenderer.cpp#L381-L400](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Render/SceneRenderer.cpp#L381-L400)
  - [BasicMeshGBuffer.hlsl#L52-L53](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/content/shaders/BasicMeshGBuffer.hlsl#L52-L53), [L84](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/content/shaders/BasicMeshGBuffer.hlsl#L84)
  - [Taa.hlsl#L59-L65](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/content/shaders/Taa.hlsl#L59-L65)
- **Cause:**
  - `m_prevViewProj = viewProj` stores the *jittered* matrix, because `Camera3D::GetViewProj` uses the jittered `m_Proj`.
  - The G-buffer velocity is therefore computed from the jittered current matrix minus the jittered previous matrix. ModelDraw.cpp L139 does the same.
- **Effect:**
  - In a fully static scene, velocity equals the frame-to-frame jitter difference.
  - TAA (`histUv = uv - vel`) re-samples history bilinearly at a moving sub-pixel offset every frame, which causes softening, wobble and loss of convergence.
  - SSR temporal (Ssr.hlsl L120-121) and GTAO temporal (Gtao.hlsl L161-162) inherit the same error.
- **Fix:** keep unjittered current and previous view-projection matrices for velocity (or subtract the jitter delta in the shader). Use jittered matrices only for SV_Position.

**P2-3. SSR compositing leaks IBL and loses metal tint**
- **Where:**
  - [SsrMarch.hlsli#L146-L148](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/content/shaders/SsrMarch.hlsli#L146-L148)
  - [DeferredLighting.hlsl#L148-L162](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/content/shaders/DeferredLighting.hlsl#L148-L162)
- **Cause:** confidence has a scalar Fresnel `lerp(0.04,1,metallic)` baked into it, and the composite is then `lerp(specIbl, ssr, conf)`.
- **Effects:**
  - On dielectrics, a confident SSR hit replaces only about 4% of the IBL. Sky reflection shows through occluders, and the reflection is roughly double-counted.
  - On metals, SSR radiance is not multiplied by F0 or albedo, so gold and copper turn grey wherever SSR hits.
- **Fix:** keep confidence as pure hit confidence, then use `spec = lerp(prefilteredIbl, ssrRadiance, hitConf) * (F0*dfg.x + dfg.y)`.

**P2-4. Remote entities move in 20 Hz steps (snapshot interpolation is effectively off)**
- **Where:**
  - [NetInterpolation.cpp#L52-L135](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Network/NetInterpolation.cpp#L52-L135)
  - [NetInterpolation.cpp#L137-L166](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Network/NetInterpolation.cpp#L137-L166)
- **Cause:**
  - The render time is the integer `m_latestRecvTick - kNetInterpDelayTicks` (2). It advances only when a snapshot arrives, with no fractional client clock.
  - The slot at exactly d == delay gives `older == newer`, so t = 0.
- **Effect:**
  - Poses snap once per snapshot (20 Hz) and freeze completely during packet loss.
  - On the host, remote pawns are written straight from PawnState and are not interpolated either.
- **Test coverage:** the only test is `InterpolationWritebackOnFlushSkipsLocalOwner`. Nothing tests that interpolation is smooth.
- **Fix:** add a client render clock that advances by dt (with drift correction) and compute a fractional `renderTick`.

**P2-5. Alt+F4 (and the Alt+Space system menu) do nothing**
- **Where:** [Window.cpp#L167-L187](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Core/Window.cpp#L167-L187)
- **Cause:**
  - `WM_SYSKEYDOWN` and `WM_SYSKEYUP` always `return 0`, so `DefWindowProcA` (L292) never sees them and never generates `WM_SYSCOMMAND/SC_CLOSE`.
  - The ImGui hook does not consume key messages, and no app-level Alt+F4 handling exists (grep: none).
- **Fix:** forward to the input system, then `break` to `DefWindowProc` for `WM_SYS*` messages. At minimum do this for VK_F4 and VK_SPACE.

### P3 (minor or perf)

**P3-1. Shadow comparison samplers use an OPAQUE_WHITE border under reverse-Z**
- **Where:**
  - [DepthState.h#L8](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Render/DepthState.h#L8), [L30-L33](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Render/DepthState.h#L30-L33)
  - [DeferredLightingPipeline.cpp#L114-L115](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Render/DeferredLightingPipeline.cpp#L114-L115)
  - [LocalLightVolumePipeline.cpp#L128-L129](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Render/LocalLightVolumePipeline.cpp#L128-L129)
  - The same sampler is used in the Water, Terrain, Mesh, Skinned, InstancedMesh, SkinnedInstanceBuffer and Sky pipelines.
- **Cause:** `kDepthClear = 0` with GREATER_EQUAL means a border depth of 1.0 is the near plane, so out-of-map taps read as occluded.
- **Effect:** dark 1-texel seams at point-light cube-face edges and at cascade edges. LocalShadow.hlsli 3x3 PCF (L94-107) runs off the exact 90° faces (LocalShadowMath.cpp L63-77).
- **Fix:** use an OPAQUE_BLACK border, or add a ≥1-texel guard band (FOV slightly over 90°).

**P3-2. Water sun specular ignores light colour.** [Water.hlsl#L549-L552](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/content/shaders/Water.hlsl#L549-L552) uses a constant `0.85` white. At night `lightDir` is the moon ([Environment.cpp#L225-L227](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Sky/Environment.cpp#L225-L227)), so the moon gets a full-strength white sun glint.

**P3-3. Normals use the world matrix, not its inverse transpose**
- **Where:** [BasicMeshGBuffer.hlsl#L55-L56](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/content/shaders/BasicMeshGBuffer.hlsl#L55-L56). The same pattern is in all mesh G-buffer and forward shaders.
- **Why it matters:** non-uniform scale is allowed ([Components.h#L25-L31](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/ECS/Components.h#L25-L31)), and normals come out skewed on non-uniformly scaled curved or slanted meshes.

**P3-4. Cloud volumes are composited in ECS order, not depth order.** [CloudVolume.cpp#L87-L108](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Sky/CloudVolume.cpp#L87-L108) and [CloudVolume.hlsl#L438-L499](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/content/shaders/CloudVolume.hlsl#L438-L499). Overlapping volumes can composite in the wrong order.

**P3-5. Decals sample mip 0 only.** [Decal.hlsl#L126](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/content/shaders/Decal.hlsl#L126) and [L167](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/content/shaders/Decal.hlsl#L167) use `SampleLevel(...,0)`, so distant decals alias and shimmer. Use `SampleGrad` with projected UV derivatives.

**P3-6. Host PawnState validation drops or rewinds valid moves**
- **Dropped moves:** the speed check ([NetReplicationApply.cpp#L208-L227](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Network/NetReplicationApply.cpp#L208-L227)) uses the host's per-poll `m_now`. Two PawnStates in one poll give dt = 0, so the newer one is rejected. The client sends up to 2 per flush ([NetworkSystemDispatch.cpp#L399](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Network/NetworkSystemDispatch.cpp#L399)), and jitter bunching does the same.
- **Rewinds:** unreliable PawnStates have no staleness or sequence check, so a reordered older packet rewinds the host pawn. Snapshots do check tick order.
- **Fix:** put a client tick or sequence in PawnState, drop stale ones, and use tick delta for the speed check.

**P3-7. Malformed WAV can trigger a 4 GiB allocation**
- **Where:** [WavFile.cpp#L75-L79](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Audio/WavFile.cpp#L75-L79)
- **Cause:** `dataBytes.resize(size)` trusts the chunk size (up to 4 GiB). This is never checked against the file size.
- **Effect:** truncated files, or streaming-recorder WAVs with a 0xFFFFFFFF placeholder size, cause a huge allocation. In a no-exceptions build, an allocation failure terminates.
- **Fix:** clamp to the remaining file bytes, as FoliageFile and HeightMap already do.

**P3-8. HeightMap sampling with NaN reads out of bounds**
- **Where:**
  - [HeightMap.cpp#L304-L323](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Terrain/HeightMap.cpp#L304-L323)
  - [HeightMap.cpp#L407-L411](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Terrain/HeightMap.cpp#L407-L411)
  - [MathHelper.h#L47-L50](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Math/MathHelper.h#L47-L50)
- **Cause:**
  - `Clamp(NaN)` returns NaN, and `static_cast<int>(floorf(NaN))` becomes INT_MIN on x64. `sampleRaw` does not clamp, so `heightAtWorld(NaN, …)` reads far out of bounds.
  - The loader's `cellSize <= 0` check (L72) also lets a NaN `cellSize` or origin from a sidecar file through, which poisons every later query.
- **Fix:** reject non-finite values in `createWithMax`/load, and clamp the int indices (or NaN-guard) in `sampleBilinear`.

**P3-9. Right and middle mouse buttons are not captured.** [Window.cpp#L209-L237](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Core/Window.cpp#L209-L237). Only LMB calls `SetCapture`. Releasing RMB or MMB outside the window while it keeps focus (editor RMB-drag) leaves the button stuck down until the next click. XBUTTON1/2 and horizontal wheel are also unhandled.

---

## 2. Risks and design notes (not proven bugs)

**Rendering**
- **ACES writes linear values to a UNORM target with no OETF.**
  - [Tonemap.hlsl#L51-L56](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/content/shaders/Tonemap.hlsl#L51-L56) writes to R8G8B8A8_UNORM. Mode 0 applies `linearToSrgb`; ACES mode does not.
  - ACES is the default when lighting is active.
  - `Render/DESIGN-color-management.md` documents this as intentional. The Narkowicz fit expects the display transform afterwards, so the content has been tuned around a darker, more contrasty curve.
  - Treat this as a retune decision, not a bug.
- **Auto-exposure first reduce** point-samples one texel per 4x4 block (AutoExposure.hlsl PSReduce; AutoExposure.cpp L328), so exposure can flicker on small bright features.
- **Per-light volumetric fog is expensive:**
  - LocalLightVolume.hlsl L184-209 and L284-289 run the full FogIntegrate plus an 8-step scatter per light per pixel, with heightmap, shadow and 8 occluder-box tests.
  - Light-cone fog disappears against the sky because sky pixels are discarded.
- **Deferred fog** (Fog.hlsli) is a 16-step per-pixel march with heightmap and CSM. The sin-hash dither is static (no temporal variation, so banding is frozen in place), and in-scatter is clamped at 1.6.
- **CloudVolume** is a full-res ALU-noise march: 40-56 steps × 5 light steps × FBM. This is the largest GPU cost after fog.
- **Water evaluates the full `EvalSurface` twice**, in both VS and PS: 5 heightmap taps, Gerstner and noise.
- **IBL prefilter** uses no PDF-based source-mip selection, so bright HDR sources produce fireflies at mid roughness.
- **G-buffer normals** are 8-bit octahedral in RGBA8 (SceneBuffers.cpp L248), which bands on smooth or glossy surfaces.
- **InstanceCull** puts UAV barriers between every view, which serializes view culling (InstanceCull.cpp L400-470).
- **Movie NV12 chroma** is point-sampled, giving blocky chroma edges.
- **"Keep-alive" reads** that stop FXC stripping cbuffer members appear in about 6 shaders (instanced and skinned variants, FoliageCull). They are fragile, and explicit padding plus reflection checks would be better.

**Water CPU vs GPU**
- `waveHeight` ([WaterWaves.cpp#L140-L186](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Water/WaterWaves.cpp#L140-L186)) drops three things the GPU does ([Water.hlsl#L245-L258](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/content/shaders/Water.hlsl#L245-L258), [L382-L408](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/content/shaders/Water.hlsl#L382-L408)):
  - the per-wave ValueNoise phase jitter (up to 0.7 rad)
  - the horizontal Gerstner displacement
  - the flow-weight amplitude scale
- `WaterWorld::heightAtWorld`/`tryHeightAtWorld` currently have no app callers, so this is latent. It will matter as soon as buoyancy or underwater checks use them.

**Networking**
- **Connection security:**
  - One shared session token is used for all peers.
  - There is no connect challenge or cookie, so a spoofed ConnectRequest allocates a slot and triggers Accept plus `queueJoinSpawns` before the token is proven. That is a reflection and amplification risk on public hosts.
- **Tunables that hurt real play:**
  - `kNetRecvBudget = 32` per frame lets a flood starve legitimate traffic.
  - `kNetPeerTimeoutSec = 2` means a 2 s hitch (loading, debugger) disconnects.
  - `kNetMaxReplicated = 32` silently caps replication.
  - `kNetPawnMaxSpeed = 20` means teleports freeze until the check passes.
- **What looks good:** packet parsing is well bounds-checked, NaN is rejected on spawn, snapshot and pawn, and PawnState owner is verified.
- **DebugServer** binds `INADDR_ANY` ([TcpSocket.cpp#L309](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Network/TcpSocket.cpp#L309)). It is opt-in (CLI or dev toggle) and only streams logs, memory and perf, but anyone on the LAN can read the logs while it is on. Default to 127.0.0.1.

**Audio**
- **Spatial voices assume mono:** `apply3D` hard-codes `SrcChannelCount = 1` and `SetOutputMatrix(nullptr, 1, …)` ([AudioSystem.cpp#L606-L654](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Audio/AudioSystem.cpp#L606-L654)). A stereo clip played spatially would fail `SetOutputMatrix` silently and not be positioned. All 16 shipped WAVs are mono, so this is latent. Force mono, or downmix on load, for spatial use.
- **2D mono is louder than spatial:** 2D mono "centering" ([AudioSystem.cpp#L723-L730](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Audio/AudioSystem.cpp#L723-L730)) writes 1.0 to both L and R. That is about +3 dB versus X3DAudio's centred pan, so 2D cues sit louder than an equivalent 3D cue.
- **Per-voice XAudio2 calls:** `SetVolume`, `SetFilterParameters`, `SetOutputMatrix` and `SetFrequencyRatio` are called separately per voice per frame, all with COMMIT_NOW. Use an operation set and commit once per tick. The occlusion query (a raycast) runs per spatial voice per frame, with no budget.

**UI and Input**
- **ImGuiHost:**
  - If `init` fails after `ImGui::CreateContext()`/`new Impl`, the context and Impl leak, because `shutdown()` early-outs when `!m_ready`.
  - The DPI scale is applied only at init (fonts and style are not rebuilt on `WM_DPICHANGED`).
  - The fallback font path `C:\Windows\Fonts\segoeui.ttf` is hard-coded.
- **MainMenu** `rasterizeText` iterates raw bytes with no UTF-8 decoding ([MainMenu.cpp#L26-L76](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Ui/MainMenu.cpp#L26-L76)). The LoadingScreen copy of the same code does decode ([LoadingScreen.cpp#L182-L215](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Render/LoadingScreen.cpp#L182-L215)), and engine strings use UTF-8 "©"/"·". Any such label in the menu would render as "?©".
- **Mouse-look** uses `WM_MOUSEMOVE` deltas plus `SetCursorPos` recentring ([Window.cpp#L429-L440](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Core/Window.cpp#L429-L440)). This means OS pointer acceleration applies, there is no sub-pixel or high-DPI mouse precision, and fast flicks can lose deltas.
- **ActionMap** lower-cases and allocates a `std::string` on every query ([ActionMap.cpp#L123](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Input/ActionMap.cpp#L123)). This is cheap thanks to SSO, but it is avoidable with pre-hashed action IDs.

---

## 3. Duplication

- Fullscreen-triangle VS: copied in **17** shaders.
- `interleavedGradientNoise`: 3 copies (CloudVolume, Gtao, Tonemap).
- Hash and value-noise functions: 5 files. The CPU WaterWaves `hash01`/`valueNoise` adds a sixth.
- FogParams fill code: copied in 5 C++ pipelines (DeferredLighting, LocalLightVolume, Sky, Terrain, Water).
- `GpuLocalLight` / `GpuFogOccluder`: defined twice in HLSL (LocalLightVolume, Water).
- Mesh shader near-copies:

  | Pair | Identical |
  |---|---|
  | Basic vs Skinned G-buffer | 93% |
  | Basic vs Skinned forward | 91% |
  | Instanced pairs | 82-86% |
  | Basic G-buffer vs instanced | 86% |
  | FoliageGBuffer vs FoliageDepth | 47% |

  Move to a shared `.hlsli` with `#define SKINNED/INSTANCED` permutations.
- Bitmap-font text rasterizer: duplicated in MainMenu.cpp and LoadingScreen.cpp, and the copies have already diverged on UTF-8.
- Comparison-sampler descriptor: set up separately in ~9 pipelines, which is why P3-1 has to be fixed in 9 places. Centralize it.

## 4. Hard-coded values

- **Water:** colours and specPower in [WaterPipeline.cpp#L604-L626](https://github.com/DarkenedGit/DarkEngine6/blob/35cf523ec6b276e90bf769e1070de365f6c79251/Render/WaterPipeline.cpp#L604-L626); fallback light `{0.35,0.85,-0.35}` in Water.cpp; the `0.85` spec intensity and `0.18+0.55*ndotl` body light in Water.hlsl; shore-chop wavelengths, amplitudes and speeds (1.6/0.12/2.4, 0.9/0.06/3.1) duplicated in HLSL and C++.
- **Fog:** density 0.0015, in-scatter clamp 1.6, HG g = 0.32, 16 march steps.
- **Echo** pass constants.
- **Clouds:** 40-56 march steps and 5 light steps.
- **Audio:** occlusion gain floor 0.7, 900 Hz low-pass corner, 0.2 s smoothing.
- **Input:** stick deadzone 0.2; `kTriggerThreshold` is declared and marked unused.
- **Network:** recv budget 32, peer timeout 2 s, 32 replicated entities, max pawn speed 20, interp delay 2 ticks.
- **UI:** the Segoe UI path; MainMenu panel metrics (560/56/10 px) are not DPI-scaled.

## 5. Modern-quality gaps

1. **No shader↔C++ layout verification.** Use DXC/FXC reflection at build time (or in a unit test) to assert every cbuffer member offset. This would have caught P2-1. It is the single highest-leverage change.
2. **TAA:** unjittered velocity (P2-2); YCoCg variance clipping; Catmull-Rom history sampling; velocity dilation (closest-depth 3x3); disocclusion via depth or velocity rejection.
3. **SSR:** Hi-Z traced, with proper split-sum compositing (P2-3).
4. **Fog:** a froxel volume (compute, temporally reprojected) replacing per-pixel and per-light marches, with blue-noise or IGN temporal dither.
5. **Clouds:** half or quarter-res temporal reconstruction, plus baked 3D noise textures instead of ALU FBM.
6. **Exposure:** a compute luminance histogram with percentile clamping.
7. **Tonemapping:** AgX, or Hill ACES with an explicit OETF and HDR10 output path; resolve the ACES/OETF design question.
8. **Shadows:** cascade blending, a guard band or black border (P3-1), PCSS or contact shadows.
9. **G-buffer:** 16-bit octahedral normals (RG16_SNORM or R10G10B10A2).
10. **Networking:**
    - a client render clock with fractional interpolation (P2-4)
    - sequenced PawnState (P3-6)
    - a connect challenge/cookie and per-peer tokens
    - adaptive timeouts
11. **Input:** Raw Input (`WM_INPUT`) for mouse-look; `SetCapture` for all buttons; pass `WM_SYS*` through to DefWindowProc.
12. **Audio:** XAudio2 operation sets; a budgeted, async occlusion query; HRTF (e.g. Windows Sonic / ISAC) for spatialization.

## 6. Suggested fix order

1. **P2-1 Water cbuffer.** A one-line pad in two files, plus a reflection-based offset test (gap #1).
2. **P2-5 Alt+F4.** A trivial fix with high user visibility.
3. **P2-2 TAA velocity jitter.** One matrix change in SceneRenderer, then re-verify TAA, SSR and GTAO convergence.
4. **P2-3 SSR compositing.** A shader-only change.
5. **P2-4 + P3-6 network interpolation and PawnState sequencing.** These change the same code paths, so do them together.
6. **P3-1 shadow border sampler.** Centralize the sampler descriptor and fix all 9 pipelines at once.
7. **P3-7 / P3-8 robustness.** WAV size clamp; HeightMap NaN and finite checks.
8. **P3-2, P3-3, P3-5, P3-9.** Small shader and input fixes.
9. **Perf (§2).** Froxel fog, low-res clouds, then the water double eval and per-voice audio calls.
10. **Duplication cleanup (§3).** Shared `.hlsli` permutations, then the shared text rasterizer.

**Counts:** 5 P2, 9 P3, 0 P1, ~25 risks or design notes.
