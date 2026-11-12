# DarkEngine6 exhaustive review — main 14945a3 (Nov 9, 2026)

Status as of 2026-11-10: [2026-11-10-review-followup.md](2026-11-10-review-followup.md). This review is left as written.

Read-only review; nothing built, run, or posted.

---

# DarkEngine6 review, part A: threading, memory/resources, modernization

**Commit:** `14945a3f4c00387c8d0bf150eee8406659d78f86` (main)
**Method:** read-only. I downloaded the source tarball for that SHA to `/workspace/de6/DarkEngine6-14945a3f…/` so I could grep it. There was no git clone, nothing was compiled or run, and nothing was posted to GitHub.
**Scope:** threading, memory/resources, and how each system compares with current engine practice. Architecture, duplication and config are covered by the other reviewer.
**Link base:** `https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/`, shown below as `B/`.

---

## 0. Summary

- **The threading design is conservative and mostly correct.** There is no general-purpose job system. The concurrency that exists is:
  - 2 I/O threads and 1 "render lane" thread (`Core/JobSystem`)
  - a SAPI speech thread
  - a Media Foundation movie decode thread
  - a save/load file thread
  - an Editor foliage-spawn thread
  - XAudio2's own audio thread

  Each has a single owner, joins before its dependents die, and uses mutex/condvar hand-off with generation tokens. Application member order is correct: `m_jobs` is destroyed before `m_renderer`, `m_assets` and `m_world`. I found **no confirmed P1 threading bug.**
- **The off-thread shadow recorder is dead code in production.** `setShadowWorker()` has no non-test caller, so `g_shadowWorker` is always `false`. All D3D12 recording is single-threaded on the main thread. If the worker is ever switched on, it shares `DrawList`, `SkinnedInstanceBuffer` and `FoliagePipeline` with the main thread at the same time (see R1).
- **The biggest memory problem is that nothing is ever evicted.** `AssetManager::collectGarbage()` and `GpuResourceCache::collectUnused()` have no production callers. Every texture, model, material, sound clip and synthesized dialogue line ever loaded stays in RAM and VRAM until the process exits. In a long Editor session that opens several scenes, this grows without limit.
- **The main modernization gaps:**
  - Shaders are compiled at runtime with FXC (SM5.x), with no cache and no hot reload.
  - Each material and each terrain tile has its own shader-visible descriptor heap, which means many `SetDescriptorHeaps` calls per frame.
  - About 65 root signatures and 87 PSOs are created one by one, with no pipeline library.
  - Barriers are legacy and tracked by hand. `PassTable` checks pass order but does not compute barriers, so there is no render graph.
  - There is no async compute.
  - Every resource is a committed resource. Each upload creates its own command allocator and list.
  - There is no BC texture compression or asset cooking. WIC decode and CPU mip generation run at load time.
  - The ECS uses `unordered_map` sparse sets.
  - There is no CI workflow in the repo.

### Coverage

| Status | Files |
|---|---|
| **Read fully** | `Core/JobSystem.{h,cpp}`, `Core/MainThread.h`, `Core/MemoryTracker.cpp`, `Play/FrameRecord.cpp`, `Play/ShadowRecord.cpp` (738 lines), `Audio/SpeechSynth.{h,cpp}`, `Audio/AudioSystem.h`, `Audio/StreamQueue.h`, `ECS/World.{h,cpp}`, `ECS/Entity.h`, `ECS/Component.h`, `Assets/AssetManager.h`, `Assets/ImageCache.{h,cpp}`, `Render/PackedSrvHeap.{h,cpp}`, `Core/Application.h` (members) |
| **Read in depth (the relevant sections)** | `Render/Renderer.cpp` (init, frame/fence, shadow list, deferred release, copy submits; about 600 of 1771 lines), `Audio/AudioSystem.cpp` (about 70%), `Media/MoviePlayer.cpp` (lifecycle, worker, audio/voice/clock; about 30%), `Save/SaveSystem.cpp` (worker plus `service`), `Scene/SceneFile.cpp` (async load table), `Sandbox/SandboxBoot.cpp` (boot job lifetimes; about 40%), `Editor/EditorTerrain.cpp` (foliage worker, terrain teardown), `Core/Log.cpp` (write/capture), `Debug/DebugServer.cpp` (log ring), `Particles/GpuParticleSystem.cpp` (`prepare`/pool lifetime), `Terrain/TerrainGrid.cpp` and `TerrainMaterial.cpp` (heap packing), `Render/GpuMaterial.cpp`, `Play/DialogueSpeech.cpp`, `Network/Reliability.cpp` and `TcpSocket.cpp` (parse and connect paths), `Sandbox/SandboxApp.cpp` and `Editor/EditorRender*.cpp` (frame/render sections), `Render/ShaderCompile.cpp` |
| **Grep census across the whole tree, excluding `third_party`** | threads, mutexes and atomics; `Map`/`Unmap`; `CreateDescriptorHeap`/`SetDescriptorHeaps`; `waitForGpu`; PSO and root-signature creation; `CreateCommittedResource`; barrier APIs; queue types; compiler (FXC/DXC); profiling (PIX/NVTX/Tracy); texture formats (BC/DDS); entity churn; per-frame containers |
| **Not reviewed for this half** | Most gameplay code (AI/Combat/Weapons/Character), most render pipelines' shader logic, Terrain streaming internals, Physics wrapper internals, `UnitTests/` |

By volume, I read about 10–12k of the roughly 230k first-party lines (63k of those are tests) in detail. Everything else is from targeted greps. The threading surface is fully enumerated: every `std::thread` and `CreateThread` site was found and checked.

---

## 1. Confirmed bugs

### P2-1: No production eviction, so RAM and VRAM grow without limit
- **Trigger:** In the Editor, open scene A, then B, then C, and so on. In Sandbox, each new dialogue line spoken through synth adds a clip.
- **Mechanism:**
  - `AssetManager` holds a strong `shared_ptr` for every asset in `m_assets` ([B/Assets/AssetManager.h#L106-L111](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Assets/AssetManager.h#L106-L111)).
  - The only release path is `collectGarbage()` ([B/Assets/AssetManager.cpp#L315-L335](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Assets/AssetManager.cpp#L315-L335)), and it is called only from tests. The GPU side has the same problem: `GpuResourceCache::collectUnused()` ([B/Render/GpuResourceCache.cpp#L513](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/GpuResourceCache.cpp#L513)) has no callers, and `clear()` runs only at Renderer teardown.
  - Dialogue speech interns a clip under a key that contains the full line text ([B/Play/DialogueSpeech.cpp#L136](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/DialogueSpeech.cpp#L136), [#L179](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/DialogueSpeech.cpp#L179)). Every voice and line combination is kept forever. Placeholder text such as `{fact}`, if added later, would make this much worse.
- **Fix:** Add a scene-boundary GC step: `assets().collectGarbage(); renderer().gpuResources().collectUnused();`, run after `waitForGpu`, or with GPU objects retired through `deferRelease`. The ECS plan doc already describes this ordering. Give synth clips an LRU, or keep them outside `AssetManager` with a byte budget. Before turning GC on, check that `AssetPinTable` and host `AssetRef` members pin everything live. The design doc lists `emplace` replacing a component as unpinned.

### P2-2 (carried over, still present): movie frames always ask for typed-UAV support for BGRA
- [B/Render/MovieFrameTexture.cpp#L226-L235](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/MovieFrameTexture.cpp#L226-L235) still creates every frame texture as BGRA with `ALLOW_UNORDERED_ACCESS`, including software-decoded frames. The fix is unchanged from the last review: drop the flag on the software path, and check `D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE` before using the GPU conversion path.

### P3-1: GPU particle pool changes stall the whole GPU in the middle of a frame
- **Trigger:** Any submitted GPU particle pool whose emitter leaves the accepted set, which happens when its entity is destroyed or the slot budget pushes it out. It also happens when a pool is recreated because its generation, capacity or secondary-emitter layout changed.
- **Mechanism:** `prepare()` sets `needWait` and calls `m_renderer->waitForGpu()` before it releases or recreates the buffers ([B/Particles/GpuParticleSystem.cpp#L752-L783](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Particles/GpuParticleSystem.cpp#L752-L783)). That drains the queue, which forces the CPU to wait for the GPU to finish all queued frames. Because acceptance depends on the budget, emitters can move in and out of it during normal play and cause repeated hitches. `isAccepted` is also a linear scan inside a loop over pools (O(N²)), and `accepted`/`seen` are new vectors every frame.
- **Fix:** Retire pool buffers with `renderer.deferRelease()`, which already exists and covers kFrameCount+1 frames, instead of draining. Pool and reuse buffers by capacity class. Use a flat set or sorted vector for `accepted`.

### P3-2: `m_prevWorldByEntity` keeps growing and gives wrong velocity on a new scene's first frame
- **Mechanism:** The Sandbox keeps `std::unordered_map<EntityID, Matrix4f>` ([B/Sandbox/SandboxApp.h#L324](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.h#L324)), writes it every frame ([B/Sandbox/SandboxApp.cpp#L3209-L3219](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L3209-L3219)), and never erases or clears it. Its keys are packed id plus generation, so each recycled slot adds a new key. After a scene reload or reuse of the same id, the first frame reads the old entity's matrix ([#L2863-L2864](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L2863-L2864)). That produces a one-frame velocity spike, which shows up as TAA or motion-blur smearing.
- **Fix:** Store `prevWorld` in a component, as the Editor already does with `ag->prevWorld`, or clear the map on scene teardown and drop entries for entities that were not seen this frame.

### P3-3: The log capture callback can run after `removeCapture` returns, and `g_logTarget` is read without synchronization
- **Mechanism:** `Log::write` copies the capture function pointers under the mutex, unlocks, then calls them ([B/Core/Log.cpp#L266-L301](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Core/Log.cpp#L266-L301)). `removeCapture` ([#L240-L251](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Core/Log.cpp#L240-L251)) does not wait for callbacks already in progress. `DebugServer::onLogCapture` reads the plain global `g_logTarget` ([B/Debug/DebugServer.cpp#L20](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Debug/DebugServer.cpp#L20), [#L98-L114](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Debug/DebugServer.cpp#L98-L114), [#L133-L137](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Debug/DebugServer.cpp#L133-L137)). The worker threads that log (speech, movie, save, foliage) race with stop/listen writes to it. Today the object outlives the workers because of member order, so this is a formal data race rather than a crash.
- **Fix:** Make `g_logTarget` a `std::atomic<DebugServer*>`. Also have `removeCapture` wait for in-progress callbacks (an epoch counter or a shared/exclusive lock around the call).

### P3-4: The foliage phase text is written by the worker and read by the UI without synchronization
- **Mechanism:** `onFoliageProgress` calls `snprintf` into `m_foliagePhase` on the worker thread ([B/Editor/EditorTerrain.cpp#L1364](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorTerrain.cpp#L1364)). ImGui reads it on the main thread ([#L1826](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorTerrain.cpp#L1826)). This can show torn text and is formally undefined behaviour. All the other worker-to-main fields here are correctly ordered by the `m_foliageDone` atomic.
- **Fix:** Publish a phase enum or index through `std::atomic<int>`, or guard the text with a small mutex.

### P3-5: Per-frame heap churn and locks on the render path
- **Per-frame containers:** Each frame builds three `std::unordered_set<EntityID>` and a `std::vector` ([B/Sandbox/SandboxApp.cpp#L2838-L2841](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L2838-L2841); the same in [B/Editor/EditorRender3D.cpp#L275-L277](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorRender3D.cpp#L275-L277)). That is one node allocation per inserted entity per frame, and `count()` is called once per caster per cascade.
- **Asset lookups:** Every `assets().getAs<T>(id)` on the render path, such as in the 4-cascade shadow loop ([B/Play/ShadowRecord.cpp collectModelCasters](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/ShadowRecord.cpp#L193-L240)), takes `AssetManager::m_mutex`, hashes, does an atomic refcount increment and decrement, and a `dynamic_pointer_cast` ([B/Assets/AssetManager.cpp#L297-L303](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Assets/AssetManager.cpp#L297-L303)). There are 18 `getAs` sites in `SandboxApp.cpp` alone.
- **Dialogue key:** `DialogueSpeech::acquire` rebuilds a `std::format` key that includes the full line text on every frame while it waits for synthesis.
- **Fix:**
  - Make the accepted sets persistent members: either `clear()` with reserved capacity, or a dense bitset indexed by entity index.
  - Resolve `AssetRef<Model>` once per frame into a frame-local table, or cache a raw `Model*` in the component and pin it with `AssetPinTable`.
  - Cache the dialogue key for each pending request.

---

## 2. Risks and latent issues (not reachable today, or needs a runtime check)

**R1. The off-thread shadow worker is unsafe if it is enabled. It is currently dead code.**
- **Current state:** `g_shadowWorker` is always false ([B/Play/FrameRecord.cpp#L16](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/FrameRecord.cpp#L16)), and `setShadowWorker` has no production caller.
- **What happens if it is turned on:**
  - `kickFramePasses` starts `shadowJobFn` on the render-lane thread ([B/Play/ShadowRecord.cpp#L562](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/ShadowRecord.cpp#L562)). The main thread then immediately calls `dispatchCameraSide`, which runs `drawList->dispatchCull` and `skinned->dispatchPreparedCull` on the same objects ([#L564](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/ShadowRecord.cpp#L564)), while the worker runs `dispatchCull` and `recordDepth` on them. `CullResourceSet::Depth/Camera` separates the GPU buffers, but I did not verify that the CPU-side state in `DrawList`, `SkinnedInstanceBuffer` and `FoliagePipeline` is disjoint. Examples are the "args/index state the main list continues", the upload cursors and the descriptor ring positions.
  - The worker writes `Renderer::m_shadowListOpen/Recorded` through `noteShadowListReset` and `adoptClosedShadowList` ([#L319](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/ShadowRecord.cpp#L319)), and `job->kicked` as plain fields. The main thread reads `g_job.kicked` at [#L456](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/ShadowRecord.cpp#L456) while a job may still be running. This is safe only because of the per-frame join.
- **Fix before enabling:**
  - Freeze shared objects into immutable per-frame packets before the kick.
  - Make `kicked` atomic, or move it under the job-system mutex.
  - Add a TSan-style stress test. MSVC has no TSan, but Clang-cl with TSan on a headless test is possible.
  - Or delete the worker until a real job graph exists.

**R2. Audio clip memory may be freed while XAudio2 still holds the buffer.**
- **What happens:**
  - `stop()` calls `voice->Stop(0)`, which leaves the buffer queued, then `slot.clip.reset()` ([B/Audio/AudioSystem.cpp#L527-L541](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Audio/AudioSystem.cpp#L527-L541)). `stopClipVoices` ([#L543-L560](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Audio/AudioSystem.cpp#L543-L560)) does the same.
  - `play()` does `Stop(0)`, then `FlushSourceBuffers()`, then `slot.clip = clip` ([#L440-L442](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Audio/AudioSystem.cpp#L440-L442)). XAudio2 applies Stop and Flush on its own audio thread at the next processing pass, and it can read the old `pAudioData` until then.
- **Why it doesn't happen today:** Every clip is also held by `AssetManager`, which never frees (P2-1). It becomes a real use-after-free read once GC is enabled, or for the non-interned `AssetRef{made}` fallback in `DialogueSpeech` ([B/Play/DialogueSpeech.cpp#L179](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/DialogueSpeech.cpp#L179)).
- **Fix:** Keep the clip ref in a "retiring" list until `OnBufferEnd`, using an `IXAudio2VoiceCallback`. Alternatively, keep it until `GetState().BuffersQueued == 0` on a later `tick()`.

**R3. Repacking a shader-visible descriptor heap releases the old heap immediately.**
- **What happens:** `packFromCpuHandles` calls `out.heap.Reset()` before it creates the new heap ([B/Render/PackedSrvHeap.cpp#L18-L22](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/PackedSrvHeap.cpp#L18-L22)). `TerrainMaterial::packSrvHeap` does `m_heap = PackedSrvHeap{}` ([B/Terrain/TerrainMaterial.cpp#L150-L153](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Terrain/TerrainMaterial.cpp#L150-L153)). A heap referenced by a list still in flight is invalid, as the comment at `TerrainGrid.h#L192` (#921) already notes for tiles.
- **Why it doesn't happen today:** The live callers run after `waitForGpu`, through `removeEditorTerrain` or at boot. The mid-session callers, `EditorApp::uploadTerrainSplatGpu` and `rebuildTerrainGpuFromSurface` (which call `rebindResidentHeaps`), have **no callers**, so they are dead code that will crash if revived.
- **Also:** The debug toggles `setLayerSamplingRaw`/`setAlbedoSamplingRaw` and `copyShadow` overwrite descriptors in heaps the GPU may be reading. That is a race, but it is debug-only.
- **Fix:** Route the old heaps through `deferRelease`. Better, move to one global shader-visible heap with a free-list allocator and per-frame retirement (see M2).

**R4. ECS handle reuse (ABA) after 4096 recycles of a slot.** The 20/12-bit split ([B/ECS/Entity.h#L10-L12](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/ECS/Entity.h#L10-L12)) plus a LIFO free list ([B/ECS/World.cpp#L13-L17](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/ECS/World.cpp#L13-L17)) means the same slot is reused immediately. After 4096 reuses, a stale `Entity` held elsewhere (hunter target, loot target, net map) passes `alive()` again. Entity churn is low today: projectiles are not entities, and there are about 59 `createEntity` sites, mostly spawn and load. **Fix:** Use a FIFO free list with a minimum free-queue depth, or a 64-bit id (32/32).

**R5. Mutating a pool during `each()`.** `World::each` passes `comps[i]` by reference ([B/ECS/World.h#L175-L187](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/ECS/World.h#L175-L187)). Emplacing the same T inside the callback reallocates the vector and leaves the reference dangling. Removing inside the callback swaps the last element into `i`, so that element is skipped. Nothing checks for this, not even an assert. I did not audit every `each` lambda. **Fix:** Add a debug "iterating" counter on each pool that asserts on insert/remove, plus deferred destroy and add queues.

**R6. A finished save or load is applied even if the world changed in between.** `SaveSystem::service` applies a completed async Load to whatever `World` is passed in, after re-checking `allowLoad()` ([B/Save/SaveSystem.cpp#L1032-L1060](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Save/SaveSystem.cpp#L1032-L1060)). If the Editor presses Stop or the scene changes while the read is in flight, the result goes into the new world, unless `allowLoad` rejects it. I didn't trace `allowLoad`'s exact rules. **Fix:** Stamp each job with a session or world generation and drop stale results.

**R7. The speech thread keeps working on requests that are no longer wanted.**
- `stop()` cannot interrupt a synthesis already running (`Speak` is blocking), so shutdown can wait for a full line.
- After the 4 s timeout in `DialogueSpeech`, the job stays queued, synthesizes anyway, and its result is thrown away ([B/Audio/SpeechSynth.cpp#L186-L203](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Audio/SpeechSynth.cpp#L186-L203)).
- `running()` is false until SAPI initializes, so the first line after `start()` gets "subtitles only".
- **Fix:** Add a cancel-by-id, a latest-request-wins queue, and use `SPF_ASYNC` with `Speak(NULL, SPF_PURGEBEFORESPEAK)` to abort.

**R8. One condition variable serves every waiter.** `JobSystem` uses a single condvar for the I/O workers, the render worker and `joinRender`, so every enqueue and completion calls `notify_all()` and wakes them all. That costs a little at today's load and won't scale. The `JobStatus::Budget/NotReady`, `JobStream`, `reads` and `generation` fields are not used by the scheduler (`pollSceneLoad` has a Budget re-queue path that can never be reached).

**R9. Log I/O happens under the lock.** Each line calls `fputs` and `fflush` on the file and on stdout, and then `OutputDebugStringA`, all with the log mutex held ([B/Core/Log.cpp#L272-L291](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Core/Log.cpp#L272-L291)). Verbose categories on worker threads will block the main thread. There is no size cap or log rotation. **Fix:** Use a lock-free MPSC ring and a logger thread. Flush only on Warn and above, or on a timer.

**Checked and found correct:**
- `JobSystem` shutdown, join and drain.
- The `SceneLoad` record lifetime: a `shared_ptr` is re-acquired from the table, with superseded and generation checks.
- Sandbox boot: I/O jobs point into `m_boot`, and `~SandboxApp` calls `jobs().shutdown()` before `delete m_boot`. Every slice waits on `*Finished` atomics before moving on.
- The MoviePlayer worker: commands go through the mutex, `m_audio` and `m_stream` are read under the lock, the voice is handed over with epochs, and join happens on close.
- The `AudioSystem` stream ring: the write index only advances while `BuffersQueued < 8`, and all stream calls are under `m_streamMutex`.
- `ImageCache` single-flight: no lock-order inversion with `AssetManager::m_mutex`.
- Editor foliage thread: `terrainBrushesLocked()` is checked at start, every writer of the working maps goes through `removeEditorTerrain()`, which cancels and joins first, and the thread joins in the destructor and on shutdown.
- The `SaveSystem` worker join.
- Renderer fence accounting: mid-frame `waitForGpu` keeps fence values monotonic, as in the MS sample.
- Per-frame Map rings are double-buffered by `frameIndex` (PathChase, skeleton lines).
- Network is single-threaded and non-blocking (`select` with a 0 timeout), and reliable-message lengths are bounds-checked.
- `deferRelease` works by frame count (kFrameCount+1 `beginFrame`s). That is correct for one direct queue plus copy-queue waits inserted into the graphics queue. It errs on the safe side when frames are skipped.

---

## 3. Memory and resources: other observations

- **Upload path:** `submitBufferCopies` and `submitTextureCopies` create a new `ID3D12CommandAllocator` and `ID3D12GraphicsCommandList` for every upload, and call `ExecuteCommandLists` once per upload ([B/Render/Renderer.cpp#L995-L1107](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/Renderer.cpp#L995-L1107)). Texture uploads run on the graphics queue. During loads this causes a lot of driver allocation and submission overhead. **Fix:** Use a pooled upload ring (one large persistently mapped UPLOAD buffer per frame, with linear sub-allocation), batch copies into one copy-queue list per frame, and fence the copy queue.
- **Allocation strategy:** There are 154 `CreateCommittedResource` sites, no placed resources or heaps, and no D3D12MA. Every small buffer becomes its own allocation of at least 64 KB. **Fix:** Adopt D3D12 Memory Allocator (MIT) or write a simple buddy/TLSF allocator over placed heaps.
- **Runtime GPU stalls:** These run during gameplay, not just teardown:
  - `waitForGpu` in GpuParticleSystem (P3-1).
  - `CamouflagePipeline` on resize, called mid-frame from the draw call ([B/Render/CamouflagePipeline.cpp#L283-L284](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/CamouflagePipeline.cpp#L283-L284)).
  - `EchoPipeline` snapshot resize.
  - `UnderwaterPipeline`.

  Each resize-driven one should use `deferRelease` instead.
- **ECS storage:** `ComponentPool<T>` is a dense vector with an `unordered_map<EntityID,uint32_t>` sparse index ([B/ECS/World.h#L109-L112](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/ECS/World.h#L109-L112)). Every `get`/`has` is a hash lookup. Joins such as `each<ModelComponent>` followed by `get<TransformComponent>` hash once per entity per component. **Fix:** Use a paged sparse array indexed by `entityIndex(id)` with a generation check (EnTT-style). That is a drop-in change with large cache wins.
- **Ownership:** Hosts use raw-pointer structs (`PlaySession`, `MovieHost`, `ShadowCasterHost`) filled every frame. That works, but the lifetimes are only documented. Debug builds should assert that the host is still alive, for example with a generation token per host.

---

## 4. Systems vs modern practice

Ratings: **A** = at modern indie/AA level · **B** = solid but dated · **C** = functional but well behind · **D** = missing.

| System | Rating | Today | Upgrade (in priority order) |
|---|---|---|---|
| **Job system / scheduler** | C | 2 I/O threads, 1 render-lane thread, global mutex and condvar, fixed-capacity deques, no parallel-for, no dependencies. Gameplay, AI, animation, culling and particle CPU work is all single-threaded on main. | 1) A work-stealing pool sized to the core count, with `parallel_for` and job counters (Naughty Dog "fibers" or the enkiTS/Taskflow style). 2) Per-frame task graph: anim pose eval, AI perception, CPU cull, skinning prep and particle CPU sim in parallel over immutable snapshots. 3) Keep `World` main-thread-only, with typed write-back buffers (as the ECS plan already describes). |
| **Command recording** | C | One main list, plus an optional shadow list recorded on main. The worker exists but is off and unsafe (R1). | Per-pass command lists from a per-thread allocator pool. Record passes in parallel once passes stop sharing mutable CPU state (needs a render graph). |
| **Frame graph / barriers** | C | `PassTable` checks pass order. Barriers are hand-written legacy `ResourceBarrier` calls with per-pipeline tracked states. There are no aliasing or transient resources. | A render graph with declared reads and writes that computes barriers and aliases transient targets (the G-buffer, SSAO, SSR, bloom chain). On Agility SDK ≥ 1.7, use **Enhanced Barriers** (`D3D12_BARRIER_*`, currently 0 uses). Batch barriers per pass. |
| **Descriptor management** | C | About 31 shader-visible heaps, one per material and terrain tile ([B/Render/PackedSrvHeap.cpp](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/PackedSrvHeap.cpp)), and `SetDescriptorHeaps` once per material bind. On some hardware a heap switch drains the pipeline. | One global shader-visible CBV/SRV/UAV heap (about 1M descriptors) plus a sampler heap. Use persistent slots with free-list allocation and per-frame ring segments for transient tables. Then go **bindless**: SM 6.6 `ResourceDescriptorHeap[]` with materials as indices in a structured buffer. That also removes most of the 65 root signatures. |
| **Shaders** | C | Runtime **FXC** `D3DCompile` (SM 5.0/5.1) from `content/shaders` at every launch, about 119 call sites, with no cache, no hot reload and no reflection. | Move to **DXC** (SM 6.6+: wave ops, bindless, 16-bit, mesh/amplification). Compile offline in CMake to DXIL blobs, with hashed permutations. Add a dev hot-reload watcher (`ReadDirectoryChangesW`) that recompiles and swaps PSOs. Generate root-signature and reflection data. |
| **PSO caching** | C | 87 PSO creation sites and 65 root signatures, all built at init. No `ID3D12PipelineLibrary` and no cached blobs. | Store PSOs in an `ID3D12PipelineLibrary1` on disk, keyed by a desc hash. Create PSOs in parallel at load. Use a single shared root signature with bindless. |
| **GPU culling / indirect** | B | Compute frustum cull plus `ExecuteIndirect` for static and skinned instances, with separate depth/camera resource sets. Good. | Two-phase Hi-Z occlusion. Move LOD selection to the GPU. Meshlets / mesh shaders for dense foliage later. |
| **Shadows** | B | CSM, a shared shadow recorder, local-light shadows, cached foliage depth. | Cache static casters per cascade (scroll and re-render only dirty parts). Virtual shadow maps are the high end. Make the per-cascade CPU caster loop parallel. |
| **PBR / IBL / post** | B | Deferred hybrid, PBR maps, IBL bake, GTAO, SSR, TAA, motion blur, bloom, auto-exposure, reverse-Z, fog/clouds/volumes (from the DESIGN docs and file list). A strong feature set. | Async compute for GTAO, SSR, bloom and the IBL bake (currently 0 compute queues). FSR or another upscaler. HDR10 output. A compute-based blue-noise and TAA history pipeline. |
| **Memory / allocation** | C | All committed resources, a new allocator per upload, frame-count deferral, no residency management, no eviction (P2-1). | D3D12MA, an upload ring, a residency budget with `IDXGIAdapter3::QueryVideoMemoryInfo`, and LRU eviction for streamed textures and tiles. |
| **Textures / asset pipeline** | C | WIC decode of source images at runtime, CPU mip generation (`TextureMips`), RGBA8 throughout (0 BC formats), runtime glTF parsing (cgltf), runtime shader compilation. | An offline cooker: BC7 for albedo, BC5 for normals, BC4/BC1 for masks, pre-baked mips, DDS/KTX2 containers. Content hashing (xxHash) for incremental builds. A single packed archive for shipping. glTF cooked to a runtime mesh format. Expect roughly 4–8× VRAM savings and much faster loads. |
| **Streaming** | B− | Terrain tiles stream with retirement (#921), and boot loads are sliced across I/O jobs. | General async texture and mesh streaming with priorities. DirectStorage, including GPU decompression, once assets are cooked. |
| **ECS** | C+ | Sparse-set pools on `unordered_map`. Single-component `each` only. Main-thread asserts and generation handles (12-bit). | Paged sparse arrays. Multi-component views with the smallest pool driving iteration. Command buffers for deferred structural changes. Assert on iteration invalidation. 32/32 ids. Or adopt EnTT or flecs (both MIT). |
| **Physics** | B | Box3D, main thread, `workerCount=1`, fixed 1/60 s with substeps, kinematic movers. | Hook Box3D's task callbacks to the new job pool. Keep ECS writes on main from event buffers (already the design). |
| **Animation** | B− | Clip sampler, anim graph JSON, GPU skinning ring. All on main. | Parallel pose eval jobs, compressed clips (ACL, MIT), and anim LOD by distance. |
| **Audio** | B− | XAudio2 with 24 fixed voices, X3DAudio, an occlusion low-pass, one stream voice. No mixer graph or buses. | Submix buses (SFX, music, dialogue, UI) with ducking, voice priority and virtualization, a callback-based buffer lifetime (R2), streaming for music, and optional reverb zones. FMOD or Wwise are alternatives. |
| **AI / nav** | B− | HSM graphs, perception, a grid/terrain pathfinder on main. | Recast/Detour navmesh (zlib), async path queries on the job pool, and crowd avoidance. |
| **Networking** | B− | Custom UDP reliability layer, non-blocking, single-threaded, a heap vector per message. | Pooled packet buffers, a snapshot delta with a bit-packer, and encryption and auth if it ever goes online. |
| **Save system** | B | JSON, versioned, atomic write on a worker, size cap, persist registration. | Binary option (zstd-compressed). Stamp results with a world generation (R6). Run schema migrations. |
| **UI** | B | Dear ImGui for tools, custom HUD headers. | Fine for tools. A retained-mode game UI (RmlUi, or Noesis if commercial) would be needed for shipping menus and localization. |
| **Profiling / telemetry** | C+ | GPU timestamp profiler (`Render/Profile`), NVTX, a few PIX events, `MemoryTracker`, `DebugServer` over TCP. | Integrate **Tracy** (BSD) with CPU zones, GPU zones (D3D12 context), lock contention and memory. Add PIX markers on every pass. DRED and auto-breadcrumbs for device-removed (currently 0). In debug, `ID3D12InfoQueue` break-on-error plus GPU-based validation as an option. |
| **Tests / CI** | B− / D | About 2,150 tests across 63k lines, which is very good, with `/W4 /WX`. There is **no CI workflow** in the repo (no `.github/`). | A GitHub Actions Windows runner: build (Debug and Release), run the unit tests headless, an optional WARP smoke render, and clang-tidy. Add a sanitizer job, using ASan on MSVC for the CPU-only tests. |

---

## 5. Suggested order of work

1. **P2-1 eviction.** Add the scene-boundary GC, and keep synth clips out of the permanent cache.
2. **P2-2 movie UAV flag.**
3. **P3-1 particle `waitForGpu`** → `deferRelease`. **P3-2** prevWorld map. **P3-5** per-frame sets and asset lookups.
4. **M:** Global descriptor heap plus an upload ring and D3D12MA. Together these remove a whole class of lifetime bugs (R3) and most driver overhead.
5. **M:** DXC plus offline shader build, a PSO library and hot reload.
6. **M:** A real job pool and per-frame task graph. Delete or rewrite the dormant shadow worker (R1) on top of it.
7. **M:** An asset cooker with BC compression and hashing.
8. **M:** A render graph with enhanced barriers, then async compute.
9. **Infra:** Tracy, DRED, and a GitHub Actions CI.

---

# DarkEngine6 review B: architecture, repeated code, Editor/Sandbox duplication, hard-coded values

**Commit:** `main` @ `14945a3f4c00387c8d0bf150eee8406659d78f86` (Windows, D3D12, MIT, no exceptions. The root `CMakeLists.txt:4` and AGENTS.md both say **C++23**, not C++20.)
**Method:** I fetched raw files from the commit with curl and analysed them with scripts. Nothing was cloned, built or run, and nothing was posted to GitHub.
**Scope:** This is my half of the review. Threading, memory and the modern-quality pass are covered by the other reviewer.

Links use `B = https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86`.

---

## 0. Coverage

| | Count |
|---|---|
| Repo blobs (all) | 1,954 (608 under `third_party/`, excluded) |
| Non-third-party text files downloaded (.h/.cpp/.inl/.hlsl/.json/.md/.cmake/.txt) | 1,247 |
| Engine and app C++ files scanned by every automated pass (include graph, clone detector, function-size scan, constant/literal greps) | **all** ~770 .h/.cpp/.inl outside `UnitTests/` (167k lines). Tests: 250 .cpp, 63k lines (only scanned for structure) |
| Read fully by hand | `CMakeLists.txt`, `cmake/DarkEngineTargets.cmake`, `UnitTests/CMakeLists.txt`, `AGENTS.md`, `Play/PlaySession.h` (PlayHost/PlaySession), `Save/SaveSystem.h` (SaveHost), `Scene/SceneSpawn.h`, `Gameplay/Item.cpp`, `Sandbox/PathChase.h`, `content/*.input.json`, `content/entities/player.entity.input.json`, `content/upgrades.json`, `content/ai/hunter.attacks.json`, `content/items/throwables.json` |
| Function bodies read side by side | Both `fillPlayHost` functions, both `fillDialogueHost`, both `spawnSpawnerUnit`, `stockPlayerKit`/`stockHunter`, `SandboxApp::stepPlayPhysics` vs `EditorApp::simulatePhysics`, `EditorApp::bakePlayWalkability` vs `PathChase::bake/init`, both `onPlayQuestNote`, `installSaveHost`, `hunterJumpDef`, `attachPlayerSounds`, `attachCameraSounds`, `spawnHybridLocalLights`, `applyNetRole`, plus the scene-instantiate paths in `SandboxBoot.cpp` and `EditorSceneFile.cpp` |
| Skimmed by targeted excerpts | ~60 further spots, cited below |
| Not reviewed | HLSL bodies, all of `Render/` internals beyond the clone, constant and include scans, `Terrain/`, `Water/`, `Media/` logic, and the test contents |

---

## 1. Summary

1. **The module layering only exists on paper.** Every engine folder except `Math` is in **one strongly connected include cycle of 24 modules**, and there are 35 direct two-way module pairs. `Core` includes `Render`, `Play`, `Audio`, `Network` and `Debug`. `Assets` includes `AI` and `Play`. `Render` includes `Weapons` and `Character`. The CMake split into `DarkFoundation`, `DarkAssets`, `DarkRender`, `DarkNet` and `DarkGameplay` works only because "unresolved symbols … resolve at final link" ([`cmake/DarkEngineTargets.cmake:236`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/cmake/DarkEngineTargets.cmake#L229-L237)). None of those libraries can link or be tested on its own.
2. **The app shells are still much bigger than the AGENTS.md "wrapper" rule allows.** Sandbox is 8,945 lines, Editor 15,243 and Sandbox2D 1,721, against 12,270 in `Play/`. AGENTS.md says "Anything both apps do is implemented once, in the engine." I found **44 methods implemented in both EditorApp and SandboxApp**. 17 of them are byte-identical and about 600 lines are ≥50 % similar. On top of that, about 300 lines of the frame-render orchestration are verbatim copies between `SandboxApp::onRender` (1,161 lines) and `EditorApp::renderScene3D` (966 lines). Some copies have already drifted and now behave differently (§3.3).
3. **Repeated code across the engine.** My clone detector (runs of ≥10 non-trivial normalized lines) found **413 clone runs covering about 5.3k lines**. The biggest families are:
   - D3D12 boilerplate: a local `FailedHr`/`CreateUploadBuffer` in **52 files**, and copy-pasted root signatures and PSOs in the post pipelines.
   - JSON read helpers (`jsonToFloat`/`readFloat`/`readBool`…) re-implemented in **14 files**, even though `Core/Json.h` already provides `jsonFloatOr` and friends.
   - 27 independent JSON loaders, each with its own `kMaxJsonBytes`.
4. **Hard-coded data.** The item catalog, status-effect catalog, sound-cue banks, hunter jump attack, door, alarm, interaction and crow tuning, the terrain layer set, demo lantern positions and the security-camera beam are all compiled in. Render-quality structs (`ShadowSettings`, `GtaoSettings`, `SsrSettings`, `TaaSettings`…) already exist with good defaults, but **nothing loads them from a file**. There is no hot reload anywhere and no JSON schemas. On the plus side, input bindings are already well data-driven, and so are upgrades, throwables, awareness, attack patterns and skills.
5. **No confirmed crash or correctness bugs** came out of this pass. There are **several confirmed behavioural divergences** between Editor Play and Sandbox, listed in §6. Each of them is the AGENTS.md "one implementation" rule being broken.

---

## 2. Architecture

### 2.1 Module dependency graph (measured from `#include "X/..."` edges)

Notable upward or sideways edges, which a layered engine shouldn't have:

| Edge | Example | Why it's a problem |
|---|---|---|
| Core → Render / Audio / Network / Debug / Input / Assets | [`Core/Application.h:5-13`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Core/Application.h#L5-L13) includes `Render/Renderer.h`, `Render/LoadingScreen.h`, `Audio/AudioSystem.h`, `Network/NetworkSystem.h`, `Debug/DebugServer.h`, `Input/Input.h` | `Application` is an "engine runtime" class, but it sits in `Core`. |
| Core → Play | [`Core/Application.cpp:6`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Core/Application.cpp#L6) includes `Play/FrameRecord.h` | The lowest layer depends on the highest gameplay layer. |
| Core → Audio / Particles / ECS | [`Core/EntityPins.h:3-6`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Core/EntityPins.h#L3-L6), [`Core/EntityPins.cpp:5`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Core/EntityPins.cpp#L5) | `EntityPins` knows about sound and particle components. |
| ECS → Assets | [`ECS/Components.h:7`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/ECS/Components.h#L7) | The core component header carries asset handles. |
| Collision (Foundation lib) → Render | [`Collision/SweptCollision.h:14`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Collision/SweptCollision.h#L14), [`Collision/StaticCollision.h:16`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Collision/StaticCollision.h#L16) include `Render/Frustum3f.h`, and `Frustum3f.cpp` is compiled into **DarkRender** | DarkFoundation needs a DarkRender symbol. `Frustum3f` belongs in `Math/`. |
| Assets → AI / Play / Audio | [`Assets/AssetManager.cpp:2-8`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Assets/AssetManager.cpp#L2-L8) includes `AI/HsmGraphJson.h`, `Play/DialogueGraphJson.h`, `Audio/SoundClip.h` | The asset manager hard-codes every asset type. It needs a loader registry (`registerLoader(ext, fn)`). |
| Render → Weapons / Character / Media / Terrain / Water / Sky | [`Render/DecalTypes.h:5`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/DecalTypes.h#L5) (`WeaponKind` in decal types), [`Render/CrosshairHud.h:7`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/CrosshairHud.h#L7), [`Render/DecalPool.cpp:4,361`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/DecalPool.cpp#L361-L369) (reads `HealthComponent`), [`Render/LoadingScreen.cpp:7-9`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/LoadingScreen.cpp#L7-L9) (MoviePlayer) | The renderer makes gameplay queries. Feature renderers (water, terrain, foliage) should either own their pipelines or depend through POD descriptors. |
| AI → Play / Save | [`AI/AiSystem.cpp:22,27-29`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/AI/AiSystem.cpp#L22-L29) includes `Play/Crow.h`, `Play/PlayMotor.h`, `Play/Spawner.h`, `Save/PersistentId.h` | |
| Scene → Play / AI / Gameplay / Save | [`Scene/SceneTypes.h:3,7`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Scene/SceneTypes.h#L3-L7) includes `AI/Awareness.h` and `Play/Spawner.h` in the *data-only* scene types header | Scene data types should be POD and leaf. |
| Gameplay → Play / Save / AI / Physics | [`Gameplay/PowerSystem.cpp:6-14`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Gameplay/PowerSystem.cpp#L6-L14), [`Gameplay/QuestRuntime.cpp:10`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Gameplay/QuestRuntime.cpp#L10) | |
| Character → Play / Render / Gameplay | `Character/*View.cpp` include `Render/GpuUpload.h` and `Play/Interaction.h` | "View" (presentation) code lives in the simulation module. |
| Sky / Media / Particles → Scene | [`Sky/CloudVolume.cpp:6`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sky/CloudVolume.cpp#L6), [`Media/MovieCue.cpp:7`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Media/MovieCue.cpp#L7), [`Particles/ParticleScene.h:5`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Particles/ParticleScene.h#L5) | Runtime systems depend on the scene *file* format. |
| Terrain → Audio | [`Terrain/LeafRustle.h:4-5`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Terrain/LeafRustle.h#L4-L5) | |

Tarjan SCC result: {AI, Animation, Assets, Audio, Character, Collision, Combat, Core, Debug, ECS, Gameplay, Input, Media, Network, Particles, Physics, Play, Render, Save, Scene, Sky, Terrain, Water, Weapons} form **one cycle**. Only `Math` is a leaf.

### 2.2 CMake target split

- The layer libraries are cosmetic. `DarkAssets` needs Animation, `DarkGameplay` needs Character, Audio and Particles, and `DarkRender` needs Terrain and Media. The project works around this by moving `GrassPipeline.cpp` and `LoadingScreen.cpp` out of DarkRender ([`DarkEngineTargets.cmake:150-168`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/cmake/DarkEngineTargets.cmake#L150-L168)) and relying on final-link resolution.
- `Ui/` sources are **compiled three times**: into Sandbox ([`CMakeLists.txt:274`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/CMakeLists.txt#L274)), into Editor and into VisualDebugger ([`DarkEngineTargets.cmake:195-196`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/cmake/DarkEngineTargets.cmake#L195-L196)). An `imgui_editor`-linked `DarkUi` static lib would compile them once and give the shared HUD a real home.
- `imgui_demo.cpp` is compiled into every ImGui exe ([`CMakeLists.txt:260`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/CMakeLists.txt#L260)).
- **`Sandbox/CMakeLists.txt` is stale and unused.** The root never calls `add_subdirectory(Sandbox)`, and the file lists only 3 of the 6 Sandbox .cpp files. Delete it.
- App logic is compiled straight into the exes, so **nothing in Editor or Sandbox can be unit-tested** (`UnitTests` links only `DarkEngine`). That is a strong argument for moving shared app logic into `Play/`.
- There are cross-TU forward declarations without a header, for example [`Sandbox/SandboxBoot.cpp:47-50`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxBoot.cpp#L47-L50) re-declares `tryCreateTerrainFromContent`, `makeGrassPatch`, `attachCameraSounds` and `attachSandboxCubeModel`, which are defined in SandboxApp.cpp. A signature change there gives a link error instead of a compile error.

### 2.3 God files and functions

The largest files (non-test, lines): `Sandbox/SandboxApp.cpp` 4,121 · `AI/AiSystem.cpp` 2,463 · `Sandbox/DevToolsPanel.cpp` 2,059 · `Scene/SceneFile.cpp` 2,032 · `Render/FoliagePipeline.cpp` 2,029 · `Editor/EditorTerrain.cpp` 1,957 · `Media/MoviePlayer.cpp` 1,951 · `Render/Renderer.cpp` 1,771 · `Sandbox2D/Sandbox2DApp.cpp` 1,512 · `Play/PlaySession.cpp` 1,504 · `Sandbox/SandboxBoot.cpp` 1,505 · `Render/TerrainGrid.cpp` 1,487 · `Render/MeshGen.cpp` 1,477 · `Particles/GpuParticleSystem.cpp` 1,398 · `Render/DrawList.cpp` 1,352 · `Editor/EditorPlay.cpp` 1,288.

The largest single functions:

| Lines | Function |
|---|---|
| **1,161** | [`SandboxApp::onRender`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L2759) |
| **966** | [`EditorApp::renderScene3D`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorRender3D.cpp#L234) |
| 882 | [`SandboxApp::drawDevTools`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/DevToolsPanel.cpp#L1178) |
| 697 | [`loadSceneFromJson`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Scene/SceneFile.cpp#L932) |
| 605 | `SandboxApp::advanceBootSlice` |
| 563 | `SandboxApp::advanceSpawnBatch` |
| 534 | `EditorApp::drawEditorUi` |
| 408 | `EditorApp::drawInspector3D` |
| 395 | `SandboxApp::handleRuntimeCommands` |
| 357 | `saveSceneToJson` |
| 309 | `EditorApp::handleEditorCommands` |

The class headers:
- `SandboxApp.h` has about 171 data members. `EditorApp.h` has about 198.
- Each pulls in **240+ project headers transitively**, including Renderer, AiSystem, PhysicsWorld, AudioSystem and SceneRenderer.
- `Render/Renderer.h` is reached by 80 .cpp files.

**Leftover refactor shims:** `SandboxApp` still holds **38 reference members aliasing `PlayRuntime` fields** ([`SandboxApp.h:267-449`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.h#L267-L449), bound in the constructor at [`SandboxApp.cpp:410-450`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L410-L450)). **16 of them are never used** except in their own initializer: `m_blockRecoil`, `m_comboCount`, `m_comboStep`, `m_comboWindow`, `m_faceLeft`, `m_faceYaw`, `m_hitStop`, `m_playerSubmerged`, `m_playerWet`, `m_shake`, `m_shakeTime`, `m_shieldBump`, `m_swimDiveEdge`, `m_swimStandEdge`, `m_swingCharged`, `m_swingLock`. The other 22 should become `m_play.runtime().x`.

### 2.4 Globals and singletons

- **Process-wide catalogs:**
  - [`throwableCatalog()`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Gameplay/ThrowableCatalog.cpp#L352-L355) and [`skillCatalog()`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Character/SkillCatalog.cpp#L859-L862) are function-static singletons.
  - [`Gameplay/Item.cpp:8-22`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Gameplay/Item.cpp#L8-L22) is a static array.
  - [`Combat/StatusCatalog.cpp:7`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Combat/StatusCatalog.cpp#L7) is a `constexpr` table.

  This mix makes it impossible to run two worlds with different data, or to hot-reload. Use one `GameData` registry owned by the app and passed in hosts.
- Hidden global frame state lives in [`Render/Profile.cpp:82-95`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/Profile.cpp#L82-L95), [`Play/FrameRecord.cpp:16-32`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/FrameRecord.cpp#L16-L32) and [`Play/ShadowRecord.cpp:135`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/ShadowRecord.cpp#L135). The threading side is the other reviewer's area. Architecturally, these should be members of a `FrameContext`.

### 2.5 Interfaces and coupling: the "Host" pattern

There are 10 `*Host` structs: `PlayHost`, `DialogueHost`, `MovieHost`, `SaveHost`, `SceneSpawnHost`, `ShadowCasterHost`, `ImpactFxHost`, `CharacterHost`, `ThrowableFlightHost` and `FacilityHost`. Between them they hold **61 `fn(void* user, …)` callback pointers**. The pattern (non-owning pointers, copy and move deleted) is fine and matches AGENTS.md. But:

- `PlayHost` ([`Play/PlaySession.h:129-197`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/PlaySession.h#L129-L197)) has about 50 fields and 14 callbacks. It is rebuilt from scratch on every `pull()`, which happens 13 times per frame in `PlaySession.cpp`. Each app re-derives the same values in about 60 lines. Most fields are services that never change after init (world, renderer, assets, audio, physics, AI, terrain…). **Split it into `PlayServices`** (bound once) **and `PlayFrameInput`** (UI gates, camera, sun, per frame). Then replace the 14 C callbacks with one small abstract `IPlayEvents` interface with default no-op methods. A missing hook then becomes a visible design choice rather than a silent `nullptr`. That is exactly how the Editor ended up without the `onRespawn`, blood and muzzle hooks.
- `PlayRuntime` ([`PlaySession.h:71`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/PlaySession.h#L71), 46 fields) holds the **local player's** state on the session, not on an entity. That locks Play to one local player and keeps it out of networking. Over time it should become a `PlayerControllerComponent` on the possessed body.

### 2.6 ECS consistency

- The ECS is used widely (79 `*Component` structs, 89 persist references), but a lot of simulation state lives in app members instead of components or engine systems:
  - The vision modes (`m_nightSightOn`, `m_infrared*`, `m_ultraviolet*`, `m_echo*`): 96 references in `SandboxApp.cpp`, and none at all in the Editor.
  - `m_questBanners` and `m_defenseText`.
  - Muzzle timers.
  - `m_playCubes`/`m_playSpheres` collision lists (Editor) vs `PathChase::m_cubes` (Sandbox).
- The AI owner differs between apps. The Editor owns `AiSystem m_ai` directly. Sandbox wraps it in `Sandbox/PathChase` ([`PathChase.h`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/PathChase.h)), which also bakes walkability, draws debug paths and owns a demo "walker". Baking and obstacle gathering should be one engine function (§3.3 #4).
- Systems are ticked by hand-ordered calls inside each app's `onUpdate` and `onRender`. There is no system schedule, so the order of Play, AI, physics, environment, water and dialogue is decided twice ([`EditorUi.cpp:1084-1204`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorUi.cpp#L1084-L1204) vs [`SandboxApp.cpp:2664-2757`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L2664-L2757)).

### 2.7 Testability

The unit tests are strong for engine pieces: 250 files, including `Play/` (PlaySession, Dialogue*, Facility, Spawner…). But because of the include cycles every test links the whole engine, and none of the Editor or Sandbox logic (scene spawn dispatch, save hooks, HUD state, net role) can be tested. Moving the items in §3 into `Play/` makes them testable for free.

---

## 3. Editor vs Sandbox (vs Sandbox2D) duplication

### 3.1 Method-level similarity

I normalised `EditorApp`/`SandboxApp` names and compared with difflib.

**Identical (100 %), 17 methods:** Each of these is a 4–9 line forwarder or host filler that a shared base or helper would remove.

| Method | Editor | Sandbox |
|---|---|---|
| `spawnWeaponImpact`, `spawnLivingBloodDecal`, `spawnHunterFootmark`, `spawnFootmark`, `spawnDeathBloodDecal`, `deferredDecals`, `isStepCue` | [`EditorPlay.cpp:888-921`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPlay.cpp#L888-L921) | [`SandboxApp.cpp:2078-2111`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L2078-L2111) |
| `saveSkip`, `saveNetRole`, `saveDestroy`, `pullPhysicsVelocities`, `pushPhysicsProgress` | [`EditorSave.cpp:81-196`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorSave.cpp#L81-L196) | [`SandboxSave.cpp:84-250`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxSave.cpp#L84-L250) |
| `onPlaySpawnUnit`, `onPlayLoot`, `onPlayGroundContact`, `onPlayDefenseText` | [`EditorPlay.cpp:448-491`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPlay.cpp#L448-L491) | [`SandboxApp.cpp:644-675`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L644-L675) |
| `fillMovieHost` | [`EditorPlay.cpp:330-338`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPlay.cpp#L330-L338) | [`SandboxApp.cpp:457-465`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L457-L465) |

**Near-identical or substantial overlap (50–94 %):**

| Similarity | Method | Editor | Sandbox / other |
|---|---|---|---|
| 94 % | `saveEndLoad` | [`EditorSave.cpp:148-155`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorSave.cpp#L148-L155) | [`SandboxSave.cpp:154-162`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxSave.cpp#L154-L162) |
| 92 % | **`drawUpgrades`** (54 lines) | [`EditorPlay.cpp:789-842`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPlay.cpp#L789-L842) | [`DevToolsPanel.cpp:332-384`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/DevToolsPanel.cpp#L332-L384) |
| 91 % | `onPlayQuestNote` | [`EditorPlay.cpp:422-446`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPlay.cpp#L422-L446) | [`SandboxApp.cpp:613-642`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L613-L642). This one has drifted: only Sandbox colours the banners, and `QuestBanner` is declared twice ([`SandboxApp.h:298`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.h#L298), [`EditorApp.h:567`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorApp.h#L567)). |
| 89 % | `installSaveHost` | [`EditorSave.cpp:57-74`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorSave.cpp#L57-L74) | [`SandboxSave.cpp:34-51`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxSave.cpp#L34-L51) |
| 86 % | `ensurePlayerStealth` | [`EditorPlay.cpp:1000-1006`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPlay.cpp#L1000-L1006) | [`SandboxApp.cpp:1439-1445`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L1439-L1445) |
| 85 % | **`drawLoot`** | [`EditorPlay.cpp:1245-1288`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPlay.cpp#L1245-L1288) | [`DevToolsPanel.cpp:985-1026`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/DevToolsPanel.cpp#L985-L1026) |
| 83 % | `impactHost` | [`EditorPlay.cpp:875-886`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPlay.cpp#L875-L886) | [`SandboxApp.cpp:2065-2076`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L2065-L2076) |
| 78 % | `saveCanSave` | [`EditorSave.cpp:91-122`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorSave.cpp#L91-L122) | [`SandboxSave.cpp:94-128`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxSave.cpp#L94-L128) |
| 64 % | `saveBeforeCapture` | [`EditorSave.cpp:204-243`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorSave.cpp#L204-L243) | [`SandboxSave.cpp:258-301`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxSave.cpp#L258-L301) |
| 65 % | `fillDialogueHost` | [`EditorPlay.cpp:340-356`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPlay.cpp#L340-L356) | [`SandboxApp.cpp:467-483`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L467-L483) |
| 62 % | `tickLeaves` | [`EditorTerrain.cpp:749-783`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorTerrain.cpp#L749-L783) | [`SandboxApp.cpp:2626-2643`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L2626-L2643) |
| 61 % | `saveBeginLoad` | [`EditorSave.cpp:124-146`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorSave.cpp#L124-L146) | [`SandboxSave.cpp:130-152`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxSave.cpp#L130-L152) |
| 57 % | `saveAfterApply` | [`EditorSave.cpp:245-290`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorSave.cpp#L245-L290) | [`SandboxSave.cpp:303-355`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxSave.cpp#L303-L355) |
| 55 % | **`fillPlayHost`** | [`EditorPlay.cpp:358-420`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPlay.cpp#L358-L420) | [`SandboxApp.cpp:485-550`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L485-L550) |
| 54 % | `drawPrimitiveShadowCasters` | [`EditorRender3D.cpp:71-102`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorRender3D.cpp#L71-L102) | [`SandboxApp.cpp:1377-1411`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L1377-L1411) |
| 51 % | **`spawnSpawnerUnit`** | [`EditorPlay.cpp:960-998`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPlay.cpp#L960-L998) | [`SandboxApp.cpp:1568-1603`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L1568-L1603) |

**Frame rendering:** `SandboxApp::onRender` and `EditorApp::renderScene3D` share **112 distinct calls** and about **296 verbatim lines** in runs of 3 or more. The biggest blocks:
- Shadow-caster host setup: [`EditorRender3D.cpp:441-476`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorRender3D.cpp#L441-L476) ↔ [`SandboxApp.cpp:3009-3045`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L3009-L3045).
- Point-light foliage culling: [`EditorRender3D.cpp:371-398`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorRender3D.cpp#L371-L398) ↔ [`SandboxApp.cpp:2941-2968`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L2941-L2968).
- G-buffer and velocity debug tiles: [`EditorRender3D.cpp:1107-1147`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorRender3D.cpp#L1107-L1147) ↔ [`SandboxApp.cpp:3963-4004`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L3963-L4004).
- Shadow debug tiles: [`EditorRender3D.cpp:1160-1187`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorRender3D.cpp#L1160-L1187) ↔ [`SandboxApp.cpp:4059-4087`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L4059-L4087).
- Model and skinned submission loops: [`EditorRender3D.cpp:1009-1048`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorRender3D.cpp#L1009-L1048) ↔ [`SandboxApp.cpp:3670-3717`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L3670-L3717).

**Sandbox2D ↔ Sandbox:**
- `onNetPeer` is 93 % similar ([`Sandbox2DApp.cpp:1037`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox2D/Sandbox2DApp.cpp#L1037) / [`SandboxApp.cpp:2385`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L2385)).
- `findPawn` is 89 % similar.
- `netRoleName` and `pawnPaletteColor` are copied verbatim ([`Sandbox2DApp.cpp:64-95`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox2D/Sandbox2DApp.cpp#L64-L95) ↔ [`SandboxApp.cpp:336-367`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L336-L367)).
- `applyNetRole` is 59 % and `populateMainMenu` is 65 % similar.
- The Editor has a third net flow in [`EditorNet.cpp`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorNet.cpp) (`onNetSpawn`, `onNetDespawn`, `onNetPeer`).

### 3.2 Feature matrix: implemented in both apps

| Feature | Editor | Sandbox | Status |
|---|---|---|---|
| PlayHost fill and Play callbacks | `EditorPlay.cpp:358-491` | `SandboxApp.cpp:485-675` | Duplicated. The Editor is missing `onMuzzle`, `onBloodBurst`, `onBloodSplat`, `onRespawn`, `cameraEntity` and `muzzle` (confirmed: the Editor fill never sets them). |
| Dialogue host fill | `EditorPlay.cpp:340` | `SandboxApp.cpp:467` | Duplicated, and `uiBlocked` is built differently. |
| Movie host fill | `EditorPlay.cpp:330` | `SandboxApp.cpp:457` | Identical. |
| Spawner units | `EditorPlay.cpp:960` | `SandboxApp.cpp:1568` | Duplicated and diverged (§3.3). |
| Hunter and wolf attach | `attachEditorHunter/Wolf` (`EditorPlay.cpp:160-170`) | `PathChase::spawnListedHunter` + `attachNpcVisual` + `bindEntityPhysics` | Two pipelines for the same actor. |
| Scene object → entity dispatch | `EditorApp::spawnObject` (209 lines, 22 `SceneObjectType` branches, [`EditorSpawn.cpp:222`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorSpawn.cpp#L222)) | `advanceSpawnBatch` (563 lines, [`SandboxBoot.cpp:943`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxBoot.cpp#L943), type branches at 969-983) | Both call the shared `instantiateSceneObjects` and `applySceneActor`, but the per-type dispatch is still written twice. |
| Building fixtures | `EditorSceneFile.cpp:448-457` | `SandboxApp.cpp:677-698` | Same `BuildingFixtureSpawn` fill. |
| Walkability bake and obstacles | `bakePlayWalkability` (`EditorPlay.cpp:267-330`) | `PathChase::bake/init` (`PathChase.cpp:81-113`) | Diverged (§3.3). |
| Conceal volumes | `EditorPlay.cpp:521-524` | `SandboxBoot.cpp:1134-1136` | Duplicated. |
| Guard barks load | [`EditorPlay.cpp:548-555`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPlay.cpp#L548-L555) | [`SandboxBoot.cpp:1155-1161`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxBoot.cpp#L1155-L1161) | Duplicated. |
| Upgrades load | [`EditorPlay.cpp:785`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPlay.cpp#L785) | [`SandboxBoot.cpp:1151`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxBoot.cpp#L1151) | Duplicated. |
| Upgrades UI and loot UI | `EditorPlay.cpp:789 / 1245` | `DevToolsPanel.cpp:332 / 985` | 92 % and 85 % clones. |
| HUD: quest banners, defense text, prompts | `EditorPlay.cpp:1070-1243`, ticked in `EditorUi.cpp:1126-1136` | `DevToolsPanel.cpp:244, 587-690, 928-940` | Duplicated and diverged. Sandbox decrements the defense timer **inside the draw call** (`DevToolsPanel.cpp:932`), and parry is detected by `text[0]=='P'`. |
| Player physics step | `simulatePhysics` ([`EditorPhysics.cpp:337-386`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPhysics.cpp#L337-L386)) | `stepPlayPhysics` ([`SandboxApp.cpp:552-573`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L552-L573)) | Diverged (§3.3). |
| Ground height and contact | [`EditorPhysics.cpp:95-98`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPhysics.cpp#L95-L98) | [`SandboxApp.cpp:2493-2501`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L2493-L2501) | Same `raisedGroundY` call. |
| Save host (10 hooks) | `EditorSave.cpp` (all) | `SandboxSave.cpp` (all) | 5 of 10 identical, the rest 57–94 %. |
| Environment, cloud and water tick | [`EditorUi.cpp:1104-1150`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorUi.cpp#L1104-L1150) | [`SandboxApp.cpp:2704-2737`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L2704-L2737) | Duplicated. |
| Terrain LOD and leaves | `EditorTerrain.cpp:749-811` | `SandboxApp.cpp:2401-2413, 2626-2643` | Duplicated. |
| Shadow casters and frame render | see §3.1 | see §3.1 | About 300 duplicated lines. |
| Net spawn, despawn, peer | `EditorNet.cpp:252-302` | `SandboxApp.cpp:2355-2399` (+ Sandbox2D) | Three copies. |
| Vision modes (night, IR, UV, echo), camouflage | — | `SandboxApp.cpp` (96 refs) | **Sandbox only.** The Editor hard-codes `camouflage=false` ([`EditorPlay.cpp:392`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPlay.cpp#L392)). Per AGENTS.md this is play simulation, so it belongs in Play. |

### 3.3 Divergences (the cost of the duplication)

These are confirmed by reading the code. I did not check them in game.

1. **Spawner hunters can talk only in Sandbox.** Sandbox calls `bindNpcDialogue(hunter, {})` for spawned hunters ([`SandboxApp.cpp:1590-1597`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L1590-L1597)). The Editor's `spawnSpawnerUnit` doesn't ([`EditorPlay.cpp:979-997`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPlay.cpp#L979-L997)).
2. **The player's kinematic body moves differently.** The Editor sweeps it with `moveKinematicTo`, which "is what shoves dynamic cubes and spheres", and restores the motor pose afterwards ([`EditorPhysics.cpp:358-385`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPhysics.cpp#L358-L385)). Sandbox teleports it with `setBodyPose` before stepping and never restores the transform ([`SandboxApp.cpp:556-572`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L556-L572)). Pushing props therefore likely feels different in Sandbox. **Needs an in-game check.**
3. **The Editor Play is missing hooks.** There's no blood burst or splat, no muzzle light, no `cameraEntity` and no respawn reset (compare the two `fillPlayHost` blocks above).
4. **The walk maps differ.** The Editor bakes cubes **and spheres** and skips volume cubes ([`EditorPlay.cpp:277-299`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPlay.cpp#L277-L299)). Sandbox bakes only cubes, and **always adds a 1 m cube at the origin** ([`PathChase.cpp:108-110`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/PathChase.cpp#L108-L110)) for the net demo cube. That cube is spawned in every Idle-role scene ([`SandboxApp.cpp:1317-1318`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L1317-L1318)), not just `level.json`.
5. **Quest banners are colourless in the Editor**, because its `QuestBanner` struct has no `color` field.
6. **The pickup and takedown keys differ by app**: pickup is G in Sandbox and E in the Editor, and takedown exists only in Sandbox ([`player.entity.input.json:23-25`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/content/entities/player.entity.input.json#L23-L25)). The prompts hard-code the letters ([`DevToolsPanel.cpp:619-687`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/DevToolsPanel.cpp#L619-L687), [`EditorPlay.cpp:1199`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPlay.cpp#L1199), [`Play/DialogueHud.h:89`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/DialogueHud.h#L89)), so they'll be wrong after any rebind.

### 3.4 Proposed shared API

These all go in `Play/`. The order is by value.

1. **`PlayServices` + `PlayFrameInput` + `IPlayEvents`** replace `PlayHost`.
   ```cpp
   struct PlayServices { World*; Renderer*; SceneRenderer*; AssetManager*; AssetPinTable*; Audio::AudioSystem*;
                         Physics::PhysicsWorld*; AiSystem*; Terrain::TerrainGrid*; const Terrain::TerrainGround*;
                         GuardVoice*; CrowModels*; Physics::FoliageCollisionSet*; GameData* data; };   // bind once
   struct PlayFrameInput { Entity player, shield, camera, muzzle, session; Camera3D* view;
                           float waterLevel, sunElevation, cloudTime; Vector3f sunDir; PlayUiGates ui; };
   class IPlayEvents { public: virtual ~IPlayEvents() = default;
       virtual void onMuzzle() {} virtual void onBloodBurst(const Vector3f&) {} virtual void onBloodSplat(float,float) {}
       virtual void onRespawn() {} virtual void onQuestNote(const QuestNoteEvent&) {} virtual void onDefense(bool) {}
       virtual void onLoot(Entity) {} virtual bool onUse(const PlayInteractPick&) { return false; } };
   ```
   Blood, muzzle, quest-note and defense handling then move into engine defaults (a `PlayFeedback` component or system), so the apps only override presentation.
2. **`PlayActors` (one spawner and actor factory):** `Entity spawnUnit(PlayServices&, Entity spawner, const SpawnerRequest&)` and `bool attachActor(PlayServices&, Entity, NpcKind)`. This replaces both `spawnSpawnerUnit`s, `attachEditorHunter/Wolf` and `PathChase::spawnListedHunter`. It always does visual, physics, stock and dialogue.
3. **`ScenePlaySetup` (one scene → play pipeline):** a sliced `ScenePlayLoader` with phases (objects, buildings, obstacles, walkability, conceal, quest, barks, upgrades). Sandbox runs it in time slices for the loading screen. The Editor runs it all at once on Play, then `restoreAuthored()` on Stop. This absorbs `bakePlayWalkability`, `PathChase::bake`, the barks and upgrades loads, the conceal gather and the per-type dispatch in `spawnObject`/`advanceSpawnBatch`.
4. **`PlaySaveHost`:** a default `Save::SaveHost` implementation in `Play/` built on `PlayServices`, with virtual overrides only for the Editor-specific respawn and skip. That removes about 250 lines from the two `*Save.cpp` files.
5. **`PlayHudState` + `Play/PlayHud.h`:** banners (with colour), defense text, prompts that read key labels from `ActionMap` (`input.bindingLabel("pickup")`), timers ticked in Play rather than in draw, and upgrade and loot panels. Following the existing `DialogueHud.h` pattern, this is header-only ImGui included by the exes.
6. **`WorldTick`:** environment, cloud, water and leaf ticks, terrain and water LOD, in one ordered function (or a small system schedule).
7. **`FrameComposer`:** a shared 3D frame recorder (shadow casters, foliage culling, G-buffer, forward, translucent, post, debug tiles) parameterised by a `FrameSceneSource` (which entities, which extra primitives). `SandboxApp::onRender` and `renderScene3D` become about 100 lines each.
8. **`NetSession` helper:** role apply, peer join and leave, pawn spawn and palette, shared by all three apps.
9. **Vision modes** become a `VisionModesComponent` plus a system in Play, so the Editor Play gets them too.

---

## 4. Repeated code across the engine

The detector looked for runs of ≥10 normalized non-trivial lines. It found **413 runs and about 5.3k lines** (pairs are counted, so a family of N copies adds N−1 runs).

| Family | Count | Example locations | Fix |
|---|---|---|---|
| Local `FailedHr()` / `CreateUploadBuffer()` helpers | **52 files** (`Render/` ×45, `Particles/`, `Terrain/TerrainGen.cpp`, `Assets/Image.cpp`) | [`Render/CloudVolumeGpuList.cpp:6-41`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/CloudVolumeGpuList.cpp#L6-L41) ↔ [`Render/LocalLightGpuList.cpp:6-41`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/LocalLightGpuList.cpp#L6-L41) ↔ [`Render/DecalGpuList.cpp:7-40`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/DecalGpuList.cpp#L7-L40) | `Render/D3D12Util.h`: `checkHr`, `createBuffer(heap, size, state, name)`, `createTexture2D`, `UploadRing<T>`. |
| Full-screen post root signatures and PSOs | Taa, Tonemap, MotionBlur, Bloom, Underwater, DeferredLighting, Echo | [`TaaPipeline.cpp:33-69`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/TaaPipeline.cpp#L33-L69) ↔ [`TonemapPipeline.cpp:32-68`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/TonemapPipeline.cpp#L32-L68) ↔ [`MotionBlurPipeline.cpp:33-69`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/MotionBlurPipeline.cpp#L33-L69) ↔ [`BloomPipeline.cpp:98-128`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/BloomPipeline.cpp#L98-L128); [`EchoPipeline.cpp:250-274`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/EchoPipeline.cpp#L250-L274) ↔ [`UnderwaterPipeline.cpp:143-167`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/UnderwaterPipeline.cpp#L143-L167) | `RootSigBuilder` + `FullscreenPassDesc{ps, srvCount, constants}` → `createFullscreenPass()`. |
| Mesh, skinned, instanced and terrain PSO input-layout and raster setup | 5 pipelines | [`MeshPipeline.cpp:70-87`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/MeshPipeline.cpp#L70-L87) ↔ [`SkinnedMeshPipeline.cpp:75-91`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/SkinnedMeshPipeline.cpp#L75-L91) ↔ [`InstancedMeshPipeline.cpp:327-346`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/InstancedMeshPipeline.cpp#L327-L346) ↔ [`SkinnedInstanceBuffer.cpp:417-436`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/SkinnedInstanceBuffer.cpp#L417-L436) ↔ [`TerrainPipeline.cpp:64-80`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/TerrainPipeline.cpp#L64-L80) | `GraphicsPsoBuilder`. |
| GPU cull compute setup | 3 | [`InstanceCull.cpp:266-302`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/InstanceCull.cpp#L266-L302) ↔ [`InstanceCull.cpp:680-716`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/InstanceCull.cpp#L680-L716); [`InstanceCull.cpp:448-475`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/InstanceCull.cpp#L448-L475) ↔ [`915-941`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/InstanceCull.cpp#L915-L941); [`FoliagePipeline.cpp:177-201`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/FoliagePipeline.cpp#L177-L201) ↔ [`InstanceCull.cpp:91-115`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/InstanceCull.cpp#L91-L115) | One `CullPass` used by static, skinned and foliage. |
| GTAO ↔ SSR half-res setup | 2 | [`GtaoPipeline.cpp:174-194, 243-267`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/GtaoPipeline.cpp#L174-L267) ↔ [`SsrPipeline.cpp:300-320, 363-387`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/SsrPipeline.cpp#L300-L387); headers [`GtaoPipeline.h:110-125`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/GtaoPipeline.h#L110-L125) ↔ [`SsrPipeline.h:247-262`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/SsrPipeline.h#L247-L262) | |
| JSON read helpers (`jsonToFloat/Double`, `readFloat/Bool/String/Int`) | **14 files** | `AI/Awareness.cpp`, `AI/AttackPattern.cpp`, `Physics/PhysicsSurface.cpp`, `Physics/PhysicsFile.cpp`, `Physics/CollisionCook.cpp`, `Gameplay/ThrowableCatalog.cpp`, `Scene/EntityInput.cpp`, `Scene/EntityMaster.cpp`, `Scene/EntityMovement.cpp`, `Terrain/TerrainLodFile.cpp`, `Terrain/TerrainGround.cpp`, `Character/SkillCatalog.cpp`, `Render/LoadingScreenConfig.cpp`, `Render/EchoChirpFile.cpp`. Examples: [`SkillCatalog.cpp:161-202`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Character/SkillCatalog.cpp#L161-L202) ↔ [`ThrowableCatalog.cpp:57-98`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Gameplay/ThrowableCatalog.cpp#L57-L98); [`AttackPattern.cpp:18-56`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/AI/AttackPattern.cpp#L18-L56) ↔ [`TerrainGround.cpp:33-71`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Terrain/TerrainGround.cpp#L33-L71); [`EntityMaster.cpp:10-48`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Scene/EntityMaster.cpp#L10-L48) ↔ [`EntityMovement.cpp:12-50`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Scene/EntityMovement.cpp#L12-L50) ↔ [`EntityInput.cpp:39-69`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Scene/EntityInput.cpp#L39-L69) | `Core/Json.h` already has `jsonFloatOr`/`jsonIntOr`/…; delete the local copies. Add `bool loadJsonFile(path, maxBytes, json&, std::string* err)`. There are **27 files calling `json::parse`** and 46 `kMaxJsonBytes` references. |
| Blood and decal procedural texture generation | 2 | [`Particles/BloodSplatPool.cpp:33-71`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Particles/BloodSplatPool.cpp#L33-L71) ↔ [`Render/DecalLibrary.cpp:19-57`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/DecalLibrary.cpp#L19-L57) | One generator. |
| Loading screen ↔ main menu drawing | 2 | [`Render/LoadingScreen.cpp:203-231`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/LoadingScreen.cpp#L203-L231) ↔ [`Ui/MainMenu.cpp:47-75`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Ui/MainMenu.cpp#L47-L75); [`IblBakePipeline.cpp:76-97`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/IblBakePipeline.cpp#L76-L97) ↔ [`LoadingScreen.cpp:332-353`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/LoadingScreen.cpp#L332-L353) | |
| Sound-cue banks | 2 | [`Play/CharacterAudio.cpp:37-53`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/CharacterAudio.cpp#L37-L53) and [`SandboxApp.cpp:298-315`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L298-L315) (two `loadClip` wrappers) | Data file (§5). |
| Constant copies | — | Mouse and pad look: [`Play/PlayLook.h:22-23`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/PlayLook.h#L22-L23) and [`SandboxApp.cpp:1183-1184`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L1183-L1184). Contact standoff 2.25: [`Play/ContactGrind.h:12`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/ContactGrind.h#L12) and [`AI/AiSystem.cpp:1456`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/AI/AiSystem.cpp#L1456). Particle caps: [`Particles/ParticleTypes.h:37-38`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Particles/ParticleTypes.h#L37-L38) and [`GpuParticleSystem.h:86-89`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Particles/GpuParticleSystem.h#L86-L89). Terrain layer ids: [`SandboxApp.cpp:183`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L183) and [`Terrain/TerrainGround.cpp:82`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Terrain/TerrainGround.cpp#L82) | Single definitions. |
| Internal near-copies (lower priority) | — | [`SweptCollision.cpp:19-53↔57-83, 256-282↔287-313, 631-653↔658-680`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Collision/SweptCollision.cpp#L19-L83); `Math/Matrix4f.cpp` (3 pairs); [`Capsule2f`↔`Capsule3f`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Math/Capsule3f.cpp#L187-L226); [`Ray2f`↔`Ray3f`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Math/Ray3f.cpp#L50-L74); [`LocalLightGather.cpp:199-226↔351-378`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/LocalLightGather.cpp#L199-L226); [`SkinnedInstanceBuffer.cpp:1167-1191↔1265-1288`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/SkinnedInstanceBuffer.cpp#L1167-L1191); [`EditorTerrain.cpp:831-849↔885-903`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorTerrain.cpp#L831-L849); [`SandboxApp.cpp:3551-3567↔3748-3764`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L3551-L3567); [`TranslateGizmo.cpp:70-99`↔`TranslateGizmo.h:214-243`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/TranslateGizmo.h#L214-L243) | Templatize or extract. |

---

## 5. Hard-coded values that should be config or data

**Context:** 991 `constexpr k*` constants outside tests. Most are structural (descriptor slots, buffer strides) and should stay in code. Below are the tuning and content values. The project already has the right pattern: versioned JSON with `"version"` and a `"note"`, loaded through `resolveContentFile`. It just isn't used for these.

### 5.1 Gameplay tuning

| Area | Location | Values |
|---|---|---|
| **Item catalog (whole table)** | [`Gameplay/Item.cpp:8-22`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Gameplay/Item.cpp#L8-L22) | 13 items: ids, names, types, slots, stack sizes, heal 50, armor 25/40/40/45. Note that `upgrades.json` and `throwables.json` are data, but items are not. |
| Starting kits | [`Play/PlaySession.cpp:399-470`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/PlaySession.cpp#L399-L470) | Player: sword, rifle, 2 medkits. Hunter: 1 medkit, armor and sword. |
| **Status-effect catalog** | [`Combat/StatusCatalog.cpp:7-222`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Combat/StatusCatalog.cpp#L7) | 9 statuses with tick damage 4/2/2, durations and CC. |
| CC rules | [`Combat/StatusId.h:18-19`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Combat/StatusId.h#L18-L19) | DR window 18 s, hard CC max 3 s |
| Armor curve | [`Combat/ArmorStats.h:13-16`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Combat/ArmorStats.h#L13-L16) | K=100, cap 0.75, resist −0.25..0.90 |
| Hit volumes | [`Combat/HitVolumes.h:31-34`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Combat/HitVolumes.h#L31-L34) | part disable 3 s, leg 0.65, waist 0.85 |
| **Hunter jump attack** | [`AI/AiSystem.cpp:50-51, 92-106`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/AI/AiSystem.cpp#L92-L106) | Telegraph 0.40, cooldown 3.5, damage 22/14, leap 10/12, gravity 24. This **duplicates `pounce` in [`content/ai/hunter.attacks.json`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/content/ai/hunter.attacks.json)** (range 4–9, windup 0.40, damage 22). The code value is used only when no pattern is loaded ([`AiSystem.cpp:1888`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/AI/AiSystem.cpp#L1888)), so the two will drift. |
| AI misc | [`AI/AiSystem.cpp:43-60`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/AI/AiSystem.cpp#L43-L60), [`845`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/AI/AiSystem.cpp#L845), [`1860-1861`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/AI/AiSystem.cpp#L1860-L1861), [`2235`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/AI/AiSystem.cpp#L2235) | peer-shown 0.6, lure search 9 s / 4 m, climb-out 12 m, squad radio 0.6 s, duel 4.0/3.2, missing 30 s |
| Wolf pack | [`AI/WolfApproach.h:10-17`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/AI/WolfApproach.h#L10-L17) | 8 distances and timers |
| Contact grind | [`Play/ContactGrind.h:12-13`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/ContactGrind.h#L12-L13) | standoff 2.25, 12 DPS |
| Weapon defaults | [`Weapons/MeleeWeapon.h:11`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Weapons/MeleeWeapon.h#L11), [`ProjectileWeapon.h:47`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Weapons/ProjectileWeapon.h#L47), [`Combat/AttackDef.h:13-15`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Combat/AttackDef.h#L13-L15), [`SpellDef.h:50-52`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Combat/SpellDef.h#L50-L52) | 16 / 24 / 16 / 22 damage |
| Throwables physics | [`Weapons/ThrowableFlight.h:23-26`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Weapons/ThrowableFlight.h#L23-L26) | gravity 18, tap scale 0.55, radius 0.12, life 12 |
| Health pack | [`Gameplay/HealthPack.h:18-20`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Gameplay/HealthPack.h#L18-L20) | radius 1.2, heal 50, respawn 16 |
| Inventory | [`Gameplay/Inventory.h:27`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Gameplay/Inventory.h#L27) | bag size 20 |
| Interaction ranges | [`Play/Interaction.h:50-56`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/Interaction.h#L50-L56) | 2.0–2.6 m ×7; pick cone 0.94 ([`PlaySession.cpp` `pickInteract`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/PlaySession.cpp#L468-L475)) |
| Doors and alarm | [`Play/Facility.cpp:41-45`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/Facility.cpp#L41-L45) | open 2.2, hold 2.8, alarm 15 s / 50 m |
| Security-camera beam | [`Play/Facility.cpp:765-772`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/Facility.cpp#L765-L772) | red 1/0.15/0.1, 900 intensity, cones 18/28 |
| Guard torch | [`Play/AttachedLights.cpp:16,47`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/AttachedLights.cpp#L16-L47) | night if sun < 0.15, colour |
| Play timers | [`Play/PlaySession.h:65-67`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/PlaySession.h#L65-L67) | death 2.5 s, spawn focus 1.75 s, jump buffer 0.12 |
| Camera | [`Play/ShoulderCamera.h:59-60, 97, 120`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/ShoulderCamera.h#L59-L60), [`Play/PlayLook.h:22-23`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/PlayLook.h#L22-L23) | boom 2.4/0.28, swim pitch, **mouse sensitivity 0.0045, pad 2.1** (should be a user setting) |
| Motor | [`Play/PlayMotor.h:15, 59-62`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/PlayMotor.h#L59-L62), [`Character/PlayerMotor.cpp:374-379`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Character/PlayerMotor.cpp#L374-L379) | wall-run and ledge constants. `player.entity.movement.json` already exists, so add them there. |
| Crows | [`Play/Crow.cpp:32-45, 369`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/Crow.cpp#L32-L45) | 12 values |
| HUD timings | `onPlayDefenseText` 0.7 s ([`EditorPlay.cpp:454`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPlay.cpp#L454), [`SandboxApp.cpp:650`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L650)), banner 3.5 s, max 4 banners, banner colours ([`SandboxApp.cpp:619-639`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L619-L639)) | |

### 5.2 Content in code

- **Sound-cue banks**: about 15 cues with paths, synthetic-fallback frequency, duration and amplitude, and volume. Found in [`Play/CharacterAudio.cpp:30-53, 88`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/CharacterAudio.cpp#L37-L53), [`SandboxApp.cpp:307-313`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L307-L313), [`EditorAppInit.cpp:291`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorAppInit.cpp#L291) and [`Sandbox2DApp.cpp:1158`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox2D/Sandbox2DApp.cpp#L1158). There are 206 `"audio/…"` / `"models/…"`-style literals in C++ overall.
- Speech and crow audio ranges: [`Play/DialogueSpeech.cpp:75`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/DialogueSpeech.cpp#L75) (12 m) and [`Play/Crow.cpp:1003`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Play/Crow.cpp#L1003) (56 m). Voice cap: [`Audio/AudioSystem.h:74`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Audio/AudioSystem.h#L74) (24). Master volume is 0.85 at [`SandboxApp.cpp:2513`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L2513).
- **Terrain layer set and tiling**: [`SandboxApp.cpp:183-184`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L183-L184) (dirt/grass/rock/snow, 24/20/16/12).
- **Demo level content keyed on the file name**: `m_demoScene = filename == "level.json"` ([`SandboxBoot.cpp:916`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxBoot.cpp#L916)) gates lanterns at 8 fixed positions with fixed light params ([`SandboxApp.cpp:1986-2050`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L1986-L2050)), the NPC info overlay and the boot path. AGENTS.md allows demo content in `Sandbox/`, but this should be authored scene objects or a `"demo": true` scene flag, not a file-name check.
- Muzzle light: [`SandboxApp.cpp:1996-1998`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L1996-L1998).
- Net palette: [`SandboxApp.cpp:355`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L355) and the 2D copy.
- Default scene names `"level.json"`, `"level2d.json"`, `"untitled.json"` and hand-rolled `-scene:` parsing of `GetCommandLineA()` ([`SandboxApp.cpp:287-296`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L287-L296)). This should go through `AppConfig` and a shared argument parser.
- Editor HSM templates in code (`makeHunterHsmGraph`, [`HsmEditorPanel.cpp:136-151`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/HsmEditorPanel.cpp#L136-L151)), with a built-in fallback graph when JSON fails ([`AiSystem.cpp:300`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/AI/AiSystem.cpp#L300)). This is acceptable as a fallback, but log it at error level in shipping.

### 5.3 Render settings

- The structs exist but nothing loads them:
  - `ShadowSettings` ([`Render/ShadowCascades.h:16-24`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/ShadowCascades.h#L16-L24)): 2048 map, 3 cascades, 280 m, λ 0.65, bias 0.05.
  - `LocalShadowSettings` ([`LocalShadowMath.h:19-21`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/LocalShadowMath.h#L19-L21)), forced to 1024 at [`LocalShadowSystem.cpp:127-128`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/LocalShadowSystem.cpp#L127-L128).
  - `GtaoSettings`, `SsrSettings`, `TaaSettings`, `TonemapSettings`, `MotionBlurSettings` and `AutoExposureSettings`. A grep shows `TaaSettings`, `MotionBlurSettings` and `LocalShadowSettings` are never set outside `Render/`.

  Only `render/echolocation.json` is data-driven ([`EchoChirpFile.cpp:114`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/EchoChirpFile.cpp#L114)).
- Light limits: `kMaxLocalLights` 256, `kWaterLocalLightMax` 8, **`kMaxFogOccluders` 8** ([`LocalLightGather.h:19-22`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/LocalLightGather.h#L19-L22)), `kMaxShadowedLocalLights` 4 ([`LocalShadowMath.h:13`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/LocalShadowMath.h#L13)), `kMaxShadowCandidates` 256 ([`LocalShadowSystem.cpp:29`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/LocalShadowSystem.cpp#L29)), `kMaxShadowCascades` 3.
- Item caps: `kMaxStaticItems` 4096, `kMaxBatches` 1024, `kMaxSkinnedParts` 1024 ([`RenderItem.h:16-18, 68-69`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/RenderItem.h#L16-L69)).
- Particles: [`ParticleTypes.h:36-39`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Particles/ParticleTypes.h#L36-L39). Foliage: [`FoliageFile.h:11-16`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Terrain/FoliageFile.h#L11-L16). Water draws: [`WaterPipeline.h:106`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Render/WaterPipeline.h#L106).

  These are **capacity** limits that size GPU buffers and HLSL arrays. Keep them `constexpr` and shared with HLSL through a generated header. Only expose a runtime "budget ≤ cap" setting.
- Fixed bootstrap camera for streaming: `Vector3f{0,50,-80}` ([`SandboxBoot.cpp:780, 819`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxBoot.cpp#L780-L819)).

### 5.4 Input

- This is largely done well: `content/{sandbox,editor,sandbox2d}.input.json` and `entities/player.entity.input.json`.
- **Remaining hard-coded keys:**
  - Movie debug keys F6/F7/F8/F10: [`SandboxApp.cpp:1079-1095`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L1079-L1095).
  - Scene-picker Up/Down/Enter: [`SandboxApp.cpp:1107-1111`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L1107-L1111) and [`EditorSceneFile.cpp:543-547`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorSceneFile.cpp#L543-L547). This logic is duplicated.
  - A `LeftShift` fallback beside `actionDown("sprint")`: [`SandboxApp.cpp:1172`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L1172).
  - Menu navigation: [`Ui/MainMenu.cpp:274-301`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Ui/MainMenu.cpp#L274-L301).
  - Escape quit: [`Core/Application.cpp:357`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Core/Application.cpp#L357).
- **Prompt text with literal keys** (`"[G] …"`, `"[E] …"`, `"[T] Talk"`): see §3.3 #6.

### 5.5 Network and debug ports

- Ports are 26160/26161 ([`Network/NetTypes.h:83-84`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Network/NetTypes.h#L83-L84)) and the debug port is 26162 ([`Debug/DebugTypes.h:14`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Debug/DebugTypes.h#L14)). I didn't find a config or command-line override, so they belong in `config/net.json` plus `-port:`.

### 5.6 UI layout

- There are 76 `IM_COL32` literals in Sandbox and 26 in the Editor, plus 41 literal `ImVec2` sizes and 21 `SetNextWindow*` calls. `Core/UiPalette.h` exists but the HUD code doesn't use it. Move them to `UiPalette` or `ui/theme.json`, and keep layout constants in the shared `PlayHud.h`.

### 5.7 Proposed config layout

```
content/config/
  game.json           # timers (death, spawn focus), interaction ranges, HUD timings, starting kits
  combat.json         # armor curve, CC rules, hit-volume scales, contact grind, weapon defaults
  ai/hunter.json      # jump attack (single source; drop hunterJumpDef or make it the parsed default), misc AI tunables
  ai/wolf.json        # WolfApproach constants
  ai/crow.json
  facility.json       # doors, alarm, security-camera beam, guard torch
  render.json         # quality presets: shadows{mapSize,cascades,maxDistance,lambda,bias}, localShadows{...}, gtao, ssr, taa, tonemap, motionBlur, autoExposure, fog{occluderBudget}
  audio.json          # master volume, voice budget, speech/crow ranges, occlusion params
  net.json            # ports, beacon, timeouts
  ui/theme.json       # colours, banner timing
content/items/items.json          # replaces Gameplay/Item.cpp table (keep enum parsing in code)
content/combat/statuses.json      # replaces StatusCatalog.cpp
content/audio/cues.json           # named cue -> {path, fallback{hz,dur,amp}, volume, spatial}
content/terrain/layers.json       # layer ids + tiling (used by Sandbox and TerrainGround)
content/schemas/*.schema.json     # one per file type, also for .dlg/.hsm/.anim/.entity
user/settings.json                # (in %LOCALAPPDATA%) mouse sens, pad look, render preset, volume, rebinds
```

**Mechanics:**
1. **Defaults stay in code**, as the existing struct initializers. The loader only overrides keys that are present, using `Core/Json.h` `json*Or` with a single `loadJsonFile` helper (size cap, version check, unknown-key warning).
2. **One `GameData` registry**, owned by the app and passed in `PlayServices`, holds items, statuses, cues and tuning. This replaces the `throwableCatalog()`/`skillCatalog()` singletons.
3. **Hot reload**: a `ContentWatcher` using `ReadDirectoryChangesW` on `content/` (dev builds only) that calls `GameData::reload(path)` at a frame boundary. Tuning structs are applied by value, so a reload is safe whenever no system holds a pointer into the old table. Today there's no file watching anywhere; the only `last_write_time` use is in `Save/SavePaths.cpp`.
4. **Schemas**: add a `$schema` field to each file and validate in the UnitTests (load every `content/**/*.json` against its schema). That catches typos in CI rather than at runtime, which fits the earlier dialogue `op`/`cmp` concern.

---

## 6. Incidental findings

### Confirmed issues (not crashes)
1. **The Editor Play is missing four PlayHost hooks and two entities** (`onMuzzle`, `onBloodBurst`, `onBloodSplat`, `onRespawn`, `cameraEntity`, `muzzle`). See [`EditorPlay.cpp:358-420`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPlay.cpp#L358-L420). This was known and is still open.
2. **Editor spawner hunters get no dialogue binding** ([`EditorPlay.cpp:960-998`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Editor/EditorPlay.cpp#L960-L998) vs [`SandboxApp.cpp:1590-1597`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/SandboxApp.cpp#L1590-L1597)).
3. **Sandbox player physics teleports the kinematic body instead of sweeping it**, unlike the Editor. See §3.3 #2. It needs an in-game check for prop pushing.
4. **The Sandbox walk map always has a 1 m obstacle at the origin and ignores spheres** (§3.3 #4).
5. **The hunter pounce is defined twice**, in code and in `hunter.attacks.json`, with matching values today (§5.1).
6. **There are 16 dead reference members** in `SandboxApp` (§2.3), and **`Sandbox/CMakeLists.txt` is dead** (§2.2).
7. **The Sandbox defense-text timer is decremented inside a draw function** ([`DevToolsPanel.cpp:928-933`](https://github.com/DarkenedGit/DarkEngine6/blob/14945a3f4c00387c8d0bf150eee8406659d78f86/Sandbox/DevToolsPanel.cpp#L928-L933)), so it only counts down on frames where that HUD is drawn.

No new P1 or P2 crash or correctness bugs were found in this half. The earlier open items (movie BGRA UAV flag and so on) are outside it.

---

## 7. Prioritised recommendations

| # | Priority | Recommendation | Value | Effort |
|---|---|---|---|---|
| 1 | **High** | Split `PlayHost` into `PlayServices`, `PlayFrameInput` and `IPlayEvents` with default implementations. This fixes the missing Editor hooks for good. (§3.4-1) | Removes about 250 duplicated lines and a whole class of "forgot the hook" bugs | M |
| 2 | **High** | A single `PlayActors::spawnUnit/attachActor`, and delete both `spawnSpawnerUnit`s, `attachEditorHunter/Wolf` and the `PathChase` hunter spawn (§3.4-2) | Fixes divergences 2 and 4 | S–M |
| 3 | **High** | `ScenePlaySetup` (sliced) for scene → Play: dispatch, walkability, conceal, barks, upgrades (§3.4-3) | Removes the largest remaining shell logic (about 700 lines) | L |
| 4 | **High** | Move items, statuses and sound cues to data, with a `GameData` registry (§5.2, §5.7) | Designers can tune without a rebuild, and it removes the singletons | M |
| 5 | **High** | `Render/D3D12Util.h` (`checkHr`, buffer and texture creation, `UploadRing`) plus a `FullscreenPass` builder (§4) | Deletes about 1.5k lines across 52 files | M |
| 6 | **Med** | A shared `PlaySaveHost` default (§3.4-4) and `PlayHud.h`/`PlayHudState` (banners, defense text, prompts from bindings, upgrade and loot panels) (§3.4-5) | About 500 lines, and fixes the prompt and key drift | M |
| 7 | **Med** | A `render.json` quality preset and `user/settings.json` loading the existing `*Settings` structs, with mouse sensitivity as a user setting (§5.3) | Quality and settings menu becomes possible | S–M |
| 8 | **Med** | Break the module cycle in stages: move `Application` out of `Core` (into an `Engine/` or `Runtime/` layer); move `Frustum3f` to `Math/`; add an `AssetManager` loader registry so Assets stops including AI and Play; keep `Scene/SceneTypes.h` POD (forward-declare or move `Spawner`/`Awareness` enums); feed `Render/` POD descriptors instead of `WeaponKind`/`HealthComponent`; move `Character/*View.cpp` to a presentation folder. Then make the CMake libs link on their own and add a CI check that fails on new upward edges (a small Python include-graph script like the one used here). | Real layering, faster incremental builds, testable libs | L (incremental) |
| 9 | **Med** | A shared `FrameComposer` for the 3D frame (§3.4-7) | About 300 duplicate lines, and one place to add render features | L |
| 10 | **Med** | Replace the 14 local JSON helper copies with `Core/Json.h`, add `loadJsonFile`, add schemas, and add a test that validates all content (§4, §5.7) | Robustness and less code | S |
| 11 | **Low** | `NetSession` helper shared by the three apps; a shared argument parser; remove the cross-TU forward decls (`SandboxBoot.cpp:47-50`) | Cleanliness | S |
| 12 | **Low** | Move the vision modes into Play as a component plus system, so the Editor Play gets them | Parity | M |
| 13 | **Low** | Compile `Ui/` once as a `DarkUi` lib, drop `imgui_demo.cpp` from shipping, delete `Sandbox/CMakeLists.txt` and the 16 dead aliases | Build hygiene | XS |
| 14 | **Low** | `ContentWatcher` hot reload in dev builds (§5.7) | Iteration speed | M |
| 15 | **Low** | Templatize the 2D/3D math pairs (`Capsule`, `Ray`) and the internal `SweptCollision`/`Matrix4f` near-copies | Minor | S |

**Suggested order:** 1 → 2 → 13 (quick hygiene) → 5 → 4 → 6 → 3 → 7 → 10 → 8 and 9 (ongoing).

---

*Tools used: curl on raw files at the commit SHA; a Python include-graph and Tarjan SCC script; a difflib method-similarity script (Editor/Sandbox/Sandbox2D); a 10-line rolling-hash clone detector; a function-length scanner; and targeted greps. These scripts are saved in `/workspace/de6/*.py` (`deps.py`, `fnsim.py`, `clones.py`, `bigfn.py`, `trans.py`).*
