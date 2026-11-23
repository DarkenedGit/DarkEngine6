#pragma once

#include "Render/AutoExposure.h"
#include "Render/BloomPipeline.h"
#include "Render/Camera3D.h"
#include "Render/DebugOverlay.h"
#include "Render/DebugTileInputs.h"
#include "Render/DeferredLightingPipeline.h"
#include "Render/GBufferInputs.h"
#include "Render/GtaoPipeline.h"
#include "Render/HdrDrawInputs.h"
#include "Render/HdrPassViews.h"
#include "Render/RenderFeature.h"
#include "Render/RgViews.h"
#include "Render/ShadowInputs.h"
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
#include "Render/TaaPipeline.h"
#include "Render/TemporalHistory.h"
#include "Render/TonemapPipeline.h"
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

// Shared mesh / lighting / post pipelines used by
// Sandbox and Editor. Hosts still own world draw order; this owns pipeline
// lifetime and the deferred present/post path (auto exposure → bloom → TAA → motion blur → tonemap).
struct SceneRendererDesc
{
    // Log prefix for create failures (e.g. "SandboxApp", "EditorApp").
    const char* logTag = "SceneRenderer";
    // Sandbox only. Editor leaves this false and does not build the meter.
    bool createAutoExposure = false;
    // When true, a registered feature that fails to create fails init (Sandbox). Otherwise it is
    // logged and its passes are skipped.
    bool featuresRequired = false;
};

// ssrEnv may be null. The other pointers are required.
struct DeferredResolveInput
{
    const Math::Matrix4f*   viewProj     = nullptr;
    const Math::Matrix4f*   prevViewProj = nullptr;
    const GtaoSettings*     gtao         = nullptr;
    const SsrSettings*      ssr          = nullptr;
    const IEnvironmentProvider* ssrEnv       = nullptr;
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

    // The Overlay entry: the debug texture tiles `request` names, drawn onto the back buffer after
    // Post (Render/DebugTileInputs.h). `tile` is the tile size in pixels (0: a fifth of the height).
    // No pass when nothing is requested or the overlay pipeline is missing.
    void addDebugTilePasses(FeatureBuilder& b, Renderer& renderer, const DebugTileRequest& request, int32_t tile = 0);

    // Right after FrameComposer::begin: requests this frame's TAA / GTAO / SSR graph histories
    // (render-graph plan Phase 5) and applies a pending discardTemporalHistory. `frame` must stay
    // alive until the frame executes (hosts own it).
    void requestHistories(FrameComposer& frame, Renderer& renderer);

    // True when Renderer::resize recreated the meter's pyramid since the last call: the readback was
    // dropped, and the host should zero its stored EV before adapting. Never waits.
    bool syncAutoExposureSize(Renderer& renderer);

    // The shadow maps FrameComposer::begin imports this frame (a missing map imports nothing).
    FrameShadows frameShadows() const;

    // `draw(Rg::PassContext&)` runs with the MRTs and depth bound and is stored until execute(). It
    // may bind its own descriptor heaps: the executor rebinds the ring before the next declared pass.
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

    // The decal pass is only added when decals are on and some are alive, so a frame without them
    // has no UAV round trip on the G-buffer. `camera` and `in` are read when the passes execute.
    void addDecalsPasses(FeatureBuilder& b, Renderer& renderer, const Camera3D& camera, const DeferredResolveInput& in);

    // The Lighting and LocalVolumes entries with declared uses (render-graph plan Phase 6 part 4).
    // Each is one pass that reads albedo, attrib, depth and the AO lighting uses (GTAO's compose
    // when the Gtao entry published a GtaoOutput, else the authored MRT3; Lighting also reads SSR
    // Full from an SsrOutput, else the dummy, and the shadow map; LocalVolumes the local shadow
    // array, else its dummy) and renders into HDR, binding a descriptor-ring table built from
    // Renderer::lightingViews() and the graph's views. Both read and write HDR (Lighting leaves sky pixels to the
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
    // Gtao: compose authored*ssao for the lighting AO when enabled; on skip lighting reads MRT3
    // (never a white SSAO tex) and AoFull is cleared to identity.
    void addGtaoPasses(FeatureBuilder& b, Renderer& renderer, const Camera3D& camera, const GtaoSettings& settings);
    // Ssr: trace -> upsample (-> confidence debug), Full for Lighting's t9; on skip Lighting binds
    // the dummy. env may be null.
    void addSsrPasses(FeatureBuilder& b, Renderer& renderer, const Camera3D& camera, const SsrSettings& settings, const IEnvironmentProvider* env = nullptr);
    // SsrCapture: HDR -> half-res scene color for next frame's trace and this frame's water
    // (published as SsrCaptureOutput). Adds no pass when SSR is unavailable.
    void addSsrCapturePasses(FeatureBuilder& b, Renderer& renderer);

    // The passes after lighting draw into HDR with read-only depth (Render/HdrDrawInputs.h).
    // Each pass binds its targets, builds its ring tables and runs the host's callable, which is
    // stored until execute(). Without scene targets the entry is an opaque pass that logs once.
    // Translucent samples the shadow map (declared, so the graph moves it there from the Shadows
    // pass); without one it binds a null view. Water and Underwater are Water/WaterRendering.
    // Camouflage: copies HDR, then `draw(Rg::PassContext&, D3D12_GPU_DESCRIPTOR_HANDLE sceneTable)`
    // (the copy, for CamouflagePipeline::drawModel) with HDR and writable depth bound: the body
    // writes depth, and the passes after it test against the new version.
    template <class Draw> void addCamouflagePasses(FeatureBuilder& b, Renderer& renderer, Draw&& draw)
    {
        Renderer* const         rr   = &renderer;
        const Rg::TextureHandle copy = importCamouflageCopy(b, renderer);
        if (!copy.valid() || !Dark::addCamouflagePasses(
                                 b, copy, [](Rg::PassContext& c, const HdrCopyTargets& d) { copyHdr(c, d); },
                                 [rr, draw = std::forward<Draw>(draw)](Rg::PassContext& c, const HdrCopyTargets& d)
                                 {
                                     bindHdrDepthWrite(c, *rr, d.hdr, d.depth);
                                     draw(c, rgSrvTable(c, { d.copy }));
                                 }))
            b.opaque("Camouflage", {}, {}, [](Rg::PassContext&) { logMissingSceneTargets("Camouflage"); });
    }
    // DebugOverlays and the Editor grid and gizmos: `draw(Rg::PassContext&)` with HDR + read-only
    // depth bound. `name` must outlive the frame (a literal).
    template <class Draw> void addHdrOverlayPasses(FeatureBuilder& b, Renderer& renderer, const char* name, Draw&& draw)
    {
        addHdrOverlayPasses(b, renderer, name, HdrDrawUses{}, std::forward<Draw>(draw));
    }
    // Translucent: as an overlay, also reading the shadow map (the forward materials' heaps hold
    // its SRV, GpuResourceCache::setShadowSrv).
    template <class Draw> void addTranslucentPasses(FeatureBuilder& b, Renderer& renderer, Draw&& draw)
    {
        addHdrOverlayPasses(b, renderer, "Translucent", translucentUses(), std::forward<Draw>(draw));
    }
    template <class Draw> void addHdrOverlayPasses(FeatureBuilder& b, Renderer& renderer, const char* name, const HdrDrawUses& uses, Draw&& draw)
    {
        Renderer* const rr = &renderer;
        if (!addHdrDrawPass(b, name, uses,
                            [rr, draw = std::forward<Draw>(draw)](Rg::PassContext& c, const HdrDrawTargets& d)
                            {
                                bindHdrDepthRead(c, *rr, d.hdr, d.depth);
                                draw(c);
                            }))
            b.opaque(name, {}, {}, [name](Rg::PassContext&) { logMissingSceneTargets(name); });
    }
    // `table`: see DeferredLightingPipeline::draw / LocalLightVolumePipeline::draw.
    void drawDeferredLighting(ID3D12GraphicsCommandList* cmd, D3D12_GPU_DESCRIPTOR_HANDLE table, const LightingConstants& lc) const;
    void drawLocalLights(ID3D12GraphicsCommandList* cmd, D3D12_GPU_DESCRIPTOR_HANDLE table, Renderer& renderer, World& world, const Camera3D& camera, const Math::Matrix4f& viewProj,
                         const LightingConstants& lc);

    // Walls and shut doors for unshadowed local-light fog. The pointer is only read during the next draw.
    void setFogOccluders(const Math::AABox3f* boxes, int count);
    const Math::AABox3f* fogOccluders() const { return m_fogOccluders; }
    int                  fogOccluderCount() const { return m_fogOccluderCount; }

    // `life` frees entity decals on a revive (Character/HealthDecalLife.h); null never frees on revive.
    void tickDecals(World& world, float dt, const IDecalLifeSource* life);
    bool spawnDecal(const DecalSpawnDesc& desc, DecalId* outId = nullptr);
    bool decalsReady() const;
    DecalFrameStats decalStats() const;
    // HDR + D32 already bound. Not the decal UAV pass.
    void drawDecalVolumes(ID3D12GraphicsCommandList* cmd, Renderer& renderer, LinePipeline& lines, const Math::Matrix4f& viewProj);

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
    CamouflagePipeline&       camouflage() { return m_camouflage; }
    Mesh&                     pointVolumeMesh() { return m_pointVolumeMesh; }
    Mesh&                     spotVolumeMesh() { return m_spotVolumeMesh; }
    BloomPipeline&            bloom() { return m_bloom; }
    AutoExposurePipeline&     autoExposure() { return m_autoExposure; }
    GtaoPipeline&             gtao() { return m_gtao; }
    SsrPipeline&              ssr() { return m_ssr; }
    MotionBlurPipeline&       motionBlur() { return m_motionBlur; }
    EchoPipeline&             echo() { return m_echo; }
    TaaPipeline&              taa() { return m_taa; }
    ShadowSystem&             shadows() { return m_shadows; }
    LocalShadowSystem&        localShadows() { return m_localShadows; }
    DebugOverlay&             debugOverlay() { return m_debugOverlay; }


    // The domain features the host registered (registerXxxFeatures) before init. init creates them
    // after the scene pipelines and shutdown destroys them; the host declares them each frame.
    RenderFeatureRegistry&       features() { return m_features; }
    const RenderFeatureRegistry& features() const { return m_features; }

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
    const CamouflagePipeline&       camouflage() const { return m_camouflage; }
    const Mesh&                     pointVolumeMesh() const { return m_pointVolumeMesh; }
    const Mesh&                     spotVolumeMesh() const { return m_spotVolumeMesh; }
    const BloomPipeline&            bloom() const { return m_bloom; }
    const AutoExposurePipeline&     autoExposure() const { return m_autoExposure; }
    const GtaoPipeline&             gtao() const { return m_gtao; }
    const SsrPipeline&              ssr() const { return m_ssr; }
    const MotionBlurPipeline&       motionBlur() const { return m_motionBlur; }
    const EchoPipeline&             echo() const { return m_echo; }
    const TaaPipeline&              taa() const { return m_taa; }
    const ShadowSystem&             shadows() const { return m_shadows; }
    const LocalShadowSystem&        localShadows() const { return m_localShadows; }
    const DebugOverlay&             debugOverlay() const { return m_debugOverlay; }


    const Math::Matrix4f& prevViewProj() const { return m_prevViewProj; }
    bool                  havePrevViewProj() const { return m_havePrevViewProj; }
    // Camera cut or a replaced pose. The next frame has no TAA history and no previous view.
    void                  discardTemporalHistory();
    // Footmarks and blood from the level being left.
    void                  clearDecals() { m_decalPool.clear(); }
    bool                  isValid() const { return m_initialized; }

private:
    bool createCorePipelines(Renderer& renderer, const char* tag);
    bool createPostPipelines(Renderer& renderer, const char* tag);
    void markHistoryUnwritten(const Rg::HistoryTargets& history, const char* name);
    void createDecalPass(Renderer& renderer, const char* tag);
    void drawDecalsPass(Rg::PassContext& c, Renderer& renderer, const Camera3D& camera, const Math::Matrix4f& viewProj, const DecalTargets& d);
    bool decalPassReady() const;
    // Clears the G-buffer MRTs and binds them with depth (graph views), with the viewport.
    static void beginGBufferPass(Rg::PassContext& c, const Renderer& renderer, const GBufferTargets& d);
    // This frame's import of the camouflage HDR copy; invalid when it cannot be made.
    Rg::TextureHandle importCamouflageCopy(FeatureBuilder& b, Renderer& renderer);
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
    CamouflagePipeline       m_camouflage;
    Mesh                     m_pointVolumeMesh;
    Mesh                     m_spotVolumeMesh;
    BloomPipeline            m_bloom;
    AutoExposurePipeline     m_autoExposure;
    GtaoPipeline             m_gtao;
    SsrPipeline              m_ssr;
    MotionBlurPipeline       m_motionBlur;
    EchoPipeline             m_echo;
    TaaPipeline              m_taa;
    ShadowSystem             m_shadows;
    LocalShadowSystem        m_localShadows;
    const Math::AABox3f*     m_fogOccluders     = nullptr;
    LightingConstants        m_resolveLighting{}; // the Lighting pass -> LocalVolumes
    int                      m_fogOccluderCount = 0;
    DebugOverlay             m_debugOverlay;


    RenderFeatureRegistry m_features;

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
