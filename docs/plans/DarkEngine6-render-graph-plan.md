# DarkEngine6: moving to a render graph (v2)

> **In-repo copy.** This is the v2 plan as approved on 2026-11-20, plus decision D14. Line links still point at `7daebbb`, the tree it was written against. Progress: Phase 0 (baselines + docs) — see [docs/perf/README.md](../perf/README.md). Phase 1 (forward path deleted, D2) — branch `render-graph/phase-1`; the Windows gates (image diff vs the Phase 0 baselines, PSO count) are run on Travis's machine. Phase 2 (Agility SDK 1.620.2, DXC 1.9.2609.5 at SM 6.0, startup gate, Release shader bake) — branch `render-graph/phase-2`. The bake landed before the SM 6.6 bump (the manifest has no profiles, so the bump is the one-line `kShaderModelDxc` change in `Render/ShaderKey.h`). Still open on Windows: the DXC-vs-FXC A/B, then SM 6.6 and FXC removal, and pinning the NVIDIA minimum (placeholder 572.16). Phase 3 (CPU graph core with queues: `Render/RenderGraph*.{h,cpp}`, data only, no D3D12 calls, not wired into the frame) — branch `render-graph/phase-3`; native unit tests in `UnitTests/Render/RenderGraph*Tests.cpp`, including a randomized plan checker. Phase 3 deviations: the `mip` argument is recorded but resources are tracked whole; the two §3.13 exceptions (bloom mips, local-shadow array) need per-subresource tracking before those passes migrate; alias sizes are format estimates with offset 0 until D3D12MA (Phases 5/9). Phase 4 (the graph drives the frame: `Render/FrameComposer`, `Render/RgExecutor`, `Render/FrameFeatures`) — branch `render-graph/phase-4`. Every pass is opaque (OwnBarriers) and keeps its own barriers; imports are tags only (Undefined to Undefined, the back buffer Present to Present), so the graph plans no barriers. Phase 4 deviations: `PassId::DebugOverlays` was added for the untracked line overlays (Sandbox: after Translucent, Editor: after the gizmos); splats are part of Particles; the HDR clear is in Decals (which declares an Hdr write) and `bindHdr(true)` is at the end of LocalVolumes; ImGui and the movie stay after the graph until Phase 7; `Renderer::endFrame` still does the back buffer RENDER_TARGET to PRESENT until Phase 6; the executor runs on the main list only (a cross-queue split, signal or wait logs an error until Phase 10). Phase 5 (the graph owns persistent targets, allocated with D3D12MA 3.3.0: `Render/GpuAllocator`, `Render/RenderTargetPool`, `Render/D3D12TargetBackend`, `Render/GraphTargets`, `Render/TemporalHistory`, `Render/FrameView.h`) — branch `render-graph/phase-5`. Depth, `SceneBuffers` and the GTAO, SSR, bloom, auto-exposure, underwater, camouflage and echo targets are registered once (output-relative, PIX names `DE.*`) and recreated by `Renderer::resize` after its one `waitForGpu`; the 8 lazy resize waits are gone. TAA, GTAO and SSR use `g.history()` (TAA writes its history as a second render target, so the Post→History copy is gone); `FrameComposer` takes a `FrameView` (`holdTemporal`) and `SceneRenderer::requestHistories` applies `discardTemporalHistory` through `invalidateHistory()`. Phase 5 deviations: the back buffer stays swapchain-owned (imported); the D3D12MA source is downloaded at configure like the Agility/DXC pins, not checked in; pool views are CPU-only and the shader-visible tables stay per pipeline until the Phase 6 descriptor ring; the underwater, camouflage and echo copies register on first use; a held frame (and the one after it) also resets GTAO and SSR history, not only TAA. Phase 6 part 1 (enhanced barriers everywhere + descriptor ring: `Render/EnhancedBarrierEmitter`, `Render/ResourceStateTracker`, `Render/DescriptorRing`, `Render/DescriptorRingHeap`) — branch `render-graph/phase-6`. Every remaining `ResourceBarrier` call (34 sites in 18 files at `bb71960`; 37 in the plan baseline) now records `ID3D12GraphicsCommandList7::Barrier` through one helper that translates legacy states with the spec's equivalence tables, so legacy and enhanced barriers never meet on a subresource (the spec only allows that through a COMMON round trip). Read-only depth that is also sampled (legacy `DEPTH_READ` + SRV bits, and the graph's `Use::DepthRead`) uses `DIRECT_QUEUE_GENERIC_READ` (or `…_COMPUTE_QUEUE_ACCESSIBLE` when `NON_PIXEL_SHADER_RESOURCE` is set), because `DEPTH_STENCIL_READ` allows depth-stencil-read access only. A record-order `ResourceStateTracker` owned by `Renderer` (main thread only) is the single source of truth for the back buffers and every graph target: owners' `transition*` helpers and the executor read before-states from it, and `GraphTarget::state`, `SceneBuffers::m_state` and `Renderer::m_depthState` are gone. The compiler plans a `tracked` barrier at each opaque→declared boundary (its before-state comes from the tracker at execute time); `MixedOpaqueUse` is now only an error for a declared AsyncCompute user. Textures created in PIXEL/NON_PIXEL_SHADER_RESOURCE, COPY_DEST or COPY_SOURCE (internal LEGACY_* layouts) are created with an explicit layout (`CreateCommittedResource3` / D3D12MA `CreateResource3`). Part 1 deviations: no feature is converted to declared uses yet (all passes stay OwnBarriers; conversions follow in post → GTAO/SSR → … order as separate PRs); imports carry the tracker's layout as `initial` and no final layout (`Renderer::endFrame` keeps RT→PRESENT because ImGui and the movie draw after the graph until Phase 7); the ring heap is created and fence-retired but no pipeline binds it yet and `PassContext::srvTable` waits for the first converted pass; the shadow and local-shadow arrays stay untracked (per-slice, recorded on the shadow worker). Phase 6 part 2 (the first converted feature: Post; `Render/RgViews`, `FeatureFrame::declare` / `FeatureBuilder`, `PassContext::rtv/dsv/srvCpu/srvTable`) — branch `render-graph/phase-6b`. Imports and history slots carry their CPU views (`RenderGraph::setViews`); a pass gets a view only for a use it declared, and `srvTable` copies the declared SRVs into the descriptor ring (`RgExecutor::descriptorTable`), which the executor binds before every converted pass. A list entry can now be `declare`d: its callback adds several declared passes at build time (one PassId for the order check). Post is auto exposure (reduce, one pass per level, the readback copy as a SideEffect pass), bloom (extract, 5 downsamples, 5 additive upsamples, additive composite), TAA (history prev/curr as graph handles), motion blur and tonemap, each its own declared pass, so the graph plans every barrier between them; the bloom mips and exposure levels are imported per frame with the tracker's layout (`importTracked`). TAA, motion blur, tonemap, bloom and auto exposure lose their shader-visible heaps and `transition*` helpers; `Renderer::bindPostHdr` and `bindTaaTargets` are gone. Part 2 deviations: the echo capture/composite stays one opaque pass at the head of the Post entry (game content, Phase 8); a TAA reset frame binds the other history slot's SRV from outside the graph (the shader ignores it); the hosts call `endCameraFrame` after `execute()`; descriptor-ring init failure is now fatal. Phase 6 part 3 (GTAO, SSR and SsrCapture converted: `SceneRenderer::addGtaoPasses` / `addSsrPasses` / `addSsrCapturePasses`) — branch `render-graph/phase-6c`. GTAO is produce → upsample (writes the history as a second render target) → compose; SSR is trace → upsample (history MRT) → the optional confidence debug; SsrCapture is one pass from HDR into the scene-color slot `SsrPipeline::beginCapture` picks at setup, after an opaque `SsrCapture.Unbind` so HDR is not bound as a render target when the graph moves it to SRV. Each is a declared pass binding a ring table, on targets imported per frame with `importTracked`; GTAO and SSR lose their shader-visible heaps and in-draw `transition*` calls (the pipelines now upload their constants and record one draw step). Settings are captured at build; the camera and the previous view-proj are read when the passes execute, because a declare callback runs before any pass executes (part 2 bug: Post read the host's view-proj at build time, before the GBuffer pass filled it; fixed here through `Render/PostMatrices.h`, which only keeps a pointer to the host's matrix until a pass exec fills from it; covered by FrameFeatures tests). Part 3 deviations: Lighting and Water are still opaque, and the graph plans no barrier at a declared→opaque boundary, so each converted entry ends with a small opaque hand-off pass (OwnBarriers | SideEffect, declaring a read of its output) that moves the output to PIXEL_SHADER_RESOURCE through the tracker and restores the bindings the old code left (lighting SRV slot, `bindHdr(false)` / `bindHdrDepthRead`); they go when those passes convert. A skipped GTAO/SSR frame is one opaque pass running the old skip code, with the history marked unwritten at setup; a reset frame binds the other history slot's SRV from outside the graph, as TAA does; Sandbox's water local-light gather moved into an opaque CPU-only pass in the SsrCapture entry. Phase 6 part 4 (Lighting and LocalVolumes converted: `SceneRenderer::addLightingPasses` / `addLocalVolumesPasses`, `Render/LightingInputs.h`, `Render/LightingViews.h`) — branch `render-graph/phase-6d`. Each is one declared pass reading albedo, attrib, depth and AO as pixel SRVs and HDR as a read-write render target (Lighting discards sky pixels, LocalVolumes blends), binding a descriptor-ring table instead of the shader-visible lighting heap, which is gone with `packLightingHeap`, `setLightingAoSrv` / `setLightingSsrSrv` and the `*TableGpu` getters. GTAO and SSR publish their outputs on the graph blackboard (`GtaoOutput`, `SsrOutput`, §3.3); `lightingReads()` picks the compose (else the authored MRT3) and SSR Full (else the dummy), so the graph plans those barriers and the `Gtao.Handoff` / `Ssr.Handoff` passes and `GtaoPipeline::transitionCompose` are gone. Views the graph does not track (cascade and local-shadow arrays, terrain height, IBL, the lighting sRGB/raw albedo view) are CPU handles from `Renderer::lightingViews()`; an unset one is a null SRV of the right dimension from a small non-shader-visible heap in `SceneBuffers` (the old heap left those slots uninitialized). The lighting table is t0..t9 (`kLightingCount` 10) and the local-volume table has its own 7-entry order (`LocalLightVolumePipeline::kTable*`). Part 4 deviations: LocalVolumes converted with Lighting (it shared the heap); a skipped SSR frame is a no-op opaque pass (it keeps the list entry); the LocalVolumes entry ends with an opaque `LocalVolumes.BindHdr` (`bindHdr(true)`) for the still-opaque Sky; `SsrCapture.Unbind` / `SsrCapture.Handoff` stay for the opaque Water; Sandbox water's height fallback is the 1×1 dry height dummy instead of the lighting heap's height slot. Next: G-buffer, water/clouds/particles, shadows and the debug tiles, one PR each.

**Repository:** [DarkenedGit/DarkEngine6](https://github.com/DarkenedGit/DarkEngine6) · **SHA:** `7daebbb` (`7daebbb8e11c858007977c17cfedc75d66ab007a`, main, pushed Nov 19, 10:21 PM PST). Main hadn't moved as of Nov 20, 8:30 PM PST.
**Tree used:** `/workspace/de6-7daebbb`, which is main at `7daebbb`. PR #39 (`d805700`) only changes `Render/WalkabilityDebug.*`, `Render/WaterPipeline.h` and a new `Render/DebugColorMesh.h` under `Render/`. Nothing here depends on it.
**Method:** static reading of the code only. Nothing was built, run, captured in PIX or profiled. **[C]** marks a fact confirmed in the code, with a link. **[I]** marks an inference or estimate that needs a build, a capture or a measurement.
**Supersedes:** `DarkEngine6-render-graph-plan-7daebbb.md` (v1, kept unchanged). This version applies Travis's decisions of Nov 20, 2026 (D1–D8, then D9–D13 from his answers to the follow-up questions, then D14).
**Constraints kept:** 9 libraries (max 10; this adds **none**), no hot-reload work, static analysis only. Sandbox ImGui docking stays as it is and isn't flagged.

Link base: `https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/`.

---

## Decisions (2026-11-20)

| # | Decision | What it changes in this plan |
|---|---|---|
| D1 | **Reverse K1: the frame graph is in.** | [DESIGN-deferred-renderer.md](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/DESIGN-deferred-renderer.md#L131) K1 / §302 and the "out of scope" line in [2026-11-10-review-followup.md L124](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/docs/plans/2026-11-10-review-followup.md#L124) get updated in Phase 0 so the docs agree with the plan. |
| D2 | **Remove the forward rendering path entirely.** It doesn't stay as a fallback. The only forward rendering left is the **forward translucent pass after deferred lighting** (plus water, particles, clouds and overlays, which already blend into HDR after lighting). | New **Phase 1** deletes it before any graph work (§3.14 counts what goes). The graph then wraps 2 feature lists instead of 4. |
| D3 | **The Editor 3D view stays full window with ImGui on top.** | The offscreen Editor viewport option is dropped. Multiple views per frame stay possible for engine needs (shadow and IBL bakes), not for an Editor viewport. |
| D4 | **Adopt the Agility SDK, DXC and D3D12MA.** | New **Phase 2** (Agility SDK + DXC + device caps). D3D12MA arrives with the targets phase (Phase 5) and backs transient aliasing (Phase 9). See §3.15. |
| D5 | **Minimum GPU is the RTX 2080** (Turing, SM 6.6, 8 GB). | Raise the device minimum to FL 12_1 with a caps struct. Resource Heap Tier 2 means one heap class for transients. Enhanced barriers are required (no legacy fallback, see D13). SM 6.6 `ResourceDescriptorHeap` bindless is a possible later step. Transient heaps are sized for 8 GB (§3.8). |
| D6 | **Async compute is a planned phase, not optional.** | Queues and fences are in the graph model from Phase 3 (the CPU-only compiler). **Phase 10** moves the first passes: GPU particle sim, GTAO (ported to compute), auto-exposure (ported to compute), then the shadow-view GPU cull (§3.11). |
| D7 | **Camouflage, Echo and the vision modes are game content.** They live in `Game.Core` and register through the feature hooks; the engine never names them. | New **Phase 8** moves them out. The engine gets a generic **frame-setup hook** (§3.4.2, refined by D11) that replaces the thermal/ultraviolet code built into ~30 engine C++ files and 13 shaders. |
| D8 | **No quality presets for now; keep the debug toggles.** | `DebugRenderState` stays as the switchboard, and the graph culls the passes a toggle turns off. Preset work is removed from the plan. |
| D9 | **Windows 11 is the OS target.** Windows 10 support isn't needed. | One Agility SDK deployment model, no Windows 10 1909/2004 runtime checks or test matrix. Startup checks the OS build (≥ 22000) and fails with a clear message (§3.15). |
| D10 | **Release ships precompiled shaders; Debug and the Editor keep runtime DXC with a cache.** No DXC DLL ships in Release. | A build-time shader bake (CMake target `de_shaders` running `dxc.exe` from a permutation manifest) is added to **Phase 2**. Release loads signed DXIL from `bin/shaders/` and has no `dxcompiler.dll`/`dxil.dll` (§3.15.1). |
| D11 | **Vision modes are full-screen passes.** Some turn lighting off completely and need to configure parts of earlier passes. | A generic **frame-setup hook** runs before the graph compiles. Game.Core features disable or parameterise earlier engine passes (lighting, shadows, GTAO, SSR…) through generic flags and params the engine never ties to mode names; the graph culls the disabled passes and their targets (§3.4.2). Surface shaders lose their vision variants: all **71** vision-mode refs leave the engine shaders (the other 8 of the "79" were thermal *erosion*, unrelated). **Phase 8 shrinks from M–L (2–3 wk) to M (1.5–2 wk).** |
| D12 | **Shadows may move after the G-buffer** so GTAO can overlap them on async compute. | No G-buffer shader reads a shadow map **[C]** (`rg -il shadow content/shaders/*GBuffer*.hlsl` only hits a comment in `TerrainGBuffer.hlsl` L1 saying "No lighting/fog/shadow"). The stage order puts `Shadows` after `GBuffer` from **Phase 7** (graphics queue, image-identical), so Phase 10 only has to move GTAO to compute (§3.11). |
| D13 | **Enhanced barriers only. No legacy barrier fallback.** | A startup caps + minimum-driver check fails with a clear message. The legacy emitter, `rg.forceLegacyBarriers`, the "both emitters" gates and the dual-path risk are removed. **Phase 6 shrinks from L (3–4 wk) to M–L (2–3 wk)** (§3.7). |
| D14 | **GTAO moves to compute in Phase 10.** | It stays a pixel shader on the graphics queue through Phase 9. Phase 10 ports it to `cs_6_6` and puts it on the async queue after the G-buffer, overlapping the shadows that run after the G-buffer from Phase 7 (D6, D12, §3.11 row 2). The Phase 0 FXC baselines are the image and timing reference for the port. |

All nine v1 open questions are answered by D1–D8, and the five follow-up questions by D9–D13. There are no open questions left (§6).

---

## 0. Summary

**Today [C].** `Render/PassTable.h` has the bones of a pass graph (20 `PassId`s, 4 tables with read and write lists), but it only checks the order *after* the frame has been recorded. The real frame is a hand-written ~1,170-line `SandboxFrame::render()` and a ~975-line `EditorApp::renderScene3D()` that drive an implicit bind/transition state machine. On top of that:
- every screen-sized feature owns committed textures and resizes them lazily with a mid-frame `waitForGpu()`;
- everything runs on one direct queue (plus a copy queue for buffer uploads);
- a second, forward 3D path (`-forward`, `ScenePath::SwapChainForward`) runs through every host and many pipelines;
- the game's vision modes, Camouflage and Echo are built into engine pipelines and shaders.

**Plan.** Build a small, DE6-shaped graph inside `Dark.Render`: `rhi` gets the state tracking, the enhanced-barrier emitter, D3D12MA-backed transient heaps and the descriptor ring; `render` gets the graph, the queue/fence scheduler and the feature registry. The phases, in order:
1. Delete the forward path.
2. Move the platform to the Agility SDK, DXC and SM 6.6 on FL 12_1 (Windows 11, enhanced-barrier caps check, Release shader bake).
3. Build a CPU-only graph compiler with a queue model.
4. Wrap the existing passes so the graph drives the order.
5. Let the graph own the targets through D3D12MA.
6. Make barriers automatic (enhanced barriers only).
7. Register engine features from their modules (shadows move after the G-buffer here).
8. Move game content (Camouflage, Echo, vision modes) to `Game.Core`; vision modes become full-screen passes that use the frame-setup hook.
9. Alias transient textures.
10. Add async compute.
11. Record on several threads.

Every phase keeps Sandbox, Editor (2D and 3D), Sandbox2D and VisualDebugger building and running.

---

## 1. Audit of the current renderer

### 1.1 Frame flow and pass order (Sandbox, HybridDeferred)

All in [SandboxFrame::render() L314-L1486](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L314-L1486), recorded on `Renderer::commandList()`:

| # | Step | Where | Notes |
|---|---|---|---|
| 0 | `Renderer::beginFrame`: reset allocator and list, PRESENT→RT on the back buffer, clear the back buffer and depth, start the profile frame | [Renderer.cpp L615-L680](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Renderer.cpp#L615-L680) | **[C]** The back buffer and depth are cleared even when the scene renders into HDR |
| 1 | Start-menu short-circuit (ImGui only) | [L325-L339](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L325-L339) | |
| 2 | CPU: local-shadow select, scene bounds, `ShadowSystem::update`, build the instanced `DrawList`, GPU-cull views | [L365-L546](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L365-L546) | `beginCameraFrame` is called at [L538](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L538) **and** [L618](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L618) **[C]**. It's harmless today (same jitter index) but fragile |
| 3 | **Shadows** (directional): `recordDirectionalShadows`, optionally on the worker's separate list | [L548-L579](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L548-L579), [Play/ShadowRecord.cpp L698-L735](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Play/ShadowRecord.cpp#L698-L735) | **[C]** The render pass lives in **Play/** (a Gameplay module) |
| 4 | **LocalShadows** (cube and spot faces) | [L581-L611](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L581-L611) | |
| 5 | **GBuffer**: `bindGBuffer`+`clearGBuffer`, terrain, grass, foliage, meshes, models, projectiles | [L620-L880](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L620-L880) | 13 vision-mode setters are pushed into individual pipelines ([L627-L640](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L627-L640)) |
| 6 | **resolveDeferred** = Decals → HDR clear → GTAO → SSR → Lighting → LocalVolumes | [L893-L910](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L893-L910), [SceneRenderer.cpp L485-L504](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.cpp#L485-L504) | **[C]** All five `markPass` calls run *unconditionally* before the call, so the order check can't see a skipped pass |
| 7 | **Sky** | [L912-L914](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L912-L914) | Forward path draws sky first instead ([L664-L668](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L664-L668)) |
| 8 | **SsrCapture** (HDR → half-res scene-color copy for water) | [L1098-L1102](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L1098-L1102) | |
| 9 | **Water** (sheet + ribbons, cap `kMaxWaterDrawsPerFrame`) | [L1112-L1176](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L1112-L1176) | Gets the terrain height heap and the SSR scene colour as raw descriptor handles |
| 10 | **Clouds** | [L1178-L1189](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L1178-L1189) | |
| 11 | **Camouflage** (copies the scene, then draws the possessed body) | [L1191-L1211](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L1191-L1211) | |
| 12 | **Translucent** (CPU sort, forward draw) | [L1213-L1269](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L1213-L1269) | |
| 13 | *Untracked:* path debug, **walk overlay**, skeleton overlay, decal volumes | [L1271-L1278](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L1271-L1278) | **[C]** No `PassId` for these |
| 14 | **Particles**: GPU sim+draw, CPU emitters, blood, impacts, leaves | [L1280-L1301](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L1280-L1301) | GPU sim dispatches on the direct list |
| 15 | **Underwater** (HDR copy, fog, then water from below) | [L1306-L1340](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L1306-L1340) | |
| 16 | **Post**: auto-exposure readback/adapt, echo capture/composite, `applyPost` = meter → bloom → TAA → motion blur → tonemap to the swap chain | [L1342-L1405](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L1342-L1405), [SceneRenderer.cpp L717-L790](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.cpp#L717-L790) | |
| 17 | **Overlay**: debug tiles (shadow maps, G-buffer, velocity, AO, SSR) | [L1422-L1428](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L1422-L1428), [L1488+](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L1488) | Transitions the G-buffer, depth, AO and SSR to SRV by hand |
| 18 | Movie on top, then ImGui HUD/menus on the swap chain | [L1429-L1480](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L1429-L1480) | |
| 19 | `passSequenceOk`, `frameJoinRender`, `endFrame`: RT→PRESENT, resolve timestamps, `ExecuteCommandLists([shadow, main])` | [L1481-L1485](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L1481-L1485), [Renderer.cpp L771-L825](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Renderer.cpp#L771-L825) | |

**Editor** ([EditorRender.cpp L49-L79](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Editor/EditorRender.cpp#L49-L79) → [EditorRender3D.cpp L211-L1185](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Editor/EditorRender3D.cpp#L211-L1185)) repeats the same skeleton: shadows, GBuffer, `resolveDeferred`, Sky, SsrCapture, placed water, Clouds, then **EditorGrid/EditorGizmos** into HDR ([L552-L580](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Editor/EditorRender3D.cpp#L552-L580), [L955](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Editor/EditorRender3D.cpp#L955)), the walk overlay and trail overlay ([L962-L963](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Editor/EditorRender3D.cpp#L962-L963)), Translucent, Particles, Post, Overlay, then ImGui. There's no camouflage, echo or vision modes, and no auto exposure (`createAutoExposure=false`, [SceneRenderer.h L58-L59](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.h#L58-L59)). **[C]** The Editor's 3D view is the full back buffer with ImGui drawn on top (`RSSetViewports(renderer().viewport())`, [L1174](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Editor/EditorRender3D.cpp#L1174)). There's no offscreen viewport texture. **[C]** Picking is CPU ray casts (`pickObject(const Ray3f&)`, [EditorApp.h L142](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Editor/EditorApp.h#L142); terrain `groundHitFromRay`, [EditorAppInit.cpp L394-L410](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Editor/EditorAppInit.cpp#L394-L410)). No GPU ID buffer is involved.

**Pass tables** ([PassTable.h L8-L68](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/PassTable.h#L8-L68), [PassTable.cpp L82-L173](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/PassTable.cpp#L82-L173)): `sandboxDeferred` (18 nodes), `sandboxForward` (11), `editorDeferred` (19), `editorForward` (9). **[C]** The read and write lists are partly empty on purpose ("Lists stay empty so passSequenceOk is unchanged", [L99](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/PassTable.cpp#L99)). Failure logs once and returns false, and both hosts ignore the result with `(void)`. The [app-to-engine recommendations](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/docs/plans/darkengine6-app-to-engine-recommendations.md#L19) already point out that the table "doesn't drive the frame".

### 1.2 Render targets, depth and history: creation, sizing, resizing

- **Swap chain + depth** are owned by `Renderer`: 2 flip-discard RGBA8 buffers (`kFrameCount = 2`, [Renderer.h L50](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Renderer.h#L50)), one D32 reverse-Z depth with its own SRV heap ([Renderer.cpp L479-L544](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Renderer.cpp#L479-L544)). `resize()` does `waitForGpu`, `ResizeBuffers` and recreates depth and `SceneBuffers` ([L547-L604](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Renderer.cpp#L547-L604)).
- **SceneBuffers** ([SceneBuffers.h L13-L143](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneBuffers.h#L13-L143), [SceneBuffers.cpp L220-L252](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneBuffers.cpp#L220-L252)) holds 7 committed textures: HDR RGBA16F, Albedo RGBA8 (sRGB view), Attrib RGBA8, Velocity RG16F, Post RGBA16F, **TaaHistory** RGBA16F and AO R8. Each has its own SRV heap (8 small heaps) and one shader-visible 12-slot "lighting" table.
- **Owned by each pipeline and resized lazily**, each with its own `waitForGpu()` **[C]**:
  - GTAO: AoHalf, AoFull, AoHistory0/1, AoCompose ([GtaoPipeline.cpp L402-L406](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/GtaoPipeline.cpp#L402-L406)), resized in `ensureGtaoSize` ([SceneRenderer.cpp L346-L362](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.cpp#L346-L362))
  - SSR: Half, Full, History0/1, SceneColor0/1, Debug ([SsrPipeline.cpp L535-L541](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SsrPipeline.cpp#L535-L541)), resized in `ensureSsrSize` ([L364-L379](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.cpp#L364-L379))
  - Bloom mip chain starting at half resolution ([BloomPipeline.cpp L240-L256](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/BloomPipeline.cpp#L240-L256)), resized in `applyPost` ([L728-L735](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.cpp#L728-L735))
  - Auto-exposure reduction chain plus readback ([AutoExposure.cpp L336-L390](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/AutoExposure.cpp#L336-L390)), resized in `syncAutoExposureSize` ([L689-L715](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.cpp#L689-L715))
  - Underwater HDR copy ([UnderwaterPipeline.cpp L140-L175](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/UnderwaterPipeline.cpp#L140-L175)), Camouflage scene copy ([CamouflagePipeline.cpp L278-L290](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/CamouflagePipeline.cpp#L278-L290)) and Echo snapshot ([EchoPipeline.cpp L190-L285](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/EchoPipeline.cpp#L190-L285)) are all resized *inside the draw call* with `waitForGpu()`.
- **History buffers:** TAA history is a **copy** from Post (`copyPostToHistory`, [Renderer.cpp L1645](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Renderer.cpp#L1645), called from [TaaPipeline.cpp L177](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/TaaPipeline.cpp#L177)). GTAO and SSR keep index-swapped pairs. Validity is tracked in four places with separate flags (`m_taaHistoryValid`, `m_gtaoNeedReset`, `m_ssrNeedReset`, `m_havePrevViewProj`, [SceneRenderer.h L264-L281](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.h#L264-L281)).
- **Allocation:** **[C]** 58 `CreateCommittedResource` calls across Render, Terrain, Particles and Sandbox. There are **0** `CreatePlacedResource`/`CreateHeap` calls, so there's no aliasing.
- **Memory [I]:** at 1920×1080 the screen-sized set above comes to roughly **95–100 bytes/pixel, about 200 MB** (about 800 MB at 4K). About two thirds of it (HDR, G-buffer, velocity, post, AO, GTAO and SSR intermediates, bloom, the underwater and camouflage copies) only lives within one frame, so it could be aliased.

### 1.3 Barriers and state tracking

- **[C]** **37** `ResourceBarrier(` call sites (34 in Render, 2 in Particles, 1 in Terrain) across 20 files. Biggest: `Renderer.cpp` (6), `InstanceCull.cpp` (4), `TerrainErosionPipeline.cpp`, `LocalShadowSystem.cpp` (3 each).
- **[C]** **30** `D3D12_RESOURCE_STATES` state fields in **14** headers (`SceneBuffers` 7, `Renderer::m_depthState`, the GTAO, SSR, Echo, Underwater and Camouflage copies, and others). Each owner tracks its own resources, and nothing checks them against each other.
- **[C]** **157** `transitionX(` mentions (helpers plus calls). The hosts flip state by hand before debug overlays ([SandboxFrame.cpp L1514-L1577](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L1514-L1577), [EditorRender3D.cpp L1076-L1132](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Editor/EditorRender3D.cpp#L1076-L1132)).
- **[C]** 69 `renderer.bind*/clear*/transition*` calls in hosts and pipelines (13 in SandboxFrame, 11 in the Editor, 7 in SceneRenderer, the rest in pipelines). `bindColorTargetOnly()` quietly moves HDR and Post to SRV ([Renderer.cpp L714-L727](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Renderer.cpp#L714-L727)), and `applyGtao`/`applySsr` re-bind HDR after themselves ([SceneRenderer.cpp L443](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.cpp#L443), [L474](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.cpp#L474)). Correctness depends on call order spread across files.
- **[C]** No split barriers (`BEGIN_ONLY`/`END_ONLY`: 0), no aliasing barriers (0) and no enhanced barriers. The device is base `ID3D12Device` at FL 11_0, with no Agility SDK ([Renderer.cpp L233-L250](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Renderer.cpp#L233-L250)). There are 11 UAV barriers.

### 1.4 Descriptor heaps

- **[C]** 71 `CreateDescriptorHeap(` calls (68 Render, 2 Particles, 1 Ui), with **SHADER_VISIBLE** heaps created in **29** source files, roughly one per pipeline. That gives **42** `SetDescriptorHeaps(` calls, so the bound heap changes many times per frame.
- Shared tables are built by `CopyDescriptors` into the lighting heap (`setLightingAoSrv`, `setLightingSsrSrv`, `setIblSrvs`…, [Renderer.h L214-L220](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Renderer.h#L214-L220)). `PackedSrvHeap` ([PackedSrvHeap.cpp](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/PackedSrvHeap.cpp)) is a small helper, not a global allocator. ImGui owns a fixed-size heap ([Ui/ImGuiHost.cpp L288-L298](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Ui/ImGuiHost.cpp#L288-L298)).
- **[I]** This is the main thing slowing transient resources down. A transient texture needs fresh views wherever it's used, and today every pipeline caches CPU handles to resources it owns.

### 1.5 Command lists, threading, queues, async compute

- **[C]** Queues: one DIRECT and one COPY ([Renderer.cpp L274-L288](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Renderer.cpp#L274-L288)). `LocalShadowSystem` creates a **third, temporary DIRECT queue** at init to clear its dummy texture ([LocalShadowSystem.cpp L221-L230](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/LocalShadowSystem.cpp#L221-L230)).
- **[C]** Lists: one main list per frame, plus an optional **shadow list** recorded on a job worker ([Play/ShadowRecord.cpp L298-L370](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Play/ShadowRecord.cpp#L298-L370)) and submitted as `[shadow, main]` ([Renderer.cpp L812-L822](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Renderer.cpp#L812-L822)). There are also per-slot texture-update DIRECT and COPY lists.
- **[C]** **No compute queue** (`D3D12_COMMAND_LIST_TYPE_COMPUTE`: 0). Compute work, all on the direct list: GPU instance cull ([InstanceCull.cpp L454-L460](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/InstanceCull.cpp#L454-L460), [L921-L927](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/InstanceCull.cpp#L921-L927)), foliage cull ([FoliagePipeline.cpp L1453-L1469](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/FoliagePipeline.cpp#L1453-L1469)), GPU particles ([GpuParticleSystem.cpp L1239](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Particles/GpuParticleSystem.cpp#L1239), [L1354](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Particles/GpuParticleSystem.cpp#L1354)), movie YUV→RGB ([MovieFrameTexture.cpp L484](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/MovieFrameTexture.cpp#L484)) and terrain erosion (editor tool, [TerrainErosionPipeline.cpp L207-L245](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/TerrainErosionPipeline.cpp#L207-L245)). That's 14 compute PSOs in total. GTAO, SSR, bloom, TAA and the rest are pixel shaders.

### 1.6 Upload and readback

- [C] Buffer uploads go upload→default on the **COPY queue**, and the graphics queue `Wait`s on the copy fence ([Renderer.cpp L1030-L1050](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Renderer.cpp#L1030-L1050)). Texture uploads record on DIRECT (COPY_DEST is rejected on COPY lists, [Renderer.h L99-L105](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Renderer.h#L99-L105)). Per-frame texture updates (movie) go through a `texUpdateFence`. There's a `deferRelease` frame-count queue, the skinning upload ring (`SkinningUploadRing`) and per-frame constant rings inside the pipelines.
- [C] Readbacks: auto-exposure 1×1, read one frame late; timestamp queries resolved in `endFrame` ([Profile.cpp L162](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Profile.cpp#L162), [L456](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Profile.cpp#L456)); and the terrain readback.
- [C] `GpuUpload.h` mixes asset loading with IBL bake and includes `DeferredLightingPipeline.h` ([GpuUpload.h L3-L6](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/GpuUpload.h#L3-L6)). This is v2 fix C (the RHI carve-out).

### 1.7 Pipelines, PSOs and shaders

- [C] 30 `*Pipeline` classes, 71 graphics plus 14 compute `Create*PipelineState` calls, and 44 root signatures. Each pipeline builds its PSOs in `create()`, from HLSL compiled at runtime with **FXC `D3DCompile`** ([ShaderCompile.cpp L105](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/ShaderCompile.cpp#L105)). There's no `ID3D12PipelineLibrary` or PSO cache. `Render/Pipeline.h` is a stub ([Pipeline.h](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Pipeline.h)).
- [C] Variant toggles are pushed in through setters every frame (`setUltraviolet`/`setThermal` on 7 objects, [SandboxFrame.cpp L627-L640](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L627-L640)).

### 1.8 Quality and feature toggles

- [C] There's no quality-preset system. Passes are switched by `DebugRenderState` booleans (`bloom`, `taa`, `motionBlur`, `ssaoEnabled`, `ssrEnabled`, `decalsEnabled`, `gpuParticles`, `shadows`, `clouds`…; [DebugRenderState.h L15-L42](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/DebugRenderState.h#L15-L42)), combined with per-scene settings (`GtaoSettings`, `SsrSettings`) and `ScenePath` (forward or HybridDeferred, [ScenePath.h](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/ScenePath.h)). Every pass checks its own skip condition (e.g. [SceneRenderer.cpp L416-L427](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.cpp#L416-L427)). A skip still has to **restore bindings and states** by hand (e.g. the dummy SSR SRV and `bindHdr(false)`, [L456-L463](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.cpp#L456-L463)).
- [C] A disabled feature keeps its full-resolution textures allocated.

### 1.9 How the domain systems plug in

| System | Pipeline lives in | Who records | Coupling evidence **[C]** |
|---|---|---|---|
| Terrain, grass, foliage | `Render/TerrainPipeline`, `GrassPipeline`, `FoliagePipeline`, `TerrainErosionPipeline` | `Terrain::drawGBuffer(cmd, TerrainPipeline&…)` in Terrain/, called by the host | Render includes Terrain/ **11×** (e.g. [FoliagePipeline.cpp L23-L26](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/FoliagePipeline.cpp#L23-L26)); Terrain includes Render/ **28×** and mentions `ID3D12GraphicsCommandList` 21× |
| Sky, clouds, fog | `SkyPipeline`, `CloudVolumePipeline`, `Fog` | host / `SceneRenderer` | Render includes Sky/ **7×** ([SkyPipeline.h L3](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SkyPipeline.h#L3), [SsrPipeline.cpp L9](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SsrPipeline.cpp#L9), [Fog.cpp L3-L4](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Fog.cpp#L3-L4)) |
| Water, underwater | `WaterPipeline`, `UnderwaterPipeline` | `WaterWorld::draw(cmd, WaterPipeline&…)` | Render includes Water/ 2×; Water includes Render/ 24× |
| Particles | `ParticlePipeline`, `ParticleGpuPipeline`; `Particles/GpuParticleSystem` owns heaps, barriers and dispatches | `simulateAndDraw` | Particles includes Render/ 23×, 2 barriers, 5 `waitForGpu` |
| Media | `MovieFrameTexture`, `MoviePresentPipeline`, `LoadingScreen` | `m_movie.draw(renderer)` after post | Render includes Media/ 3× ([LoadingScreen.cpp L7-L9](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/LoadingScreen.cpp#L7-L9)) |
| Sprite2D | `SpritePipeline`, `Camera2D` | Editor `renderScene2D`, Sandbox2D | Simple |
| ImGui | `Ui/ImGuiHost` with its own heap | `m_imgui.render(renderer)` last on the swap chain | 3 `waitForGpu` in Ui |
| AI walk overlay | `Render/WalkabilityDebug` | host, outside any `PassId` | Render includes AI/ 2× ([WalkabilityDebug.cpp L3-L4](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/WalkabilityDebug.cpp#L3-L4)) |
| ECS | `LocalLightGather`, `DecalPool`, `LocalShadowSystem`, `CloudVolumePipeline`, `SceneRenderer` | | Render includes ECS/ 11× |
| Shadow casters | `ShadowSystem` / `LocalShadowSystem` in Render | **`Play/ShadowRecord.cpp`** | A render pass recorded from the Gameplay layer |

[C] `SceneRenderer.h` itself includes 31 pipeline headers and exposes 30 accessors so the hosts can drive them ([SceneRenderer.h L3-L33](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.h#L3-L33), [L141-L201](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.h#L141-L201)). That's v2 fix E ("Renderer names system pipelines").

### 1.10 Profiling

- [C] `GpuScope` RAII (PIX events plus timestamp queries; [Profile.cpp L470](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Profile.cpp#L470), [L515](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Profile.cpp#L515)) is placed by hand: 70 uses (Render 27, Editor 15, Sandbox 15, Terrain 5, Particles 3, Sandbox2D 3, Ui 2). Scope names don't match `PassId`s one-to-one (Decals, GTAO, SSR and Lighting have no host scope).
- [C] The D3D12 debug layer is on in `_DEBUG` unless PIX or Nsight is attached ([Renderer.cpp L133-L143](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Renderer.cpp#L133-L143), [L208-L213](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Renderer.cpp#L208-L213)). There's **no GPU-based validation switch**.

### 1.11 Other CPU stalls inside the frame

[C] There are 27 `waitForGpu()` call sites outside `Renderer.cpp`. The ones that can fire **while a frame is being recorded** are `ensureGtaoSize`, `ensureSsrSize`, the bloom and auto-exposure resizes ([SceneRenderer.cpp L354](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.cpp#L354), [L373](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.cpp#L373), [L703](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.cpp#L703), [L730](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.cpp#L730)), Echo ([L198](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/EchoPipeline.cpp#L198), [L255](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/EchoPipeline.cpp#L255)), Underwater ([L148](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/UnderwaterPipeline.cpp#L148)), Camouflage ([L284](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/CamouflagePipeline.cpp#L284)) and the particle pools growing ([GpuParticleSystem.cpp L308](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Particles/GpuParticleSystem.cpp#L308), [ParticleRenderer.cpp L76](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Particles/ParticleRenderer.cpp#L76), [L93](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Particles/ParticleRenderer.cpp#L93), [LeafRenderer.cpp L364](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Particles/LeafRenderer.cpp#L364), [BloodSplatPool.cpp L200](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Particles/BloodSplatPool.cpp#L200)). A window resize can cause up to 8 separate full GPU drains spread over the next frame. **[I]** This is legal, but it hitches and it's hard to reason about.

### 1.12 Forward path inventory (what D2 deletes)

| Item | Evidence **[C]** | What happens |
|---|---|---|
| `ScenePath` enum (2 values) and the live-path plumbing | [ScenePath.h](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/ScenePath.h), [Renderer.h L195-L198](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Renderer.h#L195-L198), [Renderer.cpp L1255-L1285](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Renderer.cpp#L1255-L1285); **54** `ScenePath::`/`scenePath()` references in **18** files | Delete the enum. `enableSceneBuffers()` becomes `createSceneTargets()`, with no path argument |
| `-forward` CLI flag | [Application.cpp L250-L258](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Core/Application.cpp#L250-L258), [Application.h L45-L46](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Core/Application.h#L45-L46) | Delete. An unknown `-forward` should log "removed" once |
| Silent fallback to forward when scene targets fail | [SandboxApp.cpp L1859-L1860](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxApp.cpp#L1859-L1860), [EditorAppInit.cpp L105-L106](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Editor/EditorAppInit.cpp#L105-L106) | Becomes a hard init error (log + message box + quit) |
| Forward pass tables | `sandboxForwardPasses` (11 nodes), `editorForwardPasses` (9 nodes), [PassTable.cpp L109-L173](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/PassTable.cpp#L109-L173) | Delete. Two tables remain until the graph replaces them |
| Sandbox forward-opaque branch and sky-first draw | [SandboxFrame.cpp L916-L1058](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L916-L1058) (~143 lines), [L664-L668](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L664-L668) | Delete |
| Editor forward branches | [EditorRender3D.cpp L468-L476](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Editor/EditorRender3D.cpp#L468-L476), [L498](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Editor/EditorRender3D.cpp#L498), [L535](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Editor/EditorRender3D.cpp#L535), [L673-L674](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Editor/EditorRender3D.cpp#L673-L674), [L807+](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Editor/EditorRender3D.cpp#L807) | Delete |
| Forward opaque pass enums | `MeshPass::ForwardUnorm` ([MeshPipeline.h L15-L20](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/MeshPipeline.h#L15-L20)), `SkinnedMeshPass::Forward` ([SkinnedMeshPipeline.h L14-L20](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SkinnedMeshPipeline.h#L14-L20)), `TerrainPass::ForwardUnorm` ([TerrainPipeline.h L15-L19](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/TerrainPipeline.h#L15-L19)), `SkyPass::ForwardFirst` ([SkyPipeline.h L18-L22](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SkyPipeline.h#L18-L22)) | Delete the values and their PSOs. `ForwardTransparent` **stays** |
| Forward-only shaders | `BasicMeshInstanced.hlsl` (109 lines), `SkinnedMeshForwardInstanced.hlsl` (117), `Terrain.hlsl` (127); used only by the forward PSOs ([InstancedMeshPipeline.cpp L365](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/InstancedMeshPipeline.cpp#L365), [SkinnedInstanceBuffer.cpp L455](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SkinnedInstanceBuffer.cpp#L455), [TerrainPipeline.cpp L122](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/TerrainPipeline.cpp#L122)) | Delete 3 of 50 `.hlsl` files (353 lines) |
| Instanced forward batches | `forwardReady` (17 refs, 9 files), `instanceForward` (17 refs), `MeshForwardBatchConstants`; "forward" appears 58× in `InstancedMeshPipeline.cpp` and 78× in `SkinnedInstanceBuffer.cpp` | Delete the forward PSOs, root signatures and batch paths. The G-buffer and depth paths stay |
| `ENCODE_SRGB` variants | 40 refs in 13 C++ files; used by 10 shader files (Sky, Water, Particle, ParticleBillboard, BasicMesh, SkinnedMesh + the 3 deleted shaders + `Color.hlsli`). They only matter when a 3D pass writes the UNORM swap chain | **[I]** Once 3D always renders to HDR, the encode variant of the 6 shaders that stay can go. 2D (Sprite, Line, LoadingScreen, Movie, DebugOverlay2D) already writes the swap chain without it. Confirm per pipeline |
| Always-true guards | `hasGBuffer()` 50 refs in 16 files, `hasSceneBuffers()` 19 in 10, `bindSceneTargets()` 4 | **[I]** Most collapse to the single "scene targets created" check at init. The "HDR but no G-buffer" branch ([EditorRender3D.cpp L468](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Editor/EditorRender3D.cpp#L468), [SandboxFrame.cpp L642](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L642)) looks unreachable already, because `ScenePath` has no such mode |
| Tests | `ParseAppCommandLine.ForwardFlag`, `ApplyDeferredScenePath.MapsForwardFlag`, `ApplyDeferredScenePath.ParseThenMap` ([ParseAppCommandLineTests.cpp L139-L166](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/UnitTests/Core/ParseAppCommandLineTests.cpp#L139-L166)); `InstancedMesh.ForwardBatchConstantsAre36Dwords` ([L132](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/UnitTests/Render/InstancedMeshPipelineTests.cpp#L132)); `SkinnedInstance.ForwardSignatureIs45` ([L473](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/UnitTests/Render/SkinnedInstanceBufferTests.cpp#L473)); `ShaderCompile.TerrainForwardCompiles` ([L130](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/UnitTests/Render/ShaderCompileTests.cpp#L130)); and if `ENCODE_SRGB` goes, the 3 `EncodeSrgb*` tests ([L41-L86](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/UnitTests/Render/ShaderCompileTests.cpp#L41)) | **6 tests deleted** (9 with `ENCODE_SRGB`). **4 edited**: the default-path assert (L59), `forwardReady` asserts in `InstancedMesh` (L70-L82), and the forward-table checks in `DrawListTests` (L381-L392) |

**Not in scope of D2 [C]:** the 2D path (Editor `renderScene2D`, Sandbox2D, sprites, start menu, loading screen, movie) draws straight to the swap chain. That's not the 3D forward path, and it stays.

**[I] Size of the deletion:** about 1,000–1,500 lines of C++, 353 lines of HLSL, 4 pass-enum values, 2 pass tables, 1 CLI flag and 6–9 tests. It also removes about 15 fewer PSOs created at startup (the forward opaque, instanced forward, skinned forward, terrain forward and sky-first PSOs, plus the encode variants). That count needs a build to confirm.

### 1.13 Game content built into the engine (what D7 moves)

| Item | Evidence **[C]** |
|---|---|
| Camouflage | `Render/CamouflagePipeline.*`, owned by `SceneRenderer` ([SceneRenderer.h L33](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.h#L33), [L153](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.h#L153)), created at [SceneRenderer.cpp L156](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.cpp#L156); `PassId::Camouflage` |
| Echo | `Render/EchoPipeline.*`, `Render/EchoChirpFile.*`, owned by `SceneRenderer` ([L21](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.h#L21), [L161](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/SceneRenderer.h#L161)); recorded in Sandbox post ([SandboxFrame.cpp L1365-L1402](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L1365-L1402)) |
| Vision modes (infrared/thermal, ultraviolet) | **~200** references in **~30** engine C++ files and **71** in **13** shaders (`Thermal.hlsli`, `Ultraviolet.hlsli`, G-buffer, terrain, foliage, grass, sky, water…). Correction from the earlier count (215/31 and 79/14): 15 C++ refs in `TerrainErosionPipeline.*` and 8 refs in `TerrainErosion.hlsl` are thermal *erosion* (`CSThermal`, `thermalRate`) and stay. 7 G-buffer pipelines compile extra thermal and UV pixel-shader variants with a per-pipeline class literal (`makeThermalMacros("0.41"/"0.40"/"0.72")`, `makeUvMacros`) ([ShaderCompile.h L47-L68](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/ShaderCompile.h#L47-L68)). `LightingConstants::thermal` is a lighting-constant slot ([DeferredLightingPipeline.h L50-L57](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/DeferredLightingPipeline.h#L50-L57)), and G-buffer RT0.a carries its sign. 13 setters are called per frame ([SandboxFrame.cpp L627-L640](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L627-L640)) |
| Names in engine enums | `PassId::Camouflage`, the Echo/Camouflage `ProfileColor`s ([Profile.h](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/Profile.h)) |

**[I]** Camouflage and Echo are self-contained passes and move out easily. Vision modes are woven into the G-buffer, lighting, sky and water shaders, but per D11 they become full-screen game passes. The engine only has to expose generic switches for earlier passes and a generic per-surface tag (§3.4.2). That turns the largest item of D7 into a mostly deleting job.

---

## 2. Concrete inflexibilities

| # | Problem | Evidence | Cost today |
|---|---|---|---|
| F1 | **The hosts hard-code pass order and duplicate it** | ~1,170-line `SandboxFrame::render` and ~975-line `renderScene3D`; 4 static tables used only after the fact; `(void)passSequenceOk` | Every new pass is edited into 2 hosts, 2 tables and `PassId`. Editor and Sandbox drift apart (e.g. grid placement, no echo in the Editor) |
| F2 | **Barriers are manual and state is spread around** | 37 barrier sites, 30 state fields in 14 headers, 157 transition mentions, 69 bind/clear/transition calls | Order-dependent bugs. A skipped pass has to "restore" bindings. Barriers can't be batched |
| F3 | **Bindings are an implicit state machine** | `bindHdr(false)` after GTAO/SSR, `bindColorTargetOnly` changes state | A reorder can silently draw into the wrong target |
| F4 | **Committed, fixed-size, lazily resized targets** | 58 `CreateCommittedResource`, 0 placed; 9 independent resize paths with mid-frame `waitForGpu` | About 200 MB at 1080p **[I]**, with no reuse. Disabled features still use memory. Resizes hitch |
| F5 | **History is handled ad hoc** | TAA copies Post→History; GTAO and SSR swap index pairs; 4 validity flags | Each new temporal effect brings its own reset logic. A camera cut has to be remembered in several places |
| F6 | **Render includes domain systems, and the reverse** | Render→Terrain 11, ECS 11, Sky 7, Media 3, AI 2, Water 2; Terrain/Water/Particles→Render 28/24/23; shadows recorded from Play/ | Blocks v2 Phase 4 (fix E) and the 67→0 layering target |
| F7 | **`SceneRenderer` is a god object** | 31 includes, 30 accessors | Every feature is compiled into every host; hard to test |
| F8 | **One queue, one main list** | No compute queue; one worker list (shadows) | No overlap. Recording on one thread costs CPU time **[I]** |
| F9 | **Descriptor heap per pipeline** | 29 shader-visible heap creators, 42 `SetDescriptorHeaps` | Blocks transient resources; heap switches; no bindless path |
| F10 | **Overlays and debug passes aren't modelled** | Walk overlay, path debug, skeleton, decal volumes have no `PassId`; debug tiles flip state by hand | They can't be culled or shown in a graph dump |
| F11 | **Vision modes are pushed into pipelines** | 13 setter calls per frame; 2 extra PSO variants in each of 7 G-buffer pipelines | They belong in a frame-setup step plus a game pass (D11) |
| F12 | **Validation tooling is thin** | No GBV switch; no resource lifetime view | Hard to bisect barrier bugs |
| F13 | **Two 3D render paths** | `ScenePath` forward vs deferred: 54 refs in 18 files, 2 extra pass tables, forward-only PSOs and shaders (§1.12) | Every feature needs a forward variant or a guard; doubles test configs. **Removed in Phase 1 (D2)** |
| F14 | **Game content inside the engine** | Camouflage, Echo, thermal/UV in ~30 engine C++ files and 13 shaders (§1.13) | Engine can't be reused without the game; blocks the layering goal. **Moved in Phase 8 (D7)** |


---

## 3. Design: a render graph for DE6

### 3.1 Goals and non-goals

**Goals:**
- the graph drives the order, over one deferred path;
- barriers are automatic, with state tracked in one place, using **enhanced barriers only** (D13);
- the graph owns the screen-sized resources through D3D12MA and handles resizing;
- transient aliasing, sized for 8 GB;
- history as a first-class resource;
- queues and fences are part of the graph model from day one, and async compute is a planned phase;
- engine features register their passes from their modules, and **game features register from `Game.Core`**, including frame-setup hooks that switch off or parameterise earlier passes (D11);
- parallel recording, and a debug dump plus inspector.

**Non-goals:**
- a second 3D render path (D2);
- an offscreen Editor viewport (D3);
- quality presets (D8);
- recompiling the graph every frame (the compiled plan is cached until the pass set or the resolution changes);
- data-defined (JSON) passes;
- hot reload of any kind;
- full bindless in this plan (SM 6.6 descriptor-heap indexing is a later, separate step; §3.9);
- a legacy-barrier path (D13) or Windows 10 support (D9);
- runtime shader compilation in Release (D10).

### 3.2 Where it lives (v2 layering, no new library)

```
Dark.Render  (library #4 of 9)
  rhi       : RhiDevice (Agility SDK device, FL 12_1, caps), queues (DIRECT, COMPUTE, COPY), fences,
              ResourceStateTracker, EnhancedBarrierEmitter, GpuAllocator (D3D12MA),
              TransientPool, DescriptorRing, GpuScope, ShaderLibrary (baked DXIL | runtime DXC), PsoUtil  <- v2 fix C
  render    : RenderGraph (builder, compiler, queue scheduler, executor), RgBlackboard,
              IRenderFeature, IFrameSetupHook, FrameSetup, RenderFeatureRegistry, FrameComposer,
              core features (Shadows, LocalShadows, GBuffer, Decals, GTAO, SSR, DeferredLighting,
              LocalLights, Translucent, Post chain, DebugTiles)
  sky       : SkyFeature, CloudVolumeFeature, FogProvider               (includes render, never the reverse)
  terrain   : TerrainGBufferFeature, GrassFeature, FoliageFeature, ShadowCasterSource
  water     : WaterFeature, UnderwaterFeature
  particles : GpuParticleFeature (sim pass + draw pass), CpuParticleFeature, LeafFeature
  media     : MoviePresentFeature, LoadingScreen
  sprite2d  : Sprite2DFeature
  ui        : MainMenu (non-ImGui)
Dark.ImGui  : ImGuiFeature (last pass on the back buffer) + Graph Inspector panel
Dark.Gameplay (ai module) : WalkOverlayFeature
Game.Core   : CamouflageFeature, EchoFeature, VisionModesFeature (IRenderFeature + IFrameSetupHook)   <- D7, D11
Dark.App / exes : FrameComposer setup, choosing the feature list per host (2 lists: Sandbox, Editor 3D)
```

The module order `rhi → render → sky → terrain → water → particles → media → sprite2d → ui` is kept. `render` knows only `IRenderFeature`, `IFrameSetupHook` and the provider interfaces (`IEnvironmentProvider`, `IHeightProvider`, `IShadowCasterSource`). That removes the Render→Terrain (11), Sky (7), ECS (11), AI (2) and Media (3) includes, and `Dark.Render` no longer names Camouflage, Echo, thermal or ultraviolet. ImGui stays out of the engine libraries and engine tests. D3D12MA and the Agility SDK headers are `third_party` dependencies linked privately by `Dark.Render`, so they aren't libraries in the count.

### 3.3 Core types (no exceptions; errors are `bool`/enum plus a log)

```cpp
// render/RenderGraph.h  (sketch)
namespace Dark::Rg {

struct TextureHandle { uint16_t index = 0xFFFF; uint16_t version = 0; bool valid() const { return index != 0xFFFF; } };
struct BufferHandle  { uint16_t index = 0xFFFF; uint16_t version = 0; bool valid() const { return index != 0xFFFF; } };

enum class SizeMode : uint8_t { Absolute, RelativeToOutput };   // RelativeToOutput: scale x output size

struct TextureDesc {
    SizeMode    sizeMode = SizeMode::RelativeToOutput;
    float       scale    = 1.0f;          // 0.5 = half-res
    uint32_t    width = 0, height = 0;    // Absolute only
    uint16_t    mips = 1, arraySize = 1;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;   // resource format (may be TYPELESS)
    DXGI_FORMAT viewFormat = DXGI_FORMAT_UNKNOWN;
    float       clear[4] = {};            // used for RT/DS optimized clear + first-use init
};

enum class Use : uint8_t {          // the only thing passes say about access
    PixelSrv, ComputeSrv, AnySrv,
    RenderTarget, DepthWrite, DepthRead,
    Uav, CopySrc, CopyDst, IndirectArgs, VertexIndex, Present
};

enum class Queue : uint8_t { Graphics, AsyncCompute };
enum class PassFlags : uint8_t { None = 0, SideEffect = 1, NeverCull = 2, OwnBarriers = 4 /* opaque pass during migration */ };

class PassBuilder {
public:
    TextureHandle create(const char* name, const TextureDesc& d);          // transient
    BufferHandle  create(const char* name, const BufferDesc& d);
    TextureHandle read(TextureHandle h, Use u = Use::PixelSrv, uint16_t mip = kAllMips);
    TextureHandle write(TextureHandle h, Use u = Use::RenderTarget);       // returns new version
    TextureHandle readWrite(TextureHandle h, Use u = Use::Uav);
    BufferHandle  read(BufferHandle h, Use u);
    BufferHandle  write(BufferHandle h, Use u);
    void          queue(Queue q);
    void          flags(PassFlags f);
    void          scope(const char* gpuScopeName, uint32_t color);         // auto GpuScope
};

// Execute-time view of resolved resources. Valid only inside execute.
class PassContext {
public:
    ID3D12GraphicsCommandList* cmd() const;
    ID3D12Resource*            resource(TextureHandle h) const;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv(TextureHandle h, uint16_t mip = 0) const;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv(TextureHandle h, bool readOnly = false) const;
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu(TextureHandle h) const;   // staging (non-visible) handle
    D3D12_GPU_DESCRIPTOR_HANDLE srvTable(std::initializer_list<TextureHandle> hs); // copies into frame ring
    D3D12_GPU_DESCRIPTOR_HANDLE uav(TextureHandle h);
    uint32_t width(TextureHandle h) const; uint32_t height(TextureHandle h) const;
    uint32_t frameIndex() const;
};

class RenderGraph {
public:
    // Setup runs now, on the calling thread. Execute is stored and run later.
    // Data is a POD the pass fills in setup and reads in execute (lives in the frame arena).
    template <class Data, class Setup, class Exec>
    Data& addPass(const char* name, Setup&& setup, Exec&& exec);

    TextureHandle importTexture(const char* name, ID3D12Resource* r, D3D12_RESOURCE_STATES current,
                                D3D12_RESOURCE_STATES finalState, const ImportViews& views = {});
    BufferHandle  importBuffer (const char* name, ID3D12Resource* r, D3D12_RESOURCE_STATES current,
                                D3D12_RESOURCE_STATES finalState);
    TextureHandle backBuffer();                                  // imported every frame, ends in PRESENT

    HistoryPair   history(const char* name, const TextureDesc& d);  // see 3.6
    void          invalidateHistory();                             // camera cut / scene change / resize

    RgBlackboard& blackboard();                                   // typed slots features share

    bool compile();                              // cull, order, lifetimes, barriers, aliasing; false = logged error
    bool execute(RgExecutor& exec);              // records (1..N lists), submits through RhiDevice
    void dumpDot(std::string& out) const;        // Graphviz
    void dumpJson(std::string& out) const;       // for the inspector
};
} // namespace Dark::Rg
```

The **setup/execute lambdas** are stored in a per-frame linear arena: a type-erased `{ void* data; void (*exec)(void*, PassContext&); }` thunk. That avoids `std::function` heap allocations and needs no exceptions. Setup can **fail softly** (a feature finds a missing pipeline): it calls `b.flags(PassFlags::NeverCull)` and nothing else, or just doesn't add the pass. Compile errors (reading an unwritten resource, two writers with no order, a cycle) return `false`, log once with the pass and resource names, and the frame falls back to "clear the back buffer + ImGui" instead of crashing.

**Example: GTAO as a feature pass**

```cpp
struct GtaoOut { Rg::TextureHandle aoFull; };

void GtaoFeature::addPasses(Rg::RenderGraph& g, const FrameView& v)
{
    if (!v.settings.gtao.enabled || !m_pipe.isValid()) return;             // culled = no memory, no barrier
    auto& gb  = g.blackboard().get<GBufferOut>();
    auto  hist = g.history("gtao.ao", { .format = DXGI_FORMAT_R16_FLOAT });

    auto& out = g.addPass<GtaoOut>("GTAO",
        [&](Rg::PassBuilder& b, GtaoOut& o) {
            b.scope("GTAO", ProfileColor::Gtao);
            b.read(gb.depth,  Rg::Use::DepthRead);
            b.read(gb.attrib);
            b.read(gb.velocity);
            b.read(hist.prev);
            auto half = b.create("gtao.half", { .scale = 0.5f, .format = DXGI_FORMAT_R8_UNORM });
            half      = b.write(half);
            hist.curr = b.write(hist.curr);
            o.aoFull  = b.write(b.create("gtao.full", { .format = DXGI_FORMAT_R8_UNORM }));
        },
        [this, v](Rg::PassContext& c, const GtaoOut& o) {
            m_pipe.draw(c, v.camera, v.prevViewProj, v.settings.gtao);    // uses c.rtv()/c.srvTable(); no barriers
        });
    g.blackboard().set(out);   // Lighting reads GtaoOut if present, else the authored AO
}
```


### 3.4 Features and registration (v2 fix E)

```cpp
class IRenderFeature {
public:
    virtual ~IRenderFeature() = default;
    virtual const char*  name() const = 0;
    virtual bool         create(RhiDevice& dev, const FeatureCreateInfo& info) = 0;  // PSOs, persistent resources
    virtual void         destroy(RhiDevice& dev) = 0;
    virtual void         addPasses(Rg::RenderGraph& g, const FrameView& view) = 0; // called every frame
    virtual FeatureStage stage() const = 0;
};

// Stages are the only fixed order the engine knows. Features fill them.
enum class FeatureStage : uint8_t {
    PreFrame,       // GPU sims, culls (async-compute candidates)
    GBuffer, Shadows /* after the G-buffer, D12 */, PostGBuffer /* decals, AO, SSR */, Lighting, Sky,
    Transparent     /* water, clouds, translucent, particles */,
    PreTonemapHdr   /* underwater, game composites such as Echo */,
    Post, Overlay, Ui
};

class FrameComposer {
public:
    void add(IRenderFeature& f);                  // the host builds its list once at boot
    void addSetupHook(IFrameSetupHook& h);        // optional; run in registration order (D11)
    bool renderFrame(const FrameView& view);      // frame setup -> addPasses -> compile (cached) -> execute -> present
};
```

- **Order:** features are called in `stage()` order, then registration order. Inside the graph, order comes from the data dependencies, with insertion order as the tie-breaker. With D2 there are only **2 feature lists** (Sandbox, Editor 3D); the Editor list simply leaves out the game features and adds `EditorGridFeature`/`EditorGizmoFeature`.
- **`FrameView`:** camera, matrices, jitter, settings, `DebugRenderState` (D8) and the frame's resolved `FrameSetup` (§3.4.2). There's no `ScenePath` field anymore (D2).
- **Providers** keep Render free of domain headers: `IEnvironmentProvider` (Sky), `IHeightProvider` (Terrain), `IShadowCasterSource` (Gameplay/Terrain/Foliage). `Play/ShadowRecord.cpp` becomes the render-side `ShadowFeature` plus a Gameplay caster source.
- **Explicit registration only** (statics get stripped from static libraries): `registerCoreFeatures`, `registerSkyFeatures`, …, and `Game::registerRenderFeatures(composer)` from `Game.Core`'s boot function.

#### 3.4.1 Game features (D7)

`Game.Core` owns `CamouflageFeature` and `EchoFeature` (moved from `Render/CamouflagePipeline.*`, `Render/EchoPipeline.*`, `Render/EchoChirpFile.*`), plus their shaders under the game's content folder. They use only public graph and render APIs: the G-buffer, depth and HDR come from the blackboard (`GBufferOut`, `HdrOut`), and they write HDR in `FeatureStage::Transparent` (Camouflage) or `PreTonemapHdr` (Echo). The temporal hold today (`applyPost(..., holdTemporal)` when echo is on) becomes a generic `FrameView::holdTemporal` flag that any feature can set. `PassId::Camouflage` and the Echo/Camouflage `ProfileColor` values are deleted from the engine. Game passes get their colours from a hash of their name.

#### 3.4.2 Frame-setup hook (D11: vision modes as full-screen passes)

Vision modes are full-screen passes, but some modes turn lighting off and need to configure earlier passes. So the engine gets a **frame-setup step** that runs after `FrameView` is built and **before any feature adds passes or the graph compiles**. Game.Core hooks write to a generic `FrameSetup`. The engine reads only generic switches and never learns what a mode is.

```cpp
// render/FrameSetup.h: generic, with no game names. The engine owns this list of its own passes.
enum class EnginePass : uint8_t {
    DirectionalShadows, LocalShadows, Decals, Gtao, Ssr, DeferredLighting, LocalLights,
    Sky, Clouds, SsrCapture, Underwater, Bloom, Taa, MotionBlur, AutoExposure, Count
};
using EnginePassMask = Bitset<uint32_t(EnginePass::Count)>;

// out = lerp(c, M * float4(c, aux), strength). Identity/strength 0 means "unchanged".
struct ColorRemap { float m[3][4] = { {1,0,0,0}, {0,1,0,0}, {0,0,1,0} }; float aux = 0.0f; float strength = 0.0f; };

struct FrameSetup {
    EnginePassMask disabled;               // the graph culls these passes and any target only they use
    float shadowStrength = 1.0f;           // 0..1, read by lighting, water, translucent
    float aoStrength = 1.0f, ssrStrength = 1.0f, emissiveGain = 1.0f;
    bool  writeSurfaceTag = false;         // G-buffer also writes the R8G8 SurfaceTag target (see below)
    std::span<const float> surfaceTagLut;  // optional 256 values, owned by the hook; read as the "aux" of albedoRemap
    ColorRemap albedoRemap;                // applied to G-buffer albedo inside lighting and local lights
    ColorRemap fogRemap, waterRemap, skyRemap;
    bool holdTemporal = false;             // already generic (§3.4.1)
};

class IFrameSetupHook {
public:
    virtual ~IFrameSetupHook() = default;
    virtual void setupFrame(const FrameView& view, FrameSetup& inOut) = 0;   // no GPU work, no allocation
};
```

**Order in `FrameComposer::renderFrame`:**
1. Build `FrameView`, then start `FrameSetup` from defaults plus `DebugRenderState` (D8). A debug toggle that's off is the same as a `disabled` bit.
2. Call each `IFrameSetupHook` in registration order. `disabled` bits are OR-ed. For scalar params the last writer wins, and `rg.validate` logs when two hooks write the same field.
3. Each feature's `addPasses` reads `view.setup`. A disabled engine pass isn't added. Its consumers use `g.tryRead(handle, Fallback::White/Black/NoShadow)`, so lighting gets a 1×1 white AO, water and translucent get an "unshadowed" binding, and so on.
4. Compile. Culling (§3.5) drops anything that only fed a disabled pass, and transient targets that nothing uses aren't allocated. The plan cache key includes the `disabled` mask and `writeSurfaceTag`, so switching a mode on or off costs one cached recompile, not one per frame.
5. History: if a history writer (GTAO, SSR, TAA) was culled last frame, its `prev` is invalid on the frame it comes back. The graph does this generically, with no per-feature reset code.

Today every vision mode already switches underwater off (`underDraw = deferred && !visionPicture`, [SandboxFrame.cpp L614-L615](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L614-L615)); that becomes a `disabled` bit. Persistent resources (the CSM atlas, the local-shadow array) stay allocated while their passes are off. They just aren't rendered.

**SurfaceTag, a generic per-surface channel.** Today the per-surface data the modes need comes from 2 extra PSO variants in each G-buffer pipeline. `DE_THERMAL` stores a heat class in RT0.a, using a per-pipeline literal (0.40 foliage and grass, 0.41 meshes and terrain, 0.72 skinned). `DE_ULTRAVIOLET` stores UV reflectance from the authored `gUv` mask or a class literal. Both overwrite emissive. The replacement:
- **`SurfaceTag` target (`R8G8_UNORM`, transient, ~2 B/px):**
  - R is an 8-bit tag. It defaults to an engine **surface category** (StaticMesh, SkinnedMesh, Terrain, Grass, Foliage). Those are the engine's own categories, not game names, and a material or entity can override the tag with an opaque `uint8_t surfaceTag`.
  - G is an optional **material aux mask**: today's `gUv` texture, renamed and kept generic.
  - It's declared only when `writeSurfaceTag` is set, so normal frames don't pay for it.
- **One generic variant per G-buffer pipeline (`DE_GBUFFER_SURFACE_TAG`, one extra MRT)** instead of two vision variants. That's 7 pipelines × 3 PS variants → 7 × 2, so **7 fewer PSOs [I]**. RT0.a always stays emissive, which fixes today's "alpha is heat, not emissive" special case in lighting.
- The game maps tag to heat or UV reflectance through `surfaceTagLut` or its own buffer. Metallic is already in `attrib.a` ([BasicMeshGBuffer.hlsl L83](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/content/shaders/BasicMeshGBuffer.hlsl#L83)) and emissive in `albedo.a`, so today's `thermalScalar(emis, metal)` can be computed full-screen.

**How today's two modes map onto it (Game.Core `VisionModesFeature`, not engine code) [I]:**

| Mode (game) | `setupFrame` | Game full-screen pass |
|---|---|---|
| Heat (infrared), strength 1 | `disabled = {DirectionalShadows, LocalShadows, Gtao, Ssr, DeferredLighting, LocalLights, SsrCapture, Underwater}`; `writeSurfaceTag`; `waterRemap` = flat colour (zero matrix + offset = palette(0.30)) | `PostGBuffer`/`Lighting` slot: reads albedo, attrib, depth and SurfaceTag, and writes HDR = `thermalFalseColor(heat)`. Sky pixels (`depth == far`) get the cold-air colour plus the sun disk from `FrameView` sun dir. Bloom stays on (the palette's hot stop relies on it). |
| Heat, 0 < strength < 1 (fade) | `disabled = {Underwater}`; `writeSurfaceTag` | The same pass blends over the lit HDR, as `lerp(lit, heatCol, s)` does today |
| UV (bee) | Lighting stays on. `disabled = {Underwater}`; `writeSurfaceTag`, `surfaceTagLut` = UV classes, `albedoRemap` = channel shift (g→r, b→g, aux→b), `fogRemap`/`skyRemap`/`waterRemap` = the same shift with the fog/sky/water aux constants | Optional. The shift happens before lighting, so it has to be a parameter of the lighting pass, not a post remap |

`Thermal.hlsli` and `Ultraviolet.hlsli` move to Game.Core's shader folder. The engine's lighting, local-light, fog, sky and water shaders each get one generic `ApplyColorRemap(...)` call (one shared `ColorRemap.hlsli`). The remap is generic: a 3×4 colour matrix plus an aux scalar.

**Shader refs removed (of the 79 counted earlier) [C counts, I outcome]:**

| Shader(s) | Refs | Outcome |
|---|---|---|
| `TerrainErosion.hlsl` | 8 | **Stays.** Thermal *erosion*, not a vision mode (miscounted before) |
| `Thermal.hlsli` | 13 | Moves to Game.Core |
| 7 G-buffer writers (Basic, BasicInstanced, Skinned, SkinnedInstanced, Foliage, Grass, Terrain) + `UvMap.hlsli` | 28 (15 thermal, 13 UV) | All go. Each writer gets one generic `#ifdef DE_GBUFFER_SURFACE_TAG` write; `UvMap.hlsli` → `MaterialAuxMask.hlsli` |
| `DeferredLighting.hlsl` | 8 | All go (the lighting-off returns, the heat lerp, the emissive skip and the fog shift). Replaced by `ApplyColorRemap` on albedo and fog |
| `LocalLightVolume.hlsl` | 2 | Go, same generic remap |
| `Sky.hlsl` | 9 | Go. UV uses `skyRemap`; heat is drawn by the game pass on sky pixels |
| `Water.hlsl` | 11 | Go, replaced by one `waterRemap` |
| **Total** | **79** | **71 vision refs leave the engine (100%)**; about 15–20 generic lines are added **[I]** |

**C++ side [I]:** about 200 refs in ~30 files go: the 13 per-frame setters, `LightingConstants::thermal`, `makeThermalMacros`/`makeUvMacros` and the variant blocks in 7 pipelines, and `SandboxFrame`'s `visionPicture`/`uvDraw`/`thermalDraw` branches ([SandboxFrame.cpp L614-L662](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L614-L662)). What's added is `FrameSetup`, `EnginePass`, one variant flag and a few `tryRead` fallbacks.

**Why this is smaller than the v2 include hook:** no per-surface shader hook, no extra permutation axis for "a hook is registered", and no game HLSL compiled into engine shaders. Game code reaches the engine only through `FrameSetup` and ordinary graph passes.

### 3.5 Compile: culling, ordering, lifetimes, queues

1. **Culling:** reference-count from the sinks (back buffer, `SideEffect` passes, history writes, readbacks). A `DebugRenderState` toggle that turns a feature off means it adds no passes, so its resources are never allocated (D8).
2. **Order:** a topological sort that respects insertion order.
3. **Queue assignment (from Phase 3):** each pass declares `Queue::Graphics` or `Queue::AsyncCompute`. The compiler builds per-queue pass sequences and **sync points** (§3.11). With `rg.asyncCompute=0` every pass is forced onto Graphics. That's also the default until Phase 10, so the model is tested long before it's used.
4. **Lifetimes:** first and last use per resource, *per queue*. The alias planner treats a cross-queue lifetime as live from the producer's start to the sync point after the last consumer.
5. **Caching:** the plan is keyed by `(pass names + flags + queues, resource descs, output size)`.

### 3.6 History and ping-pong resources

`g.history(name, desc)` returns `{prev, curr}`. Two persistent D3D12MA allocations owned by the graph registry are swapped after `execute`. `prev` is invalid after a resize, after `invalidateHistory()` (camera cut, `discardTemporalHistory`, level change, or a feature setting `FrameView::holdTemporal`), or on first use. This replaces `m_taaHistoryValid`, `m_gtaoNeedReset`, `m_ssrNeedReset` and the TAA Post→History copy. Auto-exposure gets `g.readback(name, bytes, frames)`, a ring of readback buffers.

### 3.7 Barriers and state tracking

- **ResourceStateTracker (rhi)** holds one table for imported and persistent resources. Transient resources start each frame undefined.
- **EnhancedBarrierEmitter (rhi), the only back end (D13).** It emits `ID3D12GraphicsCommandList7::Barrier` with sync/access/layout triples:
  - `LAYOUT_UNDEFINED` + `DISCARD` for the first use of an aliased or freshly created transient;
  - `NO_ACCESS` for resources idle in a pass;
  - common-compatible layouts (`COMMON`, or `SHADER_RESOURCE` where it's legal on both queues) for cross-queue handoff (§3.11).

  The compiler produces `{resource, before-use, after-use, queue}` records and the emitter turns them into barrier groups, merged per pass. Unit tests check the records and the emitted `D3D12_TEXTURE_BARRIER`/`D3D12_BUFFER_BARRIER` structs, with no device needed.
- **No legacy emitter.** There's no `D3D12_RESOURCE_STATES` back end, no `rg.forceLegacyBarriers` and no A/B between emitters. Without that A/B reference, bisecting uses `rg.fullBarriers` (a full sync/access barrier between every pass), `rg.validate`, GBV and PIX.
- **Startup check (Phase 2, §3.15)** runs at device creation, before any window content:
  - `D3D12_FEATURE_D3D12_OPTIONS12::EnhancedBarriersSupported` must be true. That bit is authoritative.
  - The user-mode driver version (`IDXGIAdapter::CheckInterfaceSupport` → UMD version) must be at least a pinned minimum per vendor **[I]: Travis pins the NVIDIA minimum once Phase 2 runs on the 2080**. The version is used for the message and to block known-bad drivers.
  - On failure DE6 shows a message box naming the GPU, the driver version found, the minimum needed and the reason, writes it to the log, and exits non-zero. There's no degraded mode.
- **Opaque passes during migration** (`PassFlags::OwnBarriers`, Phases 4–6): the old code keeps its own `ResourceBarrier` calls until it's converted. The graph emits enhanced barriers at the opaque pass's boundaries, mapping the declared legacy entry/exit states to layouts through the enhanced-barrier spec's legacy-compatibility rules (legacy and enhanced barriers can be mixed in one command list when the states match the layouts) **[I]: verify each boundary with GBV**. With `rg.validate` it re-reads the owner's `m_*State` and logs mismatches. At the end of Phase 6 no `ResourceBarrier` call remains outside `rhi`, and `rhi` has none either.
- **Split barriers** later use `D3D12_BARRIER_SYNC_SPLIT` (Phase 12 option), not `BEGIN_ONLY`/`END_ONLY`.

### 3.8 Transient resources, D3D12MA and the 8 GB budget

- **Allocator:** D3D12MA (`D3D12MA::Allocator` created on the Agility device). Persistent textures and buffers use `CreateResource` (committed or pooled, D3D12MA decides). Transients use **one D3D12MA custom pool** of `ALLOW_ALL_BUFFERS_AND_TEXTURES` heaps. That's allowed because the 2080 is Resource Heap Tier 2, so there's no RT/DS/buffer bucketing. The alias planner then calls `CreateAliasingResource(allocation, offset, desc)` for each transient in the cached plan.
- **One transient set, not one per frame slot:** frames run in order on the graphics queue. At the start of a frame, the graphics queue `Wait`s (GPU-side, no CPU stall) on the compute fence value of the previous frame's last async pass. After that, no previous-frame work can still touch the pool, and `LAYOUT_UNDEFINED` + `DISCARD` barriers handle reuse inside the frame. **[I]** This halves pool memory compared with per-slot heaps.
- **Sizing for 8 GB [I]**, from §1.2 formats. After D2 the per-frame transients are HDR, albedo, attrib, velocity, post, AO, the GTAO and SSR intermediates, the bloom chain, the underwater copy, and the Game.Core Camouflage/Echo copies: about **65 B/px** if nothing were aliased. The D11 `SurfaceTag` target (2 B/px, ~7 MB at 1440p) exists only while a hook asks for it. A heat-mode frame culls GTAO, SSR and their intermediates, so it's net smaller. Persistent screen-sized resources are depth, the TAA history, the GTAO history pair and the SSR history pair: about **32 B/px**.

| Resolution | Transient, no aliasing | Transient, aliased (est. 50–60%) | Persistent screen-sized | Total RT memory |
|---|---|---|---|---|
| 1920×1080 | ~135 MB | ~70–80 MB | ~66 MB | ~150 MB |
| 2560×1440 | ~240 MB | ~130–145 MB | ~118 MB | ~260 MB |
| 3840×2160 | ~540 MB | ~290–325 MB | ~265 MB | ~590 MB |

- **Pool policy:** the first block is 256 MB, which covers 1440p aliased with headroom. The pool grows in 64 MB blocks to a **768 MB hard cap**, enough for 4K even with `rg.noAliasing` at 1440p. Past the cap, compile fails with a log naming the biggest transients and the frame falls back to no-aliasing committed resources for that plan. Each frame the renderer reads `D3D12MA::Allocator::GetBudget`. The Graph Inspector shows usage, and a one-time warning fires above 85% of the local budget. **[I]** Render targets stay under ~8% of 8 GB at 4K, leaving the rest for textures, meshes, shadows (the CSM atlas and local-shadow array are persistent and not counted above) and the particle pools.
- **First use** of an aliased resource is `LAYOUT_UNDEFINED` + `DISCARD` (enhanced only, D13), or a clear when the first pass declares `loadOp=Clear`.
- **Debug toggle:** `rg.noAliasing`.

### 3.9 Descriptors (and the later bindless option)

- **DescriptorRing (rhi):** one large shader-visible CBV/SRV/UAV heap (for example 256k descriptors, which Turing's Resource Binding Tier 3 allows; ~8 MB **[I]**) split into a persistent region and per-frame ranges. A CPU staging heap holds the graph-owned views. `PassContext::srvTable({...})` copies into the frame range. RTVs and DSVs come from graph-owned CPU heaps. Converted pipelines bind the ring heap once per list, which removes most of today's 42 `SetDescriptorHeaps` calls.
- **SM 6.6 bindless (later, separate):** because the ring is one heap, SM 6.6 `ResourceDescriptorHeap[index]` indexing can come in later without changing the graph. The pass reads `c.srvIndex(h)` instead of a table. Gate it on `D3D12_FEATURE_SHADER_MODEL >= 6_6` and Resource Binding Tier 3. Not scheduled in this plan (§4 Phase 12 option).

### 3.10 Multi-threaded recording

The compiled per-queue sequences are split into **batches**. Each batch records on a job with its own per-frame allocator and list. Barriers between batches are recorded at the start of the next batch. Submission is in order per queue, with the fence waits from §3.11 between them. The existing shadow worker ([Play/ShadowRecord.cpp L298-L370](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Play/ShadowRecord.cpp#L298-L370)) becomes the first graphics batch. Execute lambdas may only read their `Data`, the `FrameView` and immutable feature state. The constant and upload rings get per-thread chunks.

### 3.11 Async compute (planned, D6)

**Model (in the graph from Phase 3):**

```cpp
struct RgSyncPoint { Queue signalQueue; uint32_t afterPass; Queue waitQueue; uint32_t beforePass; };
struct RgCompiled {
    std::span<const uint32_t>    passes[2];       // per queue, in execution order
    std::span<const RgSyncPoint> syncs;           // minimal set: transitive reduction of cross-queue edges
    std::span<const BarrierRec>  barriers;        // per pass, per queue; queue-legal only
};
// Executor: each queue has a fence (DE.Fence.Graphics / DE.Fence.Compute) with monotonically increasing values.
// A sync point = Signal(fence[signalQueue], v) after the producer's batch, Wait(fence[signalQueue], v) on waitQueue
// before the consumer's batch. All waits are GPU-side; the CPU never blocks on them.
```

- **Queue-legal states:** compute lists can't touch RT/DS states, so a resource produced on graphics as RT and read on compute is kept in a common-compatible layout (enhanced barriers, D13). The compiler moves such barriers to the queue that can issue them. The unit tests cover this.
- **Frame-level ordering:** both queues signal at frame end. The next frame's graphics queue waits on compute's last value before touching the transient pool (§3.8).
- **First candidates (based on what exists today [C]):**

| # | Pass | Today | Async plan | Overlaps with |
|---|---|---|---|---|
| 1 | **GPU particle simulate** | Compute, on the direct list inside `simulateAndDraw` ([GpuParticleSystem.cpp L1239](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Particles/GpuParticleSystem.cpp#L1239), [L1354](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Particles/GpuParticleSystem.cpp#L1354)) | Split into `ParticleSim` (async, `PreFrame`) and `ParticleDraw` (graphics, `Transparent`). Depth collision reads *last* frame's depth (history) so the sim doesn't wait for this frame's G-buffer **[I] check the current collision source** | Shadows + G-buffer |
| 2 | **GTAO** | Pixel shader, half res + temporal ([GtaoPipeline.cpp](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/GtaoPipeline.cpp)) | Port to a compute shader (`cs_6_6`), async after the G-buffer. Shadows already run after the G-buffer from Phase 7 (D12), so GTAO overlaps shadow-map rasterisation | Directional + local shadows |
| 3 | **Auto-exposure meter** | Pixel reduction chain + 1×1 readback ([AutoExposure.cpp](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/AutoExposure.cpp)) | Port to a compute histogram, async after the last HDR write; bloom and tonemap use the *previous* frame's exposure as today | Bloom, TAA, motion blur, tonemap |
| 4 | **Shadow-view GPU cull** | Compute on the shadow list ([InstanceCull.cpp L454-L460](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/InstanceCull.cpp#L454-L460), [FoliagePipeline.cpp L1453-L1469](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/FoliagePipeline.cpp#L1453-L1469)) | Async while the camera G-buffer draws; the shadow passes (now after the G-buffer, D12) wait on it | G-buffer |
| later | SSR trace (port to compute), particle sort | Pixel / none | After 1–4 are measured | Lighting |

  **Light culling isn't a candidate:** local lights are CPU-gathered and drawn as volumes ([LocalLightGather.h L54-L84](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/LocalLightGather.h#L54-L84)). Tiled or clustered culling was explicitly ruled out by the user-locked decision L1 in [DESIGN-local-lights.md L129](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/DESIGN-local-lights.md#L129). If that's reopened later, a compute light-binning pass would be a natural async candidate.
- **Success criterion:** PIX shows real overlap, and the GPU frame time drops on the fixed-camera captures. **[I]** On Turing the gains from async compute are usually modest (a few percent), and they depend on there being idle units during shadow rasterisation. A pass that doesn't gain goes back to the graphics queue with a one-line change (`b.queue(...)`).

### 3.12 Debug and visualisation

- `dumpDot()` / `dumpJson()` on demand (dev console or `-rgdump`) show passes, resources, versions, culled passes, **queues and sync points**, and barriers on edges.
- **Graph Inspector** in `Dark.ImGui` (Editor and Sandbox dev tools):
  - pass list with GPU timings per queue;
  - resource lifetime and aliasing chart;
  - a D3D12MA budget bar;
  - a "show resource" picker that replaces the hand-written debug tiles ([SandboxFrame.cpp L1488-L1580](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Sandbox/SandboxFrame.cpp#L1488-L1580), [EditorRender3D.cpp L1074-L1180](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Editor/EditorRender3D.cpp#L1074-L1180)).
- An automatic `GpuScope` per pass. Timestamp queries are per queue; compute timestamps need a query heap that is legal on compute lists.
- Toggles (kept with D8's debug toggles):
  - `rg.validate`, `rg.noCull`, `rg.fullBarriers` and `rg.noAliasing` for checking and bisecting;
  - `rg.asyncCompute` to switch async compute on and off (there's no legacy-barrier toggle, D13).
- The inspector also shows the frame's resolved `FrameSetup` (disabled passes and which hook disabled them).
- A **GBV switch** (`DE_D3D12_GBV=1` → `ID3D12Debug1::SetEnableGPUBasedValidation`). Agility SDK debug-layer messages and DRED (auto-breadcrumbs + page fault) are on in Debug builds.

### 3.13 What to skip at first (solo-dev cut)

1. Split barriers (`D3D12_BARRIER_SYNC_SPLIT`; a Phase 12 option).
2. Per-subresource tracking, except the bloom mips and the local-shadow array.
3. Multiple views per frame (only needed for bake tools; there's no Editor viewport, D3).
4. SM 6.6 bindless (only the ring is built; indexing comes later).
5. A PSO library or cache beyond the DXC shader cache (§3.15).
6. Data-defined passes; quality presets (D8).

### 3.14 Forward-path removal (D2)

Done as **Phase 1**, before any graph code, so later phases wrap one path:

1. Delete the `-forward` flag, `ScenePath`, `applyDeferredScenePath`, `liveSkyPass`, `SkyPass::ForwardFirst`, `MeshPass::ForwardUnorm`, `SkinnedMeshPass::Forward`, `TerrainPass::ForwardUnorm` and their PSOs.
2. Delete `BasicMeshInstanced.hlsl`, `SkinnedMeshForwardInstanced.hlsl` and `Terrain.hlsl`, and the forward batch paths in `InstancedMeshPipeline`, `SkinnedInstanceBuffer` and `DrawList`.
3. Delete `sandboxForwardPasses`/`editorForwardPasses`, the Sandbox forward-opaque block and sky-first draw, and the Editor forward branches.
4. Make scene-target creation failure a hard init error.
5. Collapse the `hasGBuffer()`/`hasSceneBuffers()` guards to the init check.
6. Remove the `ENCODE_SRGB` variants from 3D shaders after checking each pipeline's target format (§1.12).
7. Delete and edit the tests listed in §1.12.
8. Update `DESIGN-deferred-renderer.md` (forward is gone) and the `README`/CLI help.

**Kept:** the `ForwardTransparent` mesh and skinned PSOs (`BasicMesh.hlsl`, `SkinnedMesh.hlsl`), water, particles, clouds and decal volumes, all blending into HDR after deferred lighting, plus the 2D swap-chain path.

### 3.15 Platform: Agility SDK, DXC, D3D12MA (D4, D5, D9, D10, D13)

**Agility SDK (Phase 2), Windows 11 only (D9):**
- **OS check:** at startup, `RtlGetVersion` must report build ≥ 22000 (Windows 11), or DE6 exits with a clear message. No Windows 10 code paths, test runs or 1909/2004 runtime-version branches.
- Fetch the NuGet package in CMake the same way `cmake/WinPixEventRuntime.cmake` does.
- Export `D3D12SDKVersion`/`D3D12SDKPath` from **each exe** (Sandbox, Editor, Sandbox2D, VisualDebugger, UnitTests, and later the per-library test exes from layering Phase 7) via one shared `AgilityExports.cpp` source added by `de_add_exe`, and copy `D3D12Core.dll` (+ `d3d12SDKLayers.dll` in Debug) to `bin/D3D12/`.
- Create the device at **FL 12_1 minimum**. Fill a `RhiCaps` struct (shader model, enhanced barriers, Resource Binding Tier, Resource Heap Tier, `WaveOps`, timestamp support on compute, UMD driver version) and log it at startup together with the loaded Agility SDK version.
- **One startup gate (D5, D9, D13)**, checked in this order with one clear message box + log line + non-zero exit, and never a degraded mode:
  1. Windows 11.
  2. The Agility runtime actually loaded (not the OS fallback).
  3. FL 12_1.
  4. SM ≥ 6.6.
  5. Resource Binding Tier 3.
  6. `EnhancedBarriersSupported`.
  7. The driver ≥ the pinned vendor minimum **[I]**.
- **Unit tests and WARP:** tests that create WARP devices skip the driver-version check. They require the WARP in the Agility package to report enhanced barriers, or they run the CPU-only graph tests only **[I]: confirm on the pinned SDK**.
- The unit tests that create WARP devices ([e.g. SsrTests.cpp L79](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/UnitTests/Render/SsrTests.cpp#L79)) move to the same `RhiDevice` factory. Agility WARP supports FL 12_1+ **[I]**.

**DXC (Phase 2), building on `/workspace/darkengine6-dxc-migration-plan.md`:**
- Swap the `D3DCompile` backend in `ShaderCompile.cpp` ([L105](https://github.com/DarkenedGit/DarkEngine6/blob/7daebbb8e11c858007977c17cfedc75d66ab007a/Render/ShaderCompile.cpp#L105)) for `IDxcCompiler3`. `IDxcBlob` shares `ID3DBlob`'s IID, so call sites keep their `ComPtr<ID3DBlob>`.
- **Inventory [C]:** 204 `compileShaderFrom*` calls in 48 files (engine + tests), 50 `.hlsl` + 24 `.hlsli` files, and the profile literals `vs_5_0` ×99, `ps_5_0` ×138, `ps_5_1` ×5, `cs_5_0` ×30. Replace them with one profile constant per stage, `6_6` after the A/B step. Plus the embedded `LoadingScreen` shader path and 5 test files using `D3DReflect` (→ `IDxcUtils::CreateReflection`).
- **Steps:**
  1. DXC at SM **6.0** with `-HV 2021`, and an FXC fallback switch (`DE_SHADER_FXC=1`) for one release.
  2. Image A/B against the Phase 0 FXC goldens.
  3. Bump to **6.6**.
  4. PDBs for PIX/Nsight.
  5. Runtime shader cache for Debug/Editor, plus the **Release shader bake** (`de_shaders` target, manifest, `ShaderLibrary`; §3.15.1).
  6. Remove FXC.

  Removing forward first (Phase 1) cuts the permutations that need A/B testing.
- **Risks:**
  - HLSL 2021 semantics: vector ternaries need `select()`, plus stricter implicit conversions. The earlier trial found a CloudVolume ternary that needs a fix.
  - Different codegen means small image differences, so the A/B threshold has to be set per pass.
  - Debug/Editor `bin/` needs `dxcompiler.dll`/`dxil.dll` (~22 MB). Release ships neither (D10), and its DXIL is signed at build time. Unsigned DXIL fails PSO creation, which the Release smoke test catches.
  - Manifest drift: a permutation used in code but missing from the manifest is a Release-only failure. Mitigation: the `ShaderManifestTests` drift test and a Release smoke run of every host in the Phase 2 gate.
  - DXC compiles slower at startup **[I]** (the earlier trial compiled 77 permutations in 2–3 s on one core, so the cache matters more than speed).
  - Root-signature and cbuffer packing should match, but verify it with the existing reflection tests.
  - `ps_5_1` users (5) need a check for descriptor-array syntax.

**D3D12MA (Phase 5 for persistent resources, Phase 9 for the transient pool):**
- Vendor it under `third_party/D3D12MemoryAllocator` (MIT). It builds without exceptions **[I]: confirm with DE6's flags**.
- Create the allocator from the Agility device and adapter. Route `CreateCommittedResource` call sites (58 today) through `GpuAllocator::createTexture/createBuffer` as each owner moves to the graph, not in one sweep.
- Budget queries feed §3.8.
- **Risks:** allocation lifetime now goes through `D3D12MA::Allocation` (`deferRelease` must release the allocation, not just the resource), and leak reports change. Name every allocation (`SetName`) so PIX memory captures stay readable.

#### 3.15.1 Shader build step (D10: Release ships precompiled DXIL)

| Config | Shader source | DXC at runtime | Ships |
|---|---|---|---|
| Debug (Sandbox, Editor, tools) | `content/shaders/*.hlsl` compiled on demand | Yes, `dxcompiler.dll` + `dxil.dll`, with an on-disk cache in `%LOCALAPPDATA%/DarkEngine6/ShaderCache/` keyed by hash(source + includes + defines + profile + DXC version) | n/a |
| **Editor** in any config | Same as Debug, so shader edits show up on the next launch or reload | Yes | n/a (Editor isn't a shipped exe) |
| **Release** (Sandbox/game exes) | `bin/shaders/*.dxil` baked at build time | **No.** No DXC DLLs are copied into a Release `bin/` | Signed DXIL + `shaders.manifest`; PDBs kept on the build machine for PIX |

- **Manifest.** A new checked-in `content/shaders/shaders.manifest` lists every permutation as one line: `file entry stage defines`, about 260 lines **[I]** after D2 and D11 remove variants. Today permutations are built in code (204 compile calls in 48 files, macros made at the call sites), so pipelines switch from `compileShaderFrom*` to `ShaderLibrary::get(ShaderKey{file, entry, stage, defines})`. The key is the same hash the runtime cache uses.
- **Build step.** `cmake/ShaderBake.cmake` adds the custom target `de_shaders`:
  - It fetches the pinned DXC release with the same NuGet/download pattern as `cmake/WinPixEventRuntime.cmake`.
  - It runs `dxc.exe -T <stage>_6_6 -HV 2021 -Qstrip_debug -Fd <pdb>` per manifest line into `${build}/shaders/<config>/`, and uses depfiles (`-MD`) so only changed includes rebuild.
  - Every Release exe depends on it. The existing `cmake/CopyContent.cmake` post-build step copies the `.dxil` files and the manifest next to the exe.
  - The build machine's `dxil.dll` signs the DXIL, so nothing is needed at runtime.
- **Embedded shaders.** The `LoadingScreen` shader (embedded source today) is baked to a generated `LoadingScreenShaders.h` byte array, so it doesn't depend on files at all.
- **Release behaviour.** A missing manifest entry is a hard error naming the key. It's never a silent compile, because DXC isn't present.
- **Drift check.** A unit test (`ShaderManifestTests`) runs in Debug: it compiles every manifest line with runtime DXC, and the pipelines' `ShaderKey`s are collected through a registration table and compared with the manifest. A key missing from the manifest fails the test.
- **Where it fits:** **Phase 2**, step 5, after the SM 6.6 bump and before FXC is removed. The bake comes after 6.6 so the manifest is written once with final profiles.

---

## 4. Phased migration

Every phase ends with **Sandbox, Editor (2D and 3D), Sandbox2D and VisualDebugger building and running**, `UnitTests.exe` green, and the gates below. **Nothing here was built; every gate runs on Travis's Windows 11 machine with the RTX 2080 as the reference GPU.**

**Standard gates (G-std):**
- D3D12 debug layer clean in Debug for a 2-minute scripted run of each host;
- **GBV clean** on a short run;
- **PIX capture compare** of the fixed scene and camera (pass list, barrier count, draw count, queue timeline) against the latest baseline;
- **perf** (GPU frame time and CPU render time, 300-frame average at 1440p) within ±3% unless the phase is meant to change it;
- **image diff** of fixed-camera screenshots (Sandbox deferred, the vision modes, underwater, Editor 3D; TAA reset for determinism) within the per-pass threshold;
- from Phase 2 on, a **Release smoke run** (Sandbox launches from baked DXIL with no DXC DLLs in `bin/`).

| Phase | Work | Size | Exit gate |
|---|---|---|---|
| **0. Baselines + docs** | GBV and DRED switches. `-capture-frame N -camera <preset>` saving PNG + timings (Sandbox, Editor). Record **FXC** baselines: PIX captures, timings, screenshots for Sandbox (normal, underwater, each vision mode, echo, camouflage) and Editor 3D. Make `passSequenceOk` failures visible. Update `DESIGN-deferred-renderer.md` K1/§302 and the 11-10 follow-up (D1). | S | Baselines in `docs/perf/`; GBV findings listed (not necessarily fixed) |
| **1. Delete the forward path** (D2) | Everything in §3.14: flag, `ScenePath`, 4 forward enum values and their PSOs, 3 shaders, 2 pass tables, the host branches, instanced forward batches, `ENCODE_SRGB` 3D variants, always-true guards, 6–9 tests deleted and 4 edited. Scene-target failure becomes a hard error. | M | G-std (deferred images identical to the baseline); `rg -i "SwapChainForward\|cliForward\|ForwardUnorm\|ForwardFirst"` returns nothing; PSO count at startup drops **[I] ~15** |
| **2. Platform: Agility SDK + DXC + caps + shader bake** (D4, D5, D9, D10, D13) | Agility exports in every exe, `D3D12Core.dll` deployment, FL 12_1, `RhiCaps` and the **single startup gate** (Windows 11, Agility loaded, FL 12_1, SM 6.6, Binding Tier 3, enhanced barriers, minimum driver; clear message and exit). DXC backend (SM 6.0 first, `-HV 2021`, FXC fallback switch), fix the compile diffs, A/B against the FXC goldens, then bump to SM 6.6; PDBs. Runtime cache for Debug/Editor. **Release bake:** `shaders.manifest`, `ShaderLibrary`/`ShaderKey`, the `de_shaders` CMake target with `dxc.exe`, baked `LoadingScreen` header, `ShaderManifestTests` drift test. `D3DReflect` tests ported. Remove FXC. | M | G-std with DXC 6.0 and then 6.6; image diff vs FXC within thresholds; PIX shows HLSL source; startup gate tested by faking each failing cap (message text checked); Release `bin/` has no `dxcompiler.dll`/`dxil.dll` and every host runs from baked DXIL |
| **3. Graph core, CPU only, with queues** | `render/RenderGraph.*`: handles, builder, arena thunks; compile = cull, order, **queue assignment, sync points**, lifetimes, barrier records (enhanced sync/access/layout only), alias plan; DOT/JSON dump. No D3D12 calls. Unit tests: cull, versioning, read-before-write, history swap, alias intervals, barrier merge, **cross-queue sync reduction, queue-legal barrier placement**, the forced-graphics fallback. | M | New tests pass; no app change |
| **4. Opaque wrap (graph drives order)** | `FrameComposer` + `RgExecutor` on the main list. Each step in §1.1 becomes an opaque pass (`OwnBarriers`) calling the existing code; the overlays get passes; the **2** remaining tables become 2 feature lists; delete `markPass`/`passSequenceOk`. Camouflage and Echo are still in Render at this point, wrapped like any other pass. | M (smaller than v1: one path) | G-std; PIX pass order identical; image diff ≈ 0 |
| **5. Graph owns targets + resize + history; D3D12MA in** | Vendor D3D12MA and add `GpuAllocator`. `SceneBuffers`, depth, back buffer and the GTAO, SSR, bloom, auto-exposure, underwater, camouflage and echo targets become graph-registered **persistent** resources allocated through D3D12MA. One resize path (one `waitForGpu`); remove the 8 lazy resize waits. `g.history()` for TAA (no Post→History copy), GTAO, SSR. One `invalidateHistory()` + `FrameView::holdTemporal`. | M | G-std; 30-s window-drag stress clean; one GPU drain per resize; D3D12MA stats match the PIX memory view |
| **6. Automatic barriers (enhanced only) + descriptor ring** (D13) | `EnhancedBarrierEmitter`. Convert passes from `OwnBarriers` to declared uses, one feature at a time (post → GTAO/SSR → lighting → G-buffer → water/clouds/particles → shadows → debug tiles), with enhanced barriers at opaque-pass boundaries until each is converted. Delete `transition*`/`bind*` helpers and `m_*State` fields as they lose callers (targets: 37 barrier sites → **0** `ResourceBarrier` calls; 30 state fields → 0 outside rhi). DescriptorRing; converted pipelines read graph views. | **M–L** (was L: no second emitter, no dual-path gates) | G-std; `rg.validate` 0 mismatches; GBV clean at each opaque/converted boundary; `rg -n "ResourceBarrier\("` → 0 hits; PIX barrier count ≤ baseline |
| **7. Engine feature registration + domain moves** (v2 fix E, D12) | `IRenderFeature`, stages, providers, explicit `registerXxxFeatures`. Move Sky, Terrain, Water, Particles, Media and Sprite2D passes into their modules; walk overlay → Gameplay `ai`; `ShadowRecord` → render `ShadowFeature` + `IShadowCasterSource`. **Stage order puts `Shadows` after `GBuffer`** (still on the graphics queue). `SceneRenderer` shrinks to the core-feature bundle. | **L** | G-std (image identical after the shadow reorder, perf ±3%); `layer_check.py`: 0 Render→domain edges |
| **8. Game content to Game.Core** (D7, D11) | (a) `CamouflageFeature`, `EchoFeature` (+ `EchoChirpFile`, shaders) move to `Game.Core`; delete `PassId::Camouflage` and the game `ProfileColor`s. (b) Engine: `FrameSetup`/`EnginePass`/`IFrameSetupHook`, `tryRead` fallbacks for AO/shadow/SSR, the generic history-invalid-on-return rule, `ColorRemap.hlsli` in lighting/local lights/fog/sky/water, and the `SurfaceTag` target with one `DE_GBUFFER_SURFACE_TAG` variant in 7 G-buffer pipelines. (c) Delete the 2 vision variants per G-buffer pipeline, `makeThermalMacros`/`makeUvMacros`, `LightingConstants::thermal`, the 13 setters and the Sandbox vision branches (all 71 vision shader refs, ~200 C++ refs). (d) Game.Core `VisionModesFeature` (setup hook + full-screen heat pass) with `Thermal.hlsli`/`Ultraviolet.hlsli`. | **M** (was M–L) | G-std with image diff for **every vision mode (full strength and mid-fade), echo and camouflage** against the Phase 0 baseline; heat mode's PIX capture shows lighting, shadows, GTAO, SSR and underwater culled; `rg -i "thermal\|ultraviolet\|camouflage\|echo" Render content/shaders` → only `TerrainErosion*` (thermal erosion) |
| **9. Transient aliasing (D3D12MA pool, 8 GB budget)** | Custom D3D12MA pool (Tier 2, one heap class), `CreateAliasingResource` per cached plan, one transient set with the frame-start cross-queue wait, `LAYOUT_UNDEFINED`+`DISCARD` first use, 256 MB start / 64 MB growth / 768 MB cap, budget warning, `rg.noAliasing`. Move the §3.8 candidates; disabled features free memory. | M | G-std + PIX memory capture showing the drop (target **[I]** transient ≤ ~145 MB at 1440p); scripted toggle sweep (bloom/TAA/MB/GTAO/SSR/underwater/each vision mode) image-diffs clean |
| **10. Async compute** (D6, planned) | COMPUTE queue + fence in `RhiDevice`; executor honours sync points. Move in order: (1) GPU particle sim split from draw; (2) GTAO ported to compute, overlapping the shadows that already run after the G-buffer (D12, Phase 7); (3) auto-exposure compute histogram; (4) shadow-view GPU cull. Compute-queue timestamps in the profiler. | **L** | G-std with `rg.asyncCompute` on and off; PIX shows overlap; each moved pass keeps its place only if the 1440p GPU frame time improves, otherwise it goes back to graphics |
| **11. Parallel recording** | Batches per queue, per-batch allocator/list per frame slot, thread-safe constant/upload rings; the shadow worker becomes batch 0. | M | G-std; CPU render time down; no new debug-layer messages |
| **12. Later options (not scheduled)** | Split barriers (`D3D12_BARRIER_SYNC_SPLIT`); SM 6.6 `ResourceDescriptorHeap` bindless over the ring (**L** on its own); SSR trace on compute; multi-view for bake tools. | S–L each | G-std per item |

**Ordering notes.**
- Phase 1 comes first because it shrinks everything after it: 2 feature lists instead of 4, fewer PSOs to port to DXC, and fewer branches to wrap.
- Phase 2 comes before the graph so every later image diff compares DXC against DXC, so enhanced barriers and SM 6.6 are guaranteed (the startup gate) before Phase 3 writes enhanced-only barrier records, and so the Release bake is in place before later phases add or delete permutations (each one is then a manifest edit).
- The shadow reorder (D12) is in Phase 7, separate from async compute, so a reorder regression and an async regression can't hide each other.
- Phase 8 needs Phase 7's stages and `tryRead` but not Phase 9. The heat mode's culled passes then show up as memory freed in Phase 9's toggle sweep.
- Phases 7 and 8 can overlap layering Phase 4 (fix E) and Phase 6 (Game.Core) and should land in the same PR series.
- Phase 10 comes before Phase 11 because the GPU-side gain is what D6 asked for. Parallel recording is CPU-side and can move earlier if CPU time becomes the bottleneck.
- Each phase is a series of small PRs (one feature or one resource per PR in Phases 6–10).

**Estimates [I] (focused solo work, ranges):**

| Phase | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Size | S | M | M | M | M | M | M–L | L | M | M | L | M |
| Weeks | 0.5–1 | 1–1.5 | 2–3 | 1.5–2 | 1–1.5 | 1.5–2 | 2–3 | 3–4 | 1.5–2 | 1.5–2 | 2.5–4 | 1.5–2 |
| Change vs previous v2 | – | – | +0.5 (bake, gate) | same (one barrier view) | – | – | **−1** (enhanced only) | same (+shadow reorder, small) | **−0.5 to −1** (full-screen + flags) | same | same (reorder already done) | – |

- **Milestone A** (Phases 0–5: forward gone, Windows 11/Agility/DXC platform with the Release bake, graph drives the frame, one resize path, history unified): about **7.5–11 weeks**.
- **Milestone B** (Phases 6–9: enhanced-only automatic barriers, clean layering with shadows after the G-buffer, game content out, aliasing): about **8–11 more weeks**.
- **Milestone C** (Phases 10–11): about **4–6 more weeks**.
- **Total: about 19.5–28 weeks** (was 21–30; the exact phase sums were 20.5–29.5).

v1 was about 10–15 weeks to its Phase 6 (aliasing) without async compute. The increase comes from the forward deletion (+1–1.5 weeks, which partly pays for itself in Phases 4 and 6), the platform move with the Release bake (+2–3 weeks), the game-content move (+1.5–2 weeks) and async compute now being mandatory (+2.5–4 weeks). D9–D13 take about 1–1.5 weeks off the previous v2 total: enhanced-only barriers (−1) and full-screen vision modes (−0.5 to −1), against +0.5 for the shader bake and the startup gate. (The previous phase sums were 20.5–29.5, rounded to 21–30.)

---

## 5. Risks

1. **Big host functions.** The opaque wrap still touches ~1,900 lines of frame code after Phase 1 removes the forward branches **[I]**. Mitigation: wrap without changing behaviour, Sandbox first.
2. **Forward removal hides a dependency.** Something may still rely on the swap-chain 3D path (a tool, a test, VisualDebugger, a fallback on a machine where scene targets fail). Mitigation: the hard init error with a clear message, a grep gate, and running every exe in Phase 1.
3. **DXC differences.** HLSL 2021 semantics and codegen changes. Small image differences can hide real bugs. Mitigation: FXC goldens in Phase 0, per-pass thresholds, keep the FXC fallback until the end of Phase 2.
4. **Agility SDK deployment.** A missing `D3D12Core.dll`, a wrong `D3D12SDKPath` or an exe without the exports falls back to the OS runtime. Mitigation: the startup gate checks that the Agility runtime loaded and that enhanced barriers are reported, in **every** config. It exits with a clear message, so the fallback can't go unnoticed.
5. **Enhanced barriers only, with no A/B reference (D13).** A machine with an old driver can't run DE6 (by design, with a clear message), and a driver bug in enhanced barriers has no legacy path to compare against. Mitigation: the pinned minimum driver plus a block list, `rg.fullBarriers` as the bisect fallback, GBV on every Phase 6 conversion, and PIX barrier views. **[I]** The legacy↔enhanced interop at opaque-pass boundaries during Phases 4–6 is the riskiest stretch, so it gets GBV per boundary.
6. **Barrier regressions** during Phase 6 (over-barriering costs perf; missing barriers corrupt only on some hardware). Mitigation: `rg.validate`, GBV, `rg.fullBarriers`.
7. **Aliasing corruption** (uninitialised placed memory, a wrong first-use clear, cross-queue lifetime mistakes). Mitigation: `rg.noAliasing`, the toggle-sweep image diff, cross-queue lifetime unit tests.
8. **Async compute brings little gain on Turing and adds cross-queue sync bugs** (deadlocks if a wait has no matching signal; races if the sync points are wrong). Mitigation: sync points computed and unit-tested from Phase 3, every pass reversible to graphics, the success test is a measured GPU-time drop.
9. **GTAO and auto-exposure compute ports change the image.** Mitigation: image-diff thresholds and side-by-side debug views during the port.
10. **Vision modes as full-screen passes (D11) may not match today's look exactly.** Thermal moves from a per-surface write to a full-screen pass using SurfaceTag, emissive and metallic, so the per-pipeline heat classes must come back through the tag LUT. UV stays in lighting as a generic albedo remap. Sky and clouds in heat mode are recoloured by depth. Mitigation: mid-fade and full-strength image diffs per mode against the Phase 0 baseline, and the tag override for any surface that needs its own class.
11. **Frame-setup misuse.** A hook can disable a pass whose output something else reads without a fallback, or flip settings every frame and defeat the plan cache. Mitigation: `rg.validate` errors on a read of a disabled pass's output without `tryRead`, the plan-cache hit rate in the inspector, and a one-time warning if the `disabled` mask changes more than N times a second.
12. **Release shader manifest drift (D10).** A permutation used in code but missing from the manifest only fails in Release. Mitigation: `ShaderManifestTests` in Debug, plus the Release smoke run in G-std from Phase 2.
13. **Shadow reorder (D12).** No G-buffer shader reads shadows **[C]**, but the CPU side (cull results, the shadow worker list, cascade fitting from scene bounds) has to be ready before the shadow passes, wherever they sit. Mitigation: Phase 7 does the reorder alone on the graphics queue with an image-identical gate.
14. **D3D12MA lifetime.** Allocations must be released through the deferred-release queue together with their resources. Mitigation: wrap both in one RAII type, and use the D3D12MA leak report at shutdown in Debug.
15. **8 GB budget.** It's fine for render targets (<8% at 4K **[I]**), but `rg.noAliasing` at 4K plus large texture sets could approach the budget. Mitigation: budget warning, the 768 MB pool cap.
16. **CPU cost** if the plan cache misses every frame (e.g. a feature changing a desc each frame). Mitigation: plan-cache hit rate in the inspector.
17. **No validation here.** Everything is a static read; all gates are on Travis's machine.

---

## 6. Open questions for Travis

None. The five follow-up questions were answered on Nov 20 and are recorded as D9–D13. The only open item is a value Travis pins during Phase 2: the minimum NVIDIA driver version for the startup gate (§3.7).

---

## 7. Appendix: counts (all at `7daebbb`, `rg` over the source dirs)

| Metric | Count |
|---|---|
| `ResourceBarrier(` call sites | 37 (Render 34, Particles 2, Terrain 1) |
| `D3D12_RESOURCE_STATES` state fields in headers | 30 in 14 headers |
| `transitionX(` mentions | 157 |
| `renderer.bind*/clear*/transition*` calls | 69 |
| `CreateCommittedResource(` / placed resources | 58 / 0 |
| `CreateDescriptorHeap(` / shader-visible heap files / `SetDescriptorHeaps(` | 71 / 29 / 42 |
| Graphics / compute PSO creates; root signatures; `*Pipeline.h` | 71 / 14; 44; 30 |
| `Dispatch(` sites | 14 (Render 12, Particles 2) |
| Command queues | DIRECT + COPY (+1 temporary DIRECT at init in LocalShadowSystem) |
| `waitForGpu()` call sites outside Renderer.cpp | 27 (13 can fire mid-frame) |
| `GpuScope` uses | 70 |
| `PassId`s / pass tables | 20 / 4 (18, 11, 19, 9 nodes) |
| Render → domain includes | Assets 36, Terrain 11, ECS 11, Sky 7, Animation 5, Media 3, Water 2, AI 2, Weapons 1, Character 1 |
| Domain → Render includes | Terrain 28, Water 24, Particles 23, Ui 10, Media 3, Sprite 3, Sky 1 |
| Forward-path refs (`ScenePath::`/`scenePath()`) | 54 in 18 files |
| Forward-only shaders | 3 (`BasicMeshInstanced`, `SkinnedMeshForwardInstanced`, `Terrain`; 353 lines) |
| Shader compile calls / profile literals | 204 calls in 48 files; vs_5_0 ×99, ps_5_0 ×138, ps_5_1 ×5, cs_5_0 ×30 |
| Vision-mode refs (thermal/ultraviolet) | ~200 in ~30 engine C++ files (216 in 32 matched `thermal\|ultraviolet` under the engine dirs; 15 of them are thermal erosion in `TerrainErosionPipeline.*`); **71 in 13 shaders** (79 matched; 8 are `TerrainErosion.hlsl`). After Phase 8: 0 in engine shaders |
| Vision shader refs by file | `Thermal.hlsli` 13, `Water` 11, `Sky` 9, `DeferredLighting` 8, `TerrainGBuffer` 6, `GrassGBuffer` 4, `FoliageGBuffer` 4, `BasicMeshGBuffer`(+Instanced) 3+3, `SkinnedMeshGBuffer`(+Instanced) 3+3, `UvMap.hlsli` 2, `LocalLightVolume` 2 |
| Vision PSO variants in G-buffer pipelines | 7 pipelines × 2 (thermal, UV) → 7 × 1 generic SurfaceTag variant after Phase 8 |
| Shader reads of shadow maps in G-buffer shaders | 0 (D12 reorder is safe on the GPU side) |

