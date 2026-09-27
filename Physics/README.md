# Physics

Box3D wrapper for DarkEngine6. Collision query code stays in `Collision/`.

This folder is globbed into the `DarkEngine` umbrella (`DE_ENGINE_REST_FOLDERS`).
The C API lives under `third_party/box3d` (tag v0.1.0). Engine sources that
include `box3d/box3d.h` belong here only — not in public headers or tests.

Public types: `PhysicsWorld`, `PhysicsIds`, `PhysicsMath` (`rotate` /
`roundTripQuat`, scale bake), `PhysicsSurfaceCatalog`, CPU `CollisionShape`.
Surfaces load from `content/physics/surfaces.json`. Cook (`CollisionCook`)
builds POD shapes from cube/sphere/pawn conventions, Model `_col*` parts, or
`*.collision.json` sidecars (`json::parse(..., false)`). `Model::valid()` is
visual or collision; GPU upload still uses `opaque()` / `translucent()` only.
`PhysicsWorld::createBody` binds a `CollisionShape` to an ECS entity
(`PhysicsBodyComponent`) and `step(dt, World&)` destroys bodies whose
`!World::alive`. `pushPoses` copies transforms into bound bodies; `writeDynamicPoses`
copies dynamic solver poses back. `debugDraw(LineMeshData&)` tessellates Box3D shapes into a line list (64k
cap) via `b3DebugDraw` / `createDebugShape`. Hosts call it when their overlay
is on (`debugOverlay()` / `setDebugOverlay`). `PhysicsWorldDesc.enabled`
defaults to false so hosts do not step until they opt in.

Authoring lives on `PhysicsComponent` (body mode, shape, density, friction,
restitution, damping, gravity scale, sensor, fixed rotation, surface name).
`*.physics.json` beside a model (`human.gltf` → `human.physics.json`, under the
authoring content root) loads through `loadPhysicsSettingsForModel`.
`bindPhysicsEntity` cooks the shape and calls `createBody`. The editor inspector
edits the component and writes that sidecar. Play steps the world; edit mode
holds bodies on their placed transforms. A flat static ground sits at Y = 0.

See `Collision/DESIGN-box3d.md`.
