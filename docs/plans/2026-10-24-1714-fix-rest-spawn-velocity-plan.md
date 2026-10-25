---
title: Rest Spawn Velocity - Plan
type: fix
date: 2026-10-24
topic: rest-spawn-velocity
artifact_contract: ce-unified-plan/v1
artifact_readiness: implementation-ready
product_contract_source: ce-brainstorm
execution: code
---

# Rest Spawn Velocity - Plan

## Goal Capsule

- **Objective:** Starting the game and stocking the rock and bottle no longer stops on a physics assert, and those pickups start at rest. A pickup spawned from a landing starts at rest. Debug still reaches gameplay.
- **Means:** Rest velocities passed into item-pickup spawn, and the linear and angular velocity stored on a new physics body, use the existing math zero constant (KTD1).
- **Product authority:** Confirmed 2026-10-24. Planning may not make default vector construction write zeros, may not clamp or reject speed at the physics door, and may not change the physics library.
- **Open blockers:** None.
- **Execution:** code
- **Stop:** Stop if an inventory toss, armor breakaway, or in-flight throw speed becomes rest, or if default vector construction starts writing its components.

---

## Product Contract

### Summary

Item-pickup spawn puts rest spin on every new pickup. Catalog stock and throwable landing pass rest as the linear velocity. A new physics body stores both velocities as that same zero constant. Default vector construction stays uninitialized.

### Problem Frame

Closing the start menu stocks a rock and a bottle as dynamic bodies, then the first physics step runs. In RelWithDebInfo that step asserts on the rock's angular velocity. The spawn builds those velocities with a default vector, and that constructor does not write its components, so the step receives leftover stack bits. A huge finite spin becomes a non-finite angular velocity in the solver, and the assert fires. Debug fills unused stack with a finite pattern the solver can clamp, so the same stock starts there.

### Key Decisions

- **Rest uses the zero constant.** (session-settled: user-directed — chosen over zeroing default construction and over clamping speed at the physics door: leftover bits must not reach the solver, and the empty constructor stays.) Governs R1, R2, R4.
- **Spawn values and stored body velocities both start at rest.** (session-settled: user-directed — chosen over fixing only the spawn arguments or only the stored fields: the assert reads the spawn arguments, and the stored fields are built the same empty way.) Governs R1, R2, R4.

### Actors

- A1. The player. Closes the start menu, throws, and drops items from the bag.
- A2. The Sandbox and Editor play hosts. Each stocks a missing rock or bottle and spawns a pickup when a throw lands. Both follow R1, R2, and R4.

### Requirements

**Pickup spawn**

- R1. Item-pickup spawn sets angular velocity to the existing math zero constant, `(0, 0, 0)`, on every new pickup body.
- R2. Catalog stock and throwable landing pass that zero constant as linear velocity.
- R3. Item-pickup spawn passes the caller's linear velocity through to the body.

**Stored velocity**

- R4. A new physics body stores linear velocity and angular velocity as the zero constant from R1 until a later write replaces them.

### Key Flows

- F1. First step after stock
  - **Trigger:** The start menu closes and a rock or bottle is missing from the scene.
  - **Actors:** A1, A2
  - **Steps:** The host stocks the missing pickup beside the player as a dynamic body. The first physics step runs.
  - **Outcome:** The step does not assert. Spin is rest. Linear velocity is rest when the step begins, then gravity acts.
  - **Covers R1, R2, R4.**
- F2. Landing becomes a pickup
  - **Trigger:** A thrown rock or bottle ends its flight.
  - **Actors:** A1, A2
  - **Steps:** The host spawns a pickup at the landing position.
  - **Outcome:** The pickup body starts at rest.
  - **Covers R1, R2.**

### Acceptance Examples

- AE1. Optimized stock
  - **Covers R1, R2, R4.**
  - **Given:** RelWithDebInfo, the start menu closes, and the scene has no rock and no bottle.
  - **When:** The host stocks both beside the player and the first physics step runs.
  - **Then:** The step does not assert. Both bodies begin the step with rest linear velocity and rest spin.
- AE2. Debug stock
  - **Covers R1, R2, R4.**
  - **Given:** Debug and the same missing rock and bottle.
  - **When:** The host stocks both and the first physics step runs.
  - **Then:** The step does not assert, gameplay continues, and both bodies begin the step at rest.
- AE3. Landing in either host
  - **Covers R1, R2.**
  - **Given:** A throw has landed in Sandbox or in the Editor.
  - **When:** The host spawns the landing pickup.
  - **Then:** Its linear velocity and spin are rest.
- AE4. Inventory drop
  - **Covers R1, R3.**
  - **Given:** The player drops an item from the bag.
  - **When:** The pickup spawns.
  - **Then:** Linear velocity is the toss the drop already passes. Spin is rest.

### Success Criteria

- RelWithDebInfo Sandbox stocks a rock and a bottle after the start menu and finishes the first physics step without a physics assert.
- Debug Sandbox does the same and still reaches gameplay.

### Scope Boundaries

- Default vector construction stays uninitialized. A missing position, muzzle placement, and walk speed when there is no motor stay on today's construction.
- In-flight throw speed stays the launch speed. Projectile velocity, jump-attack velocity, particle velocity, and audio velocity stay as they are.
- Armor breakaway keeps the linear and angular velocity that path already passes.
- Inventory drop keeps its toss, per R3.
- The physics door does not clamp or reject speed. The physics library is unchanged. The physics step count is unchanged.
- Throwable lure, counts, flight speed, and save rules are unchanged.

### Dependencies

- The math zero constant is `(0, 0, 0)` in `Math/Vector3f.cpp`.
- Item-pickup spawn applies spin itself. Linear velocity is the caller's argument, in `Character/ItemView.cpp`.
- Loading a game pushes each dynamic body's stored velocity into the sim after saved values are applied, from `Sandbox/SandboxSave.cpp` and `Editor/EditorSave.cpp`. A saved velocity stays the saved value. R4 is the value a body has before any velocity write.

### Sources

- `Math/Vector3f.h` and `Math/Vector3f.cpp` hold the empty default constructor and the zero constant.
- `Gameplay/ThrowableBelt.cpp` passes the stock linear velocity.
- `Character/ItemView.cpp` applies spin on both the throwable branch and the cube branch.
- `Sandbox/SandboxApp.cpp` and `Editor/EditorPlay.cpp` pass the landing linear velocity.
- `Sandbox/SandboxApp.cpp` passes the inventory-drop toss.
- `Physics/PhysicsBodyComponent.h` stores linear and angular velocity.
- `Physics/PhysicsWorld.cpp` copies both vectors into the physics body.
- `Character/ArmorView.cpp` passes the armor breakaway velocity.
- `Weapons/ThrowableFlight.cpp` assigns flight speed and does not set a physics-body velocity.

## Planning Contract

Product Contract unchanged.

### Key Technical Decisions

- KTD1. **Write rest with `Vector3f::ZERO`.** (session-settled: user-directed — chosen over zeroing default construction and over clamping speed at the physics door: leftover bits must not reach the solver, and the empty constructor stays.) `Vector3f(const Vect3f&)` copies `x`, `y`, and `z`, so an initializer `= Vector3f::ZERO` stores zeros. `Vector3f{}` calls `Vector3f::Vector3f()` and leaves the components uninitialized. Cites R1, R2, R4.

### Sequencing

U1 and U2 are both required. Either order works. U2 supplies the values the first physics step reads. U1 supplies the values a new `PhysicsBodyComponent` stores before any velocity write.

### Constraints

The save dependency is unchanged. U1 sets the value R4 names, before any velocity write. It does not reset a toss already stored during play.

## Implementation Units

### U1. Store rest on a new physics body

- **Goal:** A new `PhysicsBodyComponent` stores rest linear velocity and rest angular velocity.
- **Requirements:** R4, KTD1
- **Files:** `Physics/PhysicsBodyComponent.h`, `UnitTests/Physics/PhysicsBindTests.cpp`
- **Dependencies:** None. May land before or after U2.
- **Approach:** Replace the `{}` initializer on `linearVelocity` and `angularVelocity` with `= Math::Vector3f::ZERO` per KTD1. Leave `bakedScale`. The struct has no user constructor, so the default member initializer applies. `createBody` default-constructs the component and does not assign those fields. Add the check beside `PhysicsBind.CreateBodyWritesComponent`. Do not add a test that a default `Vector3f` is zero.
- **Test Scenarios:**
  - Cook a cube, call `createBody`, and read `PhysicsBodyComponent`. Linear velocity equals `Vector3f::ZERO`. Angular velocity equals `Vector3f::ZERO`.
- **Verification:** The new test passes under the Debug unit-test command in the Verification Contract.

### U2. Pass rest into pickup spawn

- **Goal:** Stock, landing, and pickup spin pass rest per R1 and R2.
- **Requirements:** R1, R2, R3, KTD1
- **Files:** `Gameplay/ThrowableBelt.cpp`, `Character/ItemView.cpp`, `Sandbox/SandboxApp.cpp`, `Editor/EditorPlay.cpp`
- **Dependencies:** None. May land before or after U1.
- **Approach:** Replace velocity temporaries only, using `Vector3f::ZERO` per KTD1.
  - `spawnCatalogPickup` passes it as linear velocity. `stockMissingThrowablePickups` and `respawnThrowablePickups` call that function.
  - `spawnItemPickup` passes it as angular velocity on the throwable branch and the cube branch. The linear argument stays `velocity`, per R3.
  - The landing loop in `Sandbox/SandboxApp.cpp` and the landing loop in `Editor/EditorPlay.cpp` pass it as linear velocity.
  - Leave `SandboxApp::dropInventoryItem` on `forward * 2.0f + Vector3f{0.0f, 2.0f, 0.0f}`.
  - Leave the armor breakaway linear push and angular `Vector3f{6.0f, 2.0f, 4.0f}` in `Character/ArmorView.cpp`.
  - Leave pose, muzzle, motor-null walk speed, and hunter-attack position fallbacks that use `Vector3f{}`.
  - Leave `Weapons/ThrowableFlight.cpp`. It assigns flight speed and does not call `setBodyVelocity`.
- **Test Scenarios:**
  - Stock. The linear argument from `spawnCatalogPickup` is `Vector3f::ZERO`. Both pickup branches pass `Vector3f::ZERO` as angular velocity.
  - Landing. Sandbox and Editor pass `Vector3f::ZERO` as linear velocity.
  - Inventory drop. Linear velocity stays `forward * 2.0f + Vector3f{0.0f, 2.0f, 0.0f}`. Spin is rest because the angular edit covers both branches.
  - Armor and flight. Armor angular velocity stays `Vector3f{6.0f, 2.0f, 4.0f}`. Flight speed stays on the flight record.
- **Verification:** Read the call sites. There is no `spawnItemPickup` unit test. AE1 and AE2 are the play checks in the Verification Contract.

## Verification Contract

- Build and run Debug unit tests with `cmake --build build --config Debug --target UnitTests`, then `build\bin\Debug\UnitTests.exe`. U1's new test is the automated proof of R4.
- `EchoChirpFile.ContentFileLoadsTheChirp` and `FoliageSpawn.TreesAndGrassSkipWaterVolumes` are pre-existing failures. Do not treat them as failures of this plan, and do not fix them here.
- `release:validate` does not apply.
- RelWithDebInfo Sandbox play is the AE1 check. Close the start menu with no rock and no bottle in the scene. The first physics step does not assert, and both bodies begin that step at rest.
- Debug Sandbox play is the AE2 check. The same stock reaches gameplay.
- AE3 and AE4 are the U2 call-site read. Landing linear velocity and pickup spin are rest. The inventory toss stays the toss that drop already passes.

## Definition of Done

- U1 is done when `linearVelocity` and `angularVelocity` initialize from `Vector3f::ZERO` and the new physics test passes.
- U2 is done when stock, both pickup branches, and both landing loops pass `Vector3f::ZERO`, and the inventory toss, armor breakaway, and flight speed are unchanged.
- AE1 and AE2 hold in play.
- The diff adds no `try`, `catch`, `throw`, or `std::exception`.
- The diff does not edit `third_party/box3d`, `Render/InstanceCull.h`, `Render/InstanceCull.cpp`, or `Render/FoliagePipeline.cpp`.
- Abandoned-attempt code is removed. That includes a clamp or reject at the physics door, an edit to `Vector3f::Vector3f()`, and a game harness for stock.
