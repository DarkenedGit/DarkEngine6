#pragma once

#include "Render/AutoExposure.h"
#include "Render/BloomPipeline.h"
#include "Render/Camera3D.h"
#include "Render/DebugOverlay.h"
#include "Render/DeferredLightingPipeline.h"
#include "Render/GBufferInputs.h"
#include "Render/GtaoPipeline.h"
#include "Render/SsrPipeline.h"
#include "Render/DecalGpuList.h"
#include "Render/DecalLibrary.h"
#include "Render/DecalPipeline.h"
#include "Render/DecalPool.h"
#include "Render/LineMesh.h"
#include "Render/LinePipeline.h"
#include "Render/LocalLightGpuList.h"
#include "Render/LocalLightVolumePipeline.h"
#include "Render/Mesh.h"
#include "Render/MeshPipeline.h"
#include "Render/MotionBlurPipeline.h"
#include "Render/EchoPipeline.h"
#include "Render/LocalShadowSystem.h"
#include "Render/ShadowSystem.h"
#include "Render/SkinnedMeshPipeline.h"
#include "Render/SkinningUploadRing.h"
#include "Render/SkyPipeline.h"
#include "Render/TaaPipeline.h"
#include "Render/TemporalHistory.h"
#include "Render/TerrainPipeline.h"
#include "Render/TonemapPipeline.h"
#include "Render/UnderwaterPipeline.h"
#include "Render/WaterPipeline.h"
#include "Render/CloudVolumePipeline.h"
#include "Render/CamouflagePipeline.h"

#include <cstdint>
#include <utility>

namespace Dark
{
    namespace Math
    {
        class AABox3f;
    }

class FeatureBuilder;
class FrameComposer;
class Renderer;
class World;

// Shared mesh / lighting / post / (optional) terrain-water-sky pipelines used by
// Sandbox and Editor. Hosts still own world draw order; this owns pipeline
// lifetime and the deferred present/post path (auto exposure → bloom → TAA → motion blur → tonemap).
struct SceneRendererDesc
{
    // When true, also create TerrainPipeline / WaterPipeline / SkyPipeline (Sandbox).
    bool createWorldEnvironment = false;
    // TerrainPipeline only (Editor). Ignored when createWorldEnvironment is true.
    bool createTerrainPipeline = false;
    // Log prefix for create failures (e.g. "SandboxApp", "EditorApp").
    const char* logTag = "SceneRenderer";
    // Sandbox only. Editor leaves this false and does not build the meter.
    bool createAutoExposure = false;
};

// ssrEnv may be null. The other pointers are required.
struct DeferredResolveInput
{
    const Math::Matrix4f*   viewProj     = nullptr;
    const Math::Matrix4f*   prevViewProj = nullptr;
    const GtaoSettings*     gtao         = nullptr;
    const SsrSettings*      ssr          = nullptr;
    const Sky::Environment* ssrEnv       = nullptr;
    const LightingConstants* lighting    = nullptr;
};

class SceneRenderer
{
public:
    SceneRenderer() = default;

    bool init(Renderer& renderer, const SceneRendererDesc& desc = {});
    void shutdown();

    // TAA jitter + view-proj bookkeeping for the current camera.
    // Returns the jittered (or plain) viewProj; stores previous for velocity/post.
    Math::Matrix4f beginCameraFrame(Camera3D& camera, Renderer& renderer);
    void           endCameraFrame(Camera3D& camera, const Math::Matrix4f& viewProj);

    bool wantsTaa(const Renderer& renderer) const;

    // The Post entry with declared uses (render-graph plan Phase 6 part 2), from the host's
    // FrameComposer::declare(PassId::Post) callback: meter (when requested) -> bloom -> TAA ->
    // motion blur -> tonemap, each step its own graph pass binding a descriptor-ring table, so the
    // graph plans every barrier. tonemap.usePostHdr is set here. meterAutoExposure records this
    // frame's 1x1. The host reads the previous sample and writes tonemap.exposure before the call,
    // so bloom and tonemap share that exposure. FrameView::holdTemporal (see requestHistories)
    // forces a TAA reset and skips motion blur so a strobe is not smeared. Runs while execute()
    // builds the graph, before any pass executes: the settings and the previous view-proj are
    // captured now, but `viewProj` is read when the passes execute (hosts fill it in their GBuffer
    // pass), so it must outlive execute(). Call endCameraFrame after execute().
    void addPostPasses(FeatureBuilder& b, Renderer& renderer, const Math::Matrix4f& viewProj, TonemapSettings tonemap, bool meterAutoExposure = false);

    // Right after FrameComposer::begin: requests this frame's TAA / GTAO / SSR graph histories
    // (render-graph plan Phase 5) and applies a pending discardTemporalHistory. `frame` must stay
    // alive until the frame executes (hosts own it).
    void requestHistories(FrameComposer& frame, Renderer& renderer);

    // True when Renderer::resize recreated the meter's pyramid since the last call: the readback was
    // dropped, and the host should zero its stored EV before adapting. Never waits.
    bool syncAutoExposureSize(Renderer& renderer);

    // The GBuffer entry (render-graph plan Phase 6 part 5): one declared pass that writes albedo,
    // attrib, velocity and AO (cleared here from the graph's views) and tests and writes depth
    // (cleared when the frame began), binds them, then runs `draw(Rg::PassContext&)`: the host's
    // terrain, grass, foliage, mesh, model and projectile draws, which bind their own pipelines and
    // material heaps (the graph rebinds the ring heap before the next declared pass). The callable
    // is stored until execute().
    template <class Draw> void addGBufferPasses(FeatureBuilder& b, Renderer& renderer, Draw&& draw)
    {
        Renderer* const rr = &renderer;
        if (!addGBufferPass(b,
                            [rr, draw = std::forward<Draw>(draw)](Rg::PassContext& c, const GBufferTargets& d)
                            {
                                beginGBufferPass(c, *rr, d);
                                draw(c);
                            }))
            b.opaque("GBuffer", {}, {}, [](Rg::PassContext&) { logMissingSceneTargets("GBuffer"); });
    }

    // The Decals entry (Phase 6 part 5): the decal pass (only when decals are on and some are
    // alive; depth tested read-only and sampled, albedo/attrib blended through ROV UAVs, a ring
    // table per definition), then the HDR clear the lighting passes draw onto. `camera` and `in`
    // are read when the passes execute.
    void addDecalsPasses(FeatureBuilder& b, Renderer& renderer, const Camera3D& camera, const DeferredResolveInput& in);

    // The Lighting and LocalVolumes entries with declared uses (render-graph plan Phase 6 part 4).
    // Each is one pass that reads albedo, attrib, depth and the AO lighting uses (GTAO's compose
    // when the Gtao entry published a GtaoOutput, else the authored MRT3; Lighting also reads SSR
    // Full from an SsrOutput, else the dummy) and renders into HDR, binding a descriptor-ring table
    // built from Renderer::lightingViews(). Both read and write HDR (Lighting leaves sky pixels to the
    // Decals clear; LocalVolumes adds to it). `in` is
    // read when the passes execute (hosts fill it in their GBuffer pass), so it must outlive
    // execute(). Lighting stores the constants it drew with (SSR bit sampled after the Ssr entry)
    // for LocalVolumes.
    void addLightingPasses(FeatureBuilder& b, Renderer& renderer, const DeferredResolveInput& in);
    void addLocalVolumesPasses(FeatureBuilder& b, Renderer& renderer, World& world, const Camera3D& camera, const DeferredResolveInput& in);

    // The Gtao, Ssr and SsrCapture entries with declared uses (render-graph plan Phase 6 part 3),
    // from the host's FrameComposer::declare callbacks. Each step is a graph pass binding a
    // descriptor-ring table; the graph plans every barrier on the targets (imported from the
    // ResourceStateTracker). Settings are captured now; the camera (jittered by the host's GBuffer
    // pass) is read when the passes execute, so it must outlive execute(). Gtao and Ssr publish
    // their outputs for the lighting passes (GtaoOutput / SsrOutput, Render/LightingInputs.h).
    // SsrCapture ends with an opaque hand-off pass that leaves the capture in PIXEL_SHADER_RESOURCE
    // and HDR bound with read-only depth for the opaque Water pass.
    // Gtao: compose authored*ssao for the lighting AO when enabled; on skip lighting reads MRT3
    // (never a white SSAO tex) and AoFull is cleared to identity.
    void addGtaoPasses(FeatureBuilder& b, Renderer& renderer, const Camera3D& camera, const GtaoSettings& settings);
    // Ssr: trace -> upsample (-> confidence debug), Full for Lighting's t9; on skip Lighting binds
    // the dummy. env may be null.
    void addSsrPasses(FeatureBuilder& b, Renderer& renderer, const Camera3D& camera, const SsrSettings& settings, const Sky::Environment* env = nullptr);
    // SsrCapture: HDR -> half-res scene color for next frame's trace and this frame's water; then
    // bindHdrDepthRead. Adds no pass when SSR is unavailable (the host adds its own then).
    void addSsrCapturePasses(FeatureBuilder& b, Renderer& renderer);

    // `table`: see DeferredLightingPipeline::draw / LocalLightVolumePipeline::draw.
    void drawDeferredLighting(ID3D12GraphicsCommandList* cmd, D3D12_GPU_DESCRIPTOR_HANDLE table, const LightingConstants& lc) const;
    void drawLocalLights(ID3D12GraphicsCommandList* cmd, D3D12_GPU_DESCRIPTOR_HANDLE table, Renderer& renderer, World& world, const Camera3D& camera, const Math::Matrix4f& viewProj,
                         const LightingConstants& lc);

    // Walls and shut doors for unshadowed local-light fog. The pointer is only read during the next draw.
    void setFogOccluders(const Math::AABox3f* boxes, int count);
    const Math::AABox3f* fogOccluders() const { return m_fogOccluders; }
    int                  fogOccluderCount() const { return m_fogOccluderCount; }

    void tickDecals(World& world, float dt);
    bool spawnDecal(const DecalSpawnDesc& desc, DecalId* outId = nullptr);
    bool decalsReady() const;
    DecalFrameStats decalStats() const;
    // HDR + D32 already bound. Not the decal UAV pass.
    void drawDecalVolumes(ID3D12GraphicsCommandList* cmd, Renderer& renderer, LinePipeline& lines, const Math::Matrix4f& viewProj);

    // HDR color target + depth SRV (bindHdrDepthRead). Composites beer-lambert in-scatter.
    void drawCloudVolumes(ID3D12GraphicsCommandList* cmd, Renderer& renderer, World& world, const Camera3D& camera, const Math::Matrix4f& viewProj,
                          const CloudVolumeFrame& frame);

    // --- Pipeline accessors (hosts use these for special / world draws) ---
    MeshPipeline&             meshPipeline() { return m_meshPipeline; }
    MeshPipeline&             meshTransparentPipeline() { return m_meshTransparentPipeline; }
    SkinnedMeshPipeline&      skinnedPipeline() { return m_skinnedPipeline; }
    SkinnedMeshPipeline&      skinnedTransparentPipeline() { return m_skinnedTransparentPipeline; }
    SkinnedMeshPipeline&      skinnedShadowPipeline() { return m_skinnedShadowPipeline; }
    SkinningUploadRing&       skinRing() { return m_skinRing; }
    TonemapPipeline&          tonemap() { return m_tonemap; }
    DeferredLightingPipeline& lighting() { return m_lighting; }
    LocalLightVolumePipeline& localLightVolumes() { return m_localLightVolumes; }
    LocalLightGpuList&        localLightGpu() { return m_localLightGpu; }
    CloudVolumePipeline&      cloudVolumes() { return m_cloudVolumes; }
    CloudVolumeGpuList&       cloudVolumeGpu() { return m_cloudVolumeGpu; }
    CamouflagePipeline&       camouflage() { return m_camouflage; }
    Mesh&                     pointVolumeMesh() { return m_pointVolumeMesh; }
    Mesh&                     spotVolumeMesh() { return m_spotVolumeMesh; }
    BloomPipeline&            bloom() { return m_bloom; }
    AutoExposurePipeline&     autoExposure() { return m_autoExposure; }
    GtaoPipeline&             gtao() { return m_gtao; }
    SsrPipeline&              ssr() { return m_ssr; }
    MotionBlurPipeline&       motionBlur() { return m_motionBlur; }
    EchoPipeline&             echo() { return m_echo; }
    UnderwaterPipeline&       underwater() { return m_underwater; }
    TaaPipeline&              taa() { return m_taa; }
    ShadowSystem&             shadows() { return m_shadows; }
    LocalShadowSystem&        localShadows() { return m_localShadows; }
    DebugOverlay&             debugOverlay() { return m_debugOverlay; }

    TerrainPipeline& terrainPipeline() { return m_terrainPipeline; }
    WaterPipeline&   waterPipeline() { return m_waterPipeline; }
    SkyPipeline&     skyPipeline() { return m_skyPipeline; }

    const MeshPipeline&             meshPipeline() const { return m_meshPipeline; }
    const MeshPipeline&             meshTransparentPipeline() const { return m_meshTransparentPipeline; }
    const SkinnedMeshPipeline&      skinnedPipeline() const { return m_skinnedPipeline; }
    const SkinnedMeshPipeline&      skinnedTransparentPipeline() const { return m_skinnedTransparentPipeline; }
    const SkinnedMeshPipeline&      skinnedShadowPipeline() const { return m_skinnedShadowPipeline; }
    const SkinningUploadRing&       skinRing() const { return m_skinRing; }
    const TonemapPipeline&          tonemap() const { return m_tonemap; }
    const DeferredLightingPipeline& lighting() const { return m_lighting; }
    const LocalLightVolumePipeline& localLightVolumes() const { return m_localLightVolumes; }
    const LocalLightGpuList&        localLightGpu() const { return m_localLightGpu; }
    const CloudVolumePipeline&      cloudVolumes() const { return m_cloudVolumes; }
    const CloudVolumeGpuList&       cloudVolumeGpu() const { return m_cloudVolumeGpu; }
    const CamouflagePipeline&       camouflage() const { return m_camouflage; }
    const Mesh&                     pointVolumeMesh() const { return m_pointVolumeMesh; }
    const Mesh&                     spotVolumeMesh() const { return m_spotVolumeMesh; }
    const BloomPipeline&            bloom() const { return m_bloom; }
    const AutoExposurePipeline&     autoExposure() const { return m_autoExposure; }
    const GtaoPipeline&             gtao() const { return m_gtao; }
    const SsrPipeline&              ssr() const { return m_ssr; }
    const MotionBlurPipeline&       motionBlur() const { return m_motionBlur; }
    const EchoPipeline&             echo() const { return m_echo; }
    const UnderwaterPipeline&       underwater() const { return m_underwater; }
    const TaaPipeline&              taa() const { return m_taa; }
    const ShadowSystem&             shadows() const { return m_shadows; }
    const LocalShadowSystem&        localShadows() const { return m_localShadows; }
    const DebugOverlay&             debugOverlay() const { return m_debugOverlay; }

    const TerrainPipeline& terrainPipeline() const { return m_terrainPipeline; }
    const WaterPipeline&   waterPipeline() const { return m_waterPipeline; }
    const SkyPipeline&     skyPipeline() const { return m_skyPipeline; }

    const Math::Matrix4f& prevViewProj() const { return m_prevViewProj; }
    bool                  havePrevViewProj() const { return m_havePrevViewProj; }
    // Camera cut or a replaced pose. The next frame has no TAA history and no previous view.
    void                  discardTemporalHistory();
    // Footmarks and blood from the level being left.
    void                  clearDecals() { m_decalPool.clear(); }
    bool                  hasWorldEnvironment() const { return m_haveWorldEnv; }
    bool                  isValid() const { return m_initialized; }

private:
    bool createCorePipelines(Renderer& renderer, const char* tag);
    bool createPostPipelines(Renderer& renderer, const char* tag);
    bool createTerrainPipelineOnly(Renderer& renderer, const char* tag);
    bool createWorldEnvPipelines(Renderer& renderer, const char* tag);
    void markHistoryUnwritten(const Rg::HistoryTargets& history, const char* name);
    void createDecalPass(Renderer& renderer, const char* tag);
    void drawDecalsPass(Rg::PassContext& c, Renderer& renderer, const Camera3D& camera, const Math::Matrix4f& viewProj, const DecalTargets& d);
    bool decalPassReady() const;
    // Clears the G-buffer MRTs and binds them with depth (graph views), with the viewport.
    static void beginGBufferPass(Rg::PassContext& c, const Renderer& renderer, const GBufferTargets& d);
    static void logMissingSceneTargets(const char* entry);
    void storeDecalStats(uint32_t drawn, uint32_t culled, uint32_t inside, uint32_t triangles);

    MeshPipeline             m_meshPipeline;
    MeshPipeline             m_meshTransparentPipeline;
    SkinnedMeshPipeline      m_skinnedPipeline;
    SkinnedMeshPipeline      m_skinnedTransparentPipeline;
    SkinnedMeshPipeline      m_skinnedShadowPipeline;
    SkinningUploadRing       m_skinRing;
    TonemapPipeline          m_tonemap;
    DeferredLightingPipeline m_lighting;
    LocalLightVolumePipeline m_localLightVolumes;
    LocalLightGpuList        m_localLightGpu;
    DecalPipeline            m_decalPipeline;
    DecalGpuList             m_decalGpu;
    DecalLibrary             m_decalLibrary;
    DecalPool                m_decalPool;
    Mesh                     m_decalCube;
    LineMesh                 m_decalVolumeMesh;
    DecalFrameStats          m_decalStats{};
    CloudVolumePipeline      m_cloudVolumes;
    CloudVolumeGpuList       m_cloudVolumeGpu;
    CamouflagePipeline       m_camouflage;
    Mesh                     m_pointVolumeMesh;
    Mesh                     m_spotVolumeMesh;
    BloomPipeline            m_bloom;
    AutoExposurePipeline     m_autoExposure;
    GtaoPipeline             m_gtao;
    SsrPipeline              m_ssr;
    MotionBlurPipeline       m_motionBlur;
    EchoPipeline             m_echo;
    UnderwaterPipeline       m_underwater;
    TaaPipeline              m_taa;
    ShadowSystem             m_shadows;
    LocalShadowSystem        m_localShadows;
    const Math::AABox3f*     m_fogOccluders     = nullptr;
    LightingConstants        m_resolveLighting{}; // the Lighting pass -> LocalVolumes
    int                      m_fogOccluderCount = 0;
    DebugOverlay             m_debugOverlay;

    TerrainPipeline m_terrainPipeline;
    WaterPipeline   m_waterPipeline;
    SkyPipeline     m_skyPipeline;
    bool            m_haveWorldEnv = false;

    Math::Matrix4f m_prevViewProj{};
    bool           m_havePrevViewProj = false;
    bool           m_discardHistory   = false; // discardTemporalHistory: invalidate at the next requestHistories
    bool           m_holdTemporal     = false; // this frame's FrameView::holdTemporal
    // This frame's graph histories (requestHistories). m_frame is the host's composer.
    FrameComposer*     m_frame = nullptr;
    Rg::HistoryTargets m_taaHistory;
    Rg::HistoryTargets m_gtaoHistory;
    Rg::HistoryTargets m_ssrHistory;
    uint64_t       m_autoExposureGeneration = 0;
    bool           m_autoExposureEnabled    = false;
    bool           m_initialized            = false;
};

} // namespace Dark
