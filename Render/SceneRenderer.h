#pragma once

#include "Render/AutoExposure.h"
#include "Render/BloomPipeline.h"
#include "Render/ColorRemapCb.h"
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
#include "Render/LocalShadowSystem.h"
#include "Render/ShadowSystem.h"
#include "Render/SkinnedMeshPipeline.h"
#include "Render/SkinningUploadRing.h"
#include "Render/TaaPipeline.h"
#include "Render/TemporalHistory.h"
#include "Render/TonemapPipeline.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace Dark
{
    namespace Math
    {
        class AABox3f;
    }

class FeatureBuilder;
class ShadowFeature;
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

// A host callable a core pass runs at execute, held by reference: the callable must outlive
// execute() (hosts pass their frame-local draw lambdas). `fn(ctx, pass)`.
struct CorePassHook
{
    void* ctx                                    = nullptr;
    void (*fn)(void* ctx, Rg::PassContext& pass) = nullptr;

    template <class F> static CorePassHook of(F& f)
    {
        return { const_cast<void*>(static_cast<const void*>(&f)), [](void* c, Rg::PassContext& pass) { (*static_cast<F*>(c))(pass); } };
    }
    void operator()(Rg::PassContext& pass) const
    {
        fn(ctx, pass);
    }
};

// The scene's world-draw pipelines and skinning upload ring, handed to the GBuffer and Translucent
// hooks and the game features while their passes execute.
struct WorldDrawContext
{
    MeshPipeline&        mesh;
    MeshPipeline&        meshTransparent;
    SkinnedMeshPipeline& skinned;
    SkinnedMeshPipeline& skinnedTransparent;
    SkinningUploadRing&  skinRing;
};

// A host draw callable the GBuffer or Translucent pass runs at execute, held by reference like
// CorePassHook. `fn(ctx, pass, draws)`.
struct CoreDrawHook
{
    void* ctx                                                                   = nullptr;
    void (*fn)(void* ctx, Rg::PassContext& pass, const WorldDrawContext& draws) = nullptr;

    template <class F> static CoreDrawHook of(F& f)
    {
        return { const_cast<void*>(static_cast<const void*>(&f)), [](void* c, Rg::PassContext& pass, const WorldDrawContext& draws) { (*static_cast<F*>(c))(pass, draws); } };
    }
    void operator()(Rg::PassContext& pass, const WorldDrawContext& draws) const
    {
        fn(ctx, pass, draws);
    }
};

// A feature in the hosts' game slot (PassId::Game), added with SceneRenderer::addGameFeature. The
// scene runs the added features in that entry, in the order they were added.
class IGameFeature
{
public:
    virtual ~IGameFeature() = default;
    // Setup time, inside the game slot's entry. Adds nothing when the feature does not run this
    // frame. `draws` stays valid while the passes execute.
    virtual void addPasses(FeatureBuilder& b, Renderer& renderer, const WorldDrawContext& draws) = 0;
};

// Post's optional auto exposure: the host's meter state and settings, and this frame's artistic
// exposure. When the meter pipeline exists, Post adapts it from last frame's sample (none while
// `hold` is on or in Manual) and meters this frame; `result` gets the outcome either way.
struct CoreAutoExposure
{
    AutoExposureState*          state       = nullptr;
    AutoExposureResult*         result      = nullptr;
    const AutoExposureSettings* settings    = nullptr;
    float                       envExposure = 1.0f;
    float                       dt          = 0.0f;
    bool                        hold        = false;
};

// What the core features (registerCoreFeatures) read this frame. Non-owning: the host fills it
// before the frame executes (the entries read it when the passes are added), and everything it
// points to must outlive execute(). The settings and the tile request are read when the passes
// are added; the camera, `resolve` and the hooks when they execute. ssrEnv may be null.
struct CoreFrameInputs
{
    World*                      world   = nullptr;
    const Camera3D*             camera  = nullptr;
    const GtaoSettings*         gtao    = nullptr;
    const SsrSettings*          ssr     = nullptr;
    const IEnvironmentProvider* ssrEnv  = nullptr;
    const DeferredResolveInput* resolve = nullptr;
    // GBuffer: the host's opaque draws, with the MRTs and depth bound.
    CoreDrawHook gbuffer;
    // Translucent: the host's forward draws, with HDR and read-only depth bound.
    CoreDrawHook translucent;
    // SsrCapture: an optional host pass ahead of the capture, named `beforeSsrCaptureName` (a
    // literal), with no declared uses. A hook without a name is skipped (logged once); the capture
    // still runs.
    CorePassHook beforeSsrCapture;
    const char*  beforeSsrCaptureName = nullptr;
    // Overlay: the debug tiles to draw (null: no pass) and the tile size (0: a fifth of the height).
    const DebugTileRequest* debugTiles    = nullptr;
    int32_t                 debugTileSize = 0;
    // Post: the tonemap settings (exposure is replaced when autoExposure is set) and the
    // view-proj the host fills in its GBuffer pass (read when the passes execute).
    const TonemapSettings* post     = nullptr;
    const Math::Matrix4f*  viewProj = nullptr;
    // Optional; null: no adaptation, no metering, `post`'s exposure as is.
    const CoreAutoExposure* autoExposure = nullptr;
    // Optional: a host HDR pass ahead of Post (reads HDR and depth, writes HDR), named
    // `beforePostName` (a literal). A hook without a name is skipped (logged once); Post still runs.
    CorePassHook beforePost;
    const char*  beforePostName = nullptr;
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

    // Right after FrameComposer::begin: requests this frame's TAA / GTAO / SSR graph histories
    // (render-graph plan Phase 5) and applies a pending discardTemporalHistory. `frame` must stay
    // alive until the frame executes (hosts own it).
    void requestHistories(FrameComposer& frame, Renderer& renderer);

    // The shadow maps FrameComposer::begin imports this frame (a missing map imports nothing).
    FrameShadows frameShadows() const;

    // The GBuffer, Decals, Gtao, Ssr, Lighting, LocalVolumes, SsrCapture, Translucent, Post and
    // Overlay entries are the core features (registerCoreFeatures). They read this frame's inputs, set
    // before the frame executes.
    void setCoreFrame(const CoreFrameInputs& inputs) { m_coreFrame = inputs; }

    // The passes after lighting draw into HDR with read-only depth (Render/HdrDrawInputs.h).
    // Each pass binds its targets, builds its ring tables and runs the host's callable, which is
    // stored until execute(). Without scene targets the entry is an opaque pass that logs once.
    // Translucent samples the shadow map (declared, so the graph moves it there from the Shadows
    // pass); without one it binds a null view. Water and Underwater are Water/WaterRendering.
    // DebugOverlays and the Editor grid and gizmos: `draw(Rg::PassContext&)` with HDR + read-only
    // depth bound. `name` must outlive the frame (a literal).
    template <class Draw> void addHdrOverlayPasses(FeatureBuilder& b, Renderer& renderer, const char* name, Draw&& draw)
    {
        addHdrOverlayPasses(b, renderer, name, HdrDrawUses{}, std::forward<Draw>(draw));
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

    // Starts this frame's slot of the skinning upload ring. Once per frame, before any skinned draw.
    void beginSkinningFrame(uint32_t frameIndex)
    {
        if (m_skinRing.isValid())
            m_skinRing.beginFrame(frameIndex);
    }

    // The game slot (PassId::Game): `feature` runs there each frame, after the features added
    // before it. Non-owning: the host keeps it alive until removeGameFeature or shutdown. False
    // (logged) for null or a feature already added.
    bool addGameFeature(IGameFeature* feature);
    void removeGameFeature(IGameFeature* feature);

    // A host gather of the local lights (Sandbox's water) into the list LocalVolumes draws from.
    // localLightsReady: the GPU list exists. localLightsDummyVa: the list's empty buffer (0 without
    // the list). uploadLocalLights writes `lists` into this frame's slot and returns its address.
    bool                      localLightsReady() const { return m_localLightGpu.isValid(); }
    D3D12_GPU_VIRTUAL_ADDRESS localLightsDummyVa() const { return m_localLightGpu.isValid() ? m_localLightGpu.dummyGpuVa() : 0; }
    D3D12_GPU_VIRTUAL_ADDRESS uploadLocalLights(uint32_t frameIndex, const LocalLightDrawLists& lists)
    {
        m_localLightGpu.upload(frameIndex, lists);
        return m_localLightGpu.lightsGpuVa();
    }
    // Drops the meter's stored samples (the next reads return 0 until a later frame meters), e.g.
    // when the host switches auto exposure back on. Does not change GPU memory.
    void invalidateAutoExposureSamples() { m_autoExposure.invalidateReadback(); }


    // The domain features the host registered (registerXxxFeatures) before init. init creates them
    // after the scene pipelines and shutdown destroys them; the host declares them each frame.
    RenderFeatureRegistry&       features() { return m_features; }
    const RenderFeatureRegistry& features() const { return m_features; }

    const Math::Matrix4f& prevViewProj() const { return m_prevViewProj; }
    bool                  havePrevViewProj() const { return m_havePrevViewProj; }
    // Camera cut or a replaced pose. The next frame has no TAA history and no previous view.
    void                  discardTemporalHistory();
    // Footmarks and blood from the level being left.
    void                  clearDecals() { m_decalPool.clear(); }
    bool                  isValid() const { return m_initialized; }
    // The SSR pipelines exist (the lighting constants' SSR bit, fillSsrLightingConstants).
    bool                  ssrReady() const { return m_ssr.isValid(); }
    // This frame's setup, taken from the composer's view by requestHistories.
    // Hosts call this after resolveSetup so wantsTaa() (the GPU-cull camera view is built before
    // requestHistories) and the pass adders see this frame's bits.
    void                  setFrameSetup(const FrameSetup& setup) { m_setup = setup; }
    const FrameSetup&     frameSetup() const { return m_setup; }
    // Lighting constants published by the Lighting entry (the fullscreen draw, or the hook-off
    // placeholder) for LocalVolumes to consume this frame.
    const LightingConstants& resolveLighting() const { return m_resolveLighting; }

private:
    friend void registerCoreFeatures(RenderFeatureRegistry& registry, SceneRenderer& scene);
    friend void registerShadowFeature(SceneRenderer& scene, ShadowFeature& feature);

    WorldDrawContext worldDraws() { return { m_meshPipeline, m_meshTransparentPipeline, m_skinnedPipeline, m_skinnedTransparentPipeline, m_skinRing }; }

    enum class CoreEntryKind : uint8_t
    {
        Decals,
        Gtao,
        Ssr,
        Lighting,
        LocalVolumes,
        GBuffer,
        SsrCapture,
        Translucent,
        DebugTiles,
        Post,
        Game,
    };
    static constexpr uint32_t kCoreEntryCount = 11;

    // One core list entry as a render feature: its passes come from the add*Passes below.
    class CoreEntry final : public IRenderFeature
    {
    public:
        CoreEntry(SceneRenderer& owner, CoreEntryKind kind) :
            m_owner(owner),
            m_kind(kind)
        {
        }
        const char*  name() const override;
        FeatureStage stage() const override;
        PassId       pass() const override;
        // False without a renderer.
        bool create(const FeatureCreateInfo& info) override;
        void destroy() override;
        // Without this entry's frame inputs (setCoreFrame): an empty pass, logged once.
        void addPasses(FeatureBuilder& b, const FrameView& view) override;

    private:
        bool inputsOk() const;
        void addPostPasses(FeatureBuilder& b);

        SceneRenderer& m_owner;
        CoreEntryKind  m_kind     = CoreEntryKind::Decals;
        Renderer*      m_renderer      = nullptr;
        bool           m_loggedMissing = false;
    };

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
    // Copies this frame's lighting constants (and the SSR bit) into m_resolveLighting. No draw.
    void publishResolveLighting(const DeferredResolveInput& in, const Renderer& renderer);
    // The Gtao and Ssr entries with declared uses (render-graph plan Phase 6 part 3). Each step is a
    // graph pass binding a descriptor-ring table; the graph plans every barrier on the targets
    // (imported from the ResourceStateTracker). Settings are captured now; the camera (jittered by
    // the host's GBuffer pass) is read when the passes execute, so it must outlive execute(). Gtao
    // and Ssr publish their outputs for the lighting passes (GtaoOutput / SsrOutput,
    // Render/LightingInputs.h).
    // Gtao: compose authored*ssao for the lighting AO when enabled; on skip lighting reads MRT3
    // (never a white SSAO tex) and AoFull is cleared to identity.
    void addGtaoPasses(FeatureBuilder& b, Renderer& renderer, const Camera3D& camera, const GtaoSettings& settings);
    // Ssr: trace -> upsample (-> confidence debug), Full for Lighting's t9; on skip Lighting binds
    // the dummy. env may be null.
    void addSsrPasses(FeatureBuilder& b, Renderer& renderer, const Camera3D& camera, const SsrSettings& settings, const IEnvironmentProvider* env = nullptr);
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
                            },
                            m_setup.writeSurfaceTag && b.handle(RenderResource::SurfaceTag).valid()))
            b.opaque("GBuffer", {}, {}, [](Rg::PassContext&) { logMissingSceneTargets("GBuffer"); });
    }

    // The SsrCapture entry with declared uses: HDR -> half-res scene color for next frame's trace
    // and this frame's water (published as SsrCaptureOutput). Adds no pass when SSR is unavailable.
    void addSsrCapturePasses(FeatureBuilder& b, Renderer& renderer);

    // Translucent: as an overlay, also reading the shadow map (the forward materials' heaps hold
    // its SRV, GpuResourceCache::setShadowSrv).
    template <class Draw> void addTranslucentPasses(FeatureBuilder& b, Renderer& renderer, Draw&& draw)
    {
        addHdrOverlayPasses(b, renderer, "Translucent", translucentUses(), std::forward<Draw>(draw));
    }
    // The Overlay entry: the debug texture tiles `request` names, drawn onto the back buffer after
    // Post (Render/DebugTileInputs.h). `tile` is the tile size in pixels (0: a fifth of the height).
    // No pass when nothing is requested or the overlay pipeline is missing.
    void addDebugTilePasses(FeatureBuilder& b, Renderer& renderer, const DebugTileRequest& request, int32_t tile = 0);

    // The Post entry with declared uses (render-graph plan Phase 6 part 2): meter (when requested)
    // -> bloom -> TAA -> motion blur -> tonemap, each step its own graph pass binding a
    // descriptor-ring table, so the graph plans every barrier. tonemap.usePostHdr is set here.
    // meterAutoExposure records this frame's 1x1. The entry adapts tonemap.exposure from the
    // previous sample before the call (CoreAutoExposure), so bloom and tonemap share that exposure.
    // FrameView::holdTemporal (see requestHistories) forces a TAA reset and skips motion blur so a
    // strobe is not smeared. Runs while execute() builds the graph, before any pass executes: the
    // settings and the previous view-proj are captured now, but `viewProj` is read when the passes
    // execute (hosts fill it in their GBuffer pass), so it must outlive execute(). Call
    // endCameraFrame after execute().
    void addPostPasses(FeatureBuilder& b, Renderer& renderer, const Math::Matrix4f& viewProj, TonemapSettings tonemap, bool meterAutoExposure = false);
    // True when Renderer::resize recreated the meter's pyramid since the last call: the readback was
    // dropped, and the Post entry zeroes the host's stored EV before adapting. Never waits.
    bool syncAutoExposureSize(Renderer& renderer);

    bool wantsTaa(const Renderer& renderer) const;
    // `table`: see DeferredLightingPipeline::draw / LocalLightVolumePipeline::draw.
    void drawDeferredLighting(ID3D12GraphicsCommandList* cmd, D3D12_GPU_DESCRIPTOR_HANDLE table, const LightingConstants& lc) const;
    void drawLocalLights(ID3D12GraphicsCommandList* cmd, D3D12_GPU_DESCRIPTOR_HANDLE table, Renderer& renderer, World& world, const Camera3D& camera, const Math::Matrix4f& viewProj,
                         const LightingConstants& lc);
    bool createCorePipelines(Renderer& renderer, const char* tag);
    bool createPostPipelines(Renderer& renderer, const char* tag);
    void markHistoryUnwritten(const Rg::HistoryTargets& history, const char* name);
    void createDecalPass(Renderer& renderer, const char* tag);
    void drawDecalsPass(Rg::PassContext& c, Renderer& renderer, const Camera3D& camera, const Math::Matrix4f& viewProj, const DecalTargets& d);
    bool decalPassReady() const;
    // Clears the G-buffer MRTs and binds them with depth (graph views), with the viewport.
    static void beginGBufferPass(Rg::PassContext& c, const Renderer& renderer, const GBufferTargets& d);
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
    Mesh                     m_pointVolumeMesh;
    Mesh                     m_spotVolumeMesh;
    BloomPipeline            m_bloom;
    AutoExposurePipeline     m_autoExposure;
    GtaoPipeline             m_gtao;
    SsrPipeline              m_ssr;
    MotionBlurPipeline       m_motionBlur;
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
    CoreFrameInputs m_coreFrame{};
    CoreEntry          m_coreEntries[kCoreEntryCount]{
        { *this, CoreEntryKind::Decals },       { *this, CoreEntryKind::Gtao },    { *this, CoreEntryKind::Ssr },        { *this, CoreEntryKind::Lighting },
        { *this, CoreEntryKind::LocalVolumes }, { *this, CoreEntryKind::GBuffer }, { *this, CoreEntryKind::SsrCapture }, { *this, CoreEntryKind::Translucent },
        { *this, CoreEntryKind::DebugTiles },   { *this, CoreEntryKind::Post },    { *this, CoreEntryKind::Game },
    };
    std::vector<IGameFeature*> m_gameFeatures;
    FrameSetup                 m_setup{}; // this frame's (requestHistories)
    ColorRemapCb               m_colorRemapCb; // lighting + local lights; appended last
};

// Adds the GBuffer, Decals, Gtao, Ssr, Lighting, LocalVolumes, SsrCapture, Translucent, Post and
// Overlay entries and the game slot. Hosts register them before SceneRenderer::init and call
// setCoreFrame each frame.
void registerCoreFeatures(RenderFeatureRegistry& registry, SceneRenderer& scene);

// Adds the ShadowFeature entries to the scene's registry and binds the scene's shadow maps and
// skinned depth pipeline to `feature`. Hosts call it before SceneRenderer::init, after
// registerCoreFeatures; they reach the shadow systems through the feature.
void registerShadowFeature(SceneRenderer& scene, ShadowFeature& feature);

} // namespace Dark
