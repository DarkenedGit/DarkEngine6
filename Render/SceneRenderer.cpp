#include "Render/SceneRenderer.h"

#include "Core/Log.h"
#include "ECS/World.h"
#include "Render/FrameComposer.h"
#include "Render/GraphTargets.h"
#include "Render/LightingInputs.h"
#include "Render/MeshGen.h"
#include "Render/PostMatrices.h"
#include "Render/Profile.h"
#include "Render/Renderer.h"
#include "Render/RgViews.h"
#include "Render/SceneBuffers.h"
#include "Render/TaaJitter.h"
#include "Math/MathHelper.h"

#include <cstring>

namespace Dark
{
namespace
{
    void copyMatrix(float dst[16], const Math::Matrix4f& m)
    {
        m.store(dst);
    }

    struct DecalEventBlob
    {
        uint64_t color;
        char     name[16];
    };
    static_assert(sizeof(DecalEventBlob) == 24, "DE.Decals blob");

    void beginDecalEvent(ID3D12GraphicsCommandList* cmd)
    {
        DecalEventBlob blob{};
        blob.color = 0xFFC62828ull;
        std::memcpy(blob.name, "DE.Decals", sizeof("DE.Decals"));
        cmd->BeginEvent(1, &blob, static_cast<UINT>(sizeof(blob)));
    }

    void decalVolumeColor(DecalKind kind, float color[3])
    {
        color[0] = 1.0f;
        color[1] = 1.0f;
        color[2] = 1.0f;
        switch (kind)
        {
        case DecalKind::Footmark:
            color[0] = 0.45f;
            color[1] = 0.32f;
            color[2] = 0.18f;
            break;
        case DecalKind::Blood:
            color[0] = 0.7f;
            color[1] = 0.05f;
            color[2] = 0.05f;
            break;
        case DecalKind::Impact:
            color[0] = 0.75f;
            color[1] = 0.75f;
            color[2] = 0.8f;
            break;
        case DecalKind::Burn:
            color[0] = 1.0f;
            color[1] = 0.35f;
            color[2] = 0.05f;
            break;
        case DecalKind::Count:
            break;
        }
    }
} // namespace

bool SceneRenderer::createCorePipelines(Renderer& renderer, const char* tag)
{
    if (!m_meshPipeline.create(renderer.device(), MeshPass::GBuffer))
    {
        DE_LOG_FATAL("{}: MeshPipeline create failed", tag);
        return false;
    }
    if (!m_meshTransparentPipeline.create(renderer.device(), MeshPass::ForwardTransparent, renderer.sceneColorFormat()))
    {
        DE_LOG_FATAL("{}: transparent MeshPipeline create failed", tag);
        return false;
    }

    if (!m_skinnedPipeline.create(renderer.device(), SkinnedMeshPass::GBuffer))
        DE_LOG_ERROR(LogCategory::Render, "{}: SkinnedMeshPipeline create failed; skinned parts skipped", tag);
    if (!m_skinnedTransparentPipeline.create(renderer.device(), SkinnedMeshPass::ForwardTransparent, renderer.sceneColorFormat()))
        DE_LOG_ERROR(LogCategory::Render, "{}: skinned transparent pipeline create failed", tag);
    if (!m_skinnedShadowPipeline.create(renderer.device(), SkinnedMeshPass::Shadow))
        DE_LOG_ERROR(LogCategory::Render, "{}: skinned shadow pipeline create failed", tag);
    if (!m_skinRing.create(renderer.device()))
        DE_LOG_ERROR(LogCategory::Render, "{}: SkinningUploadRing create failed", tag);

    if (!m_shadows.create(renderer.device()))
    {
        DE_LOG_FATAL("{}: ShadowSystem create failed", tag);
        return false;
    }
    // A failed local-shadow atlas still leaves the scene up. Hosts publish the dummy SRV.
    if (!m_localShadows.create(renderer.device()))
        DE_LOG_ERROR(LogCategory::Render, "{}: LocalShadowSystem create failed — local shadows disabled", tag);
    if (!m_debugOverlay.create(renderer.device()))
        DE_LOG_WARN("{}: DebugOverlay create failed — depth/shadow tiles disabled", tag);

    return true;
}

bool SceneRenderer::createPostPipelines(Renderer& renderer, const char* tag)
{
    if (!m_tonemap.create(renderer.device()))
    {
        DE_LOG_FATAL("{}: TonemapPipeline create failed", tag);
        return false;
    }
    // Render-graph plan Phase 5: SSR, bloom, the meter and GTAO register their targets with the
    // renderer's graph targets, which keep them at the output size (no per-owner resize).
    GraphTargets* const targets = renderer.targets();
    if (!targets || !m_ssr.create(*targets))
        DE_LOG_WARN(LogCategory::Render, "{}: SsrPipeline create failed — SSR disabled", tag);
    if (!m_lighting.create(renderer.device()))
    {
        DE_LOG_FATAL("{}: DeferredLightingPipeline create failed", tag);
        return false;
    }
    if (!m_localLightVolumes.create(renderer.device()))
        DE_LOG_ERROR(LogCategory::Render, "{}: LocalLightVolumePipeline create failed — local lights disabled", tag);
    if (!m_localLightGpu.create(renderer.device()))
        DE_LOG_ERROR(LogCategory::Render, "{}: LocalLightGpuList create failed — local lights disabled", tag);
    if (!m_cloudVolumes.create(renderer.device(), renderer.sceneColorFormat()))
        DE_LOG_ERROR(LogCategory::Render, "{}: CloudVolumePipeline create failed — cloud volumes disabled", tag);
    if (!m_cloudVolumeGpu.create(renderer.device()))
        DE_LOG_ERROR(LogCategory::Render, "{}: CloudVolumeGpuList create failed — cloud volumes disabled", tag);
    if (!m_camouflage.create(renderer.device(), renderer.sceneColorFormat()))
        DE_LOG_ERROR(LogCategory::Render, "{}: CamouflagePipeline create failed — camouflage disabled", tag);

    MeshData sphereData;
    MeshData coneData;
    if (!CreateIcosahedronBounding(sphereData, 1.0f, 1) || !Mesh::tryCreate(renderer, sphereData, m_pointVolumeMesh))
        DE_LOG_ERROR(LogCategory::Render, "{}: point volume mesh failed — local lights skipped", tag);
    if (!CreateSpotVolumeCone(coneData, 16, true) || !Mesh::tryCreate(renderer, coneData, m_spotVolumeMesh))
        DE_LOG_ERROR(LogCategory::Render, "{}: spot volume mesh failed — local lights skipped", tag);

    if (!targets || !m_bloom.create(*targets))
        DE_LOG_WARN(LogCategory::Render, "{}: BloomPipeline create failed — bloom disabled", tag);
    if (m_autoExposureEnabled)
    {
        if (!targets || !m_autoExposure.create(*targets, &renderer))
            DE_LOG_WARN(LogCategory::Render, "{}: AutoExposurePipeline create failed — exposure correction stays 0", tag);
        m_autoExposureGeneration = m_autoExposure.isValid() ? m_autoExposure.generation() : 0;
    }
    if (!targets || !m_gtao.create(*targets))
        DE_LOG_WARN(LogCategory::Render, "{}: GtaoPipeline create failed — SSAO disabled", tag);
    if (!m_motionBlur.create(renderer.device()))
        DE_LOG_WARN(LogCategory::Render, "{}: MotionBlurPipeline create failed — motion blur disabled", tag);
    if (!m_echo.create(renderer.device()))
        DE_LOG_WARN(LogCategory::Render, "{}: EchoPipeline create failed — echolocation disabled", tag);
    if (!m_underwater.create(renderer.device()))
        DE_LOG_WARN(LogCategory::Render, "{}: UnderwaterPipeline create failed — underwater view disabled", tag);
    if (!m_taa.create(renderer.device()))
        DE_LOG_WARN(LogCategory::Render, "{}: TaaPipeline create failed — TAA disabled", tag);

    return true;
}

bool SceneRenderer::createTerrainPipelineOnly(Renderer& renderer, const char* tag)
{
    if (!m_terrainPipeline.create(renderer.device()))
    {
        DE_LOG_FATAL("{}: TerrainPipeline create failed", tag);
        return false;
    }
    if (!m_waterPipeline.create(renderer.device(), renderer.sceneColorFormat()))
    {
        DE_LOG_ERROR(LogCategory::Render, "{}: WaterPipeline create failed — no water in this host", tag);
    }
    return true;
}

bool SceneRenderer::createWorldEnvPipelines(Renderer& renderer, const char* tag)
{
    if (!createTerrainPipelineOnly(renderer, tag))
        return false;
    if (!m_waterPipeline.isValid())
    {
        DE_LOG_FATAL("{}: WaterPipeline create failed", tag);
        return false;
    }
    if (!m_skyPipeline.create(renderer.device(), renderer.sceneColorFormat()))
    {
        DE_LOG_FATAL("{}: SkyPipeline create failed", tag);
        return false;
    }
    m_haveWorldEnv = true;
    return true;
}

bool SceneRenderer::init(Renderer& renderer, const SceneRendererDesc& desc)
{
    const char* tag = desc.logTag ? desc.logTag : "SceneRenderer";
    // The 3D frame is always deferred (D2). Hosts create the scene targets first and quit if that fails.
    if (!renderer.hasSceneBuffers())
    {
        DE_LOG_FATAL("{}: scene targets missing; call Renderer::createSceneTargets() before SceneRenderer::init", tag);
        return false;
    }
    m_autoExposureEnabled = desc.createAutoExposure;
    if (!createCorePipelines(renderer, tag))
        return false;
    if (!createPostPipelines(renderer, tag))
        return false;
    if (desc.createWorldEnvironment)
    {
        if (!createWorldEnvPipelines(renderer, tag))
            return false;
    }
    else if (desc.createTerrainPipeline)
    {
        if (!createTerrainPipelineOnly(renderer, tag))
            return false;
    }
    createDecalPass(renderer, tag);
    m_initialized = true;
    return true;
}

void SceneRenderer::createDecalPass(Renderer& renderer, const char* tag)
{
    if (!m_decalPipeline.create(renderer.device()))
        DE_LOG_WARN(LogCategory::Render, "{}: DecalPipeline create failed — decals disabled", tag);
    if (!m_decalGpu.create(renderer.device()))
        DE_LOG_WARN(LogCategory::Render, "{}: DecalGpuList create failed — decals disabled", tag);
    if (!m_decalLibrary.create(renderer))
        DE_LOG_WARN(LogCategory::Render, "{}: DecalLibrary create failed — decals disabled", tag);

    MeshData cube;
    if (!CreateUnitCube(cube) || !Mesh::tryCreate(renderer, cube, m_decalCube))
        DE_LOG_WARN(LogCategory::Render, "{}: decal cube mesh failed — decals disabled", tag);

    LineMeshData boxLines;
    if (!CreateBoxOutline(boxLines))
        DE_LOG_WARN(LogCategory::Render, "{}: decal volume line mesh failed", tag);
    else
    {
        // CreateBoxOutline is ±0.5. The decal matrix scales the ±1 cube.
        for (Math::Vector3f& p : boxLines.positions)
            p *= 2.0f;
        m_decalVolumeMesh = LineMesh::Create(renderer, boxLines);
        if (!m_decalVolumeMesh.valid())
            DE_LOG_WARN(LogCategory::Render, "{}: decal volume line mesh failed", tag);
    }
}

void SceneRenderer::shutdown()
{
    m_meshPipeline             = MeshPipeline{};
    m_meshTransparentPipeline  = MeshPipeline{};
    m_skinnedPipeline          = SkinnedMeshPipeline{};
    m_skinnedTransparentPipeline = SkinnedMeshPipeline{};
    m_skinnedShadowPipeline    = SkinnedMeshPipeline{};
    m_skinRing                 = SkinningUploadRing{};
    m_tonemap                  = TonemapPipeline{};
    m_lighting                 = DeferredLightingPipeline{};
    m_localLightVolumes        = LocalLightVolumePipeline{};
    m_localLightGpu            = LocalLightGpuList{};
    m_decalPipeline            = DecalPipeline{};
    m_decalGpu                 = DecalGpuList{};
    m_decalLibrary             = DecalLibrary{};
    m_decalPool                = DecalPool{};
    m_decalCube                = Mesh{};
    m_decalVolumeMesh          = LineMesh{};
    m_decalStats               = {};
    m_cloudVolumes             = CloudVolumePipeline{};
    m_cloudVolumeGpu           = CloudVolumeGpuList{};
    m_camouflage.release();
    m_pointVolumeMesh          = Mesh{};
    m_spotVolumeMesh           = Mesh{};
    // Graph-target owners unregister in release() (they are not reassignable).
    m_bloom.release();
    m_autoExposure.release();
    m_gtao.release();
    m_ssr.release();
    m_motionBlur               = MotionBlurPipeline{};
    m_echo.release();
    m_underwater.release();
    m_taa                      = TaaPipeline{};
    m_shadows                  = ShadowSystem{};
    m_localShadows             = LocalShadowSystem{};
    m_debugOverlay             = DebugOverlay{};
    m_terrainPipeline          = TerrainPipeline{};
    m_waterPipeline            = WaterPipeline{};
    m_skyPipeline              = SkyPipeline{};
    m_haveWorldEnv             = false;
    m_prevViewProj             = Math::Matrix4f{};
    m_havePrevViewProj         = false;
    m_discardHistory           = false;
    m_holdTemporal             = false;
    m_frame                    = nullptr;
    m_taaHistory               = {};
    m_gtaoHistory              = {};
    m_ssrHistory               = {};
    m_autoExposureGeneration   = 0;
    m_autoExposureEnabled      = false;
    m_initialized              = false;
}

bool SceneRenderer::wantsTaa(const Renderer& renderer) const
{
    return renderer.debugState().taa && m_taa.isValid();
}

void SceneRenderer::requestHistories(FrameComposer& frame, Renderer& renderer)
{
    // Render-graph plan Phase 5: TAA, GTAO and SSR histories are graph histories. The graph decides
    // validity (resize, hold, invalidate, a skipped write) and the ping-pong; the pairs live in the
    // renderer's graph targets. Only what can run this frame is requested, so an off feature holds
    // no history memory (its slots are collected at execute).
    m_frame        = &frame;
    m_holdTemporal = frame.view().holdTemporal;
    if (m_discardHistory)
    {
        frame.invalidateHistory();
        m_discardHistory = false;
    }
    m_taaHistory  = {};
    m_gtaoHistory = {};
    m_ssrHistory  = {};
    if (!renderer.targets() || !renderer.hasSceneBuffers())
        return;
    if (wantsTaa(renderer))
    {
        const float zero4[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_taaHistory = frame.history(RenderResource::TaaHistory, "Taa",
                                     colorTargetDesc(DXGI_FORMAT_R16G16B16A16_FLOAT, Rg::TargetSize::OutputFloor, 1, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, zero4));
    }
    if (m_gtao.isValid() && renderer.debugState().ssaoEnabled)
        m_gtaoHistory = frame.history(RenderResource::GtaoHistory, "Gtao", GtaoPipeline::historyDesc());
    if (m_ssr.isValid() && renderer.debugState().ssrEnabled)
        m_ssrHistory = frame.history(RenderResource::SsrHistory, "Ssr", SsrPipeline::historyDesc());
}

void SceneRenderer::markHistoryUnwritten(const Rg::HistoryTargets& history, const char* name)
{
    if (m_frame && history.valid())
        m_frame->markHistoryUnwritten(name);
}

Math::Matrix4f SceneRenderer::beginCameraFrame(Camera3D& camera, Renderer& renderer)
{
    camera.ClearSubpixelJitter();
    if (wantsTaa(renderer))
    {
        float jx = 0.0f, jy = 0.0f;
        taaHaltonJitter(renderer.frameIndex(), jx, jy);
        camera.SetSubpixelJitter(jx, jy, renderer.width(), renderer.height());
    }
    return camera.GetViewProj();
}

void SceneRenderer::endCameraFrame(Camera3D& camera, const Math::Matrix4f& viewProj)
{
    m_prevViewProj     = viewProj;
    m_havePrevViewProj = true;
    camera.ClearSubpixelJitter();
}

void SceneRenderer::discardTemporalHistory()
{
    m_havePrevViewProj = false;
    m_discardHistory   = true; // the next requestHistories invalidates the graph histories
}

void SceneRenderer::addGtaoPasses(FeatureBuilder& b, Renderer& renderer, const Camera3D& camera, const GtaoSettings& settings)
{
    using Rg::PassBuilder;
    using Rg::PassContext;
    using Rg::TextureHandle;
    using Rg::Use;
    using R = RenderResource;

    GtaoSettings drawSettings = settings;
    drawSettings.enabled      = settings.enabled && renderer.debugState().ssaoEnabled;
    const TextureHandle depth    = b.handle(R::Depth);
    const TextureHandle attrib   = b.handle(R::Attrib);
    const TextureHandle velocity = b.handle(R::Velocity);
    const TextureHandle ao       = b.handle(R::Ao);
    const TextureHandle histCurr = b.handle(R::GtaoHistory);
    const HistoryView   history  = renderer.targets() ? renderer.targets()->historyView(m_gtaoHistory) : HistoryView{};
    const bool          run      = drawSettings.enabled && m_gtao.isValid() && renderer.device() != nullptr && renderer.depthSrvCpu().ptr != 0 && renderer.attribSrvCpu().ptr != 0 &&
                     renderer.velocitySrvCpu().ptr != 0 && renderer.aoSrvCpu().ptr != 0 && depth.valid() && attrib.valid() && velocity.valid() && ao.valid() && histCurr.valid() && history.valid();
    Renderer* const     rr   = &renderer;
    GtaoPipeline* const gtao = &m_gtao;
    if (!run)
    {
        // No GtaoOutput: lighting reads the authored MRT3. Never a white SSAO tex (compose cleared to 1
        // would wipe ORM cavities).
        markHistoryUnwritten(m_gtaoHistory, "Gtao");
        b.opaque("Gtao", {}, {},
                 [gtao](PassContext& c)
                 {
                     gtao->clearAoFullIdentity(c.cmd()); // overlay-when-off samples identity, not stale/garbage
                 });
        return;
    }

    // The graph's prevValid covers first use, re-enable, resize and held frames.
    const TextureHandle histPrev     = b.prev(R::GtaoHistory);
    const bool          resetHistory = !history.prevValid || !histPrev.valid() || !m_havePrevViewProj || (wantsTaa(renderer) && !m_taaHistory.prevValid);
    // Without a valid prev the shader ignores t3 (reset); the other slot's SRV fills the table.
    const D3D12_CPU_DESCRIPTOR_HANDLE resetPrev = history.prevSrv;
    // Read at execute: the camera is jittered by the host's GBuffer pass, which runs after this setup.
    const Camera3D* const cam      = &camera;
    const bool            havePrev = m_havePrevViewProj;
    const Math::Matrix4f  prev     = m_prevViewProj;

    Rg::RenderGraph& g    = b.graph();
    TextureHandle    half = importTracked(g, GtaoPipeline::name(GtaoTarget::Half), gtao->resource(GtaoTarget::Half), textureViews(gtao->srv(GtaoTarget::Half), gtao->rtv(GtaoTarget::Half)));
    TextureHandle    full = importTracked(g, GtaoPipeline::name(GtaoTarget::Full), gtao->resource(GtaoTarget::Full), textureViews(gtao->srv(GtaoTarget::Full), gtao->rtv(GtaoTarget::Full)));
    TextureHandle    compose =
        importTracked(g, GtaoPipeline::name(GtaoTarget::Compose), gtao->resource(GtaoTarget::Compose), textureViews(gtao->srv(GtaoTarget::Compose), gtao->rtv(GtaoTarget::Compose)));

    struct Produce
    {
        TextureHandle depth, attrib, half;
    };
    b.addPass<Produce>(
        "Gtao.Produce",
        [&](PassBuilder& pb, Produce& d)
        {
            d.depth  = pb.read(depth, Use::PixelSrv);
            d.attrib = pb.read(attrib, Use::PixelSrv);
            d.half = half = pb.write(half, Use::RenderTarget);
        },
        [gtao, rr, cam, havePrev, prev, drawSettings, resetHistory](PassContext& c, const Produce& d)
        {
            profileGpuBegin(c.cmd(), "GTAO", ProfileColor::Gtao);
            const D3D12_GPU_VIRTUAL_ADDRESS params = gtao->uploadParams(*cam, havePrev ? prev : cam->GetViewProj(), drawSettings, resetHistory, static_cast<uint32_t>(rr->frameIndex()));
            gtao->drawProduce(c.cmd(), params, rgSrvTable(c, { d.depth, d.attrib, d.attrib, d.attrib, d.depth }));
        });

    struct Upsample
    {
        TextureHandle half, depth, attrib, prev, velocity, full, hist;
    };
    TextureHandle histOut;
    b.addPass<Upsample>(
        "Gtao.Upsample",
        [&](PassBuilder& pb, Upsample& d)
        {
            d.half     = pb.read(half, Use::PixelSrv);
            d.depth    = pb.read(depth, Use::PixelSrv);
            d.attrib   = pb.read(attrib, Use::PixelSrv);
            d.prev     = histPrev.valid() ? pb.read(histPrev, Use::PixelSrv) : TextureHandle{};
            d.velocity = pb.read(velocity, Use::PixelSrv);
            d.full = full = pb.write(full, Use::RenderTarget);
            d.hist = histOut = pb.write(histCurr, Use::RenderTarget);
        },
        [gtao, rr, resetPrev](PassContext& c, const Upsample& d)
        {
            const D3D12_GPU_DESCRIPTOR_HANDLE table = d.prev.valid() ? rgSrvTable(c, { d.half, d.depth, d.attrib, d.prev, d.velocity })
                                                                     : rgTable(c, { rgSrvCpu(c, d.half), rgSrvCpu(c, d.depth), rgSrvCpu(c, d.attrib), resetPrev, rgSrvCpu(c, d.velocity) });
            gtao->drawUpsample(c.cmd(), gtao->paramsAddress(static_cast<uint32_t>(rr->frameIndex())), table, rgRtv(c, d.hist));
        });
    b.set(R::GtaoHistory, histOut);

    struct Compose
    {
        TextureHandle ao, full, compose;
    };
    b.addPass<Compose>(
        "Gtao.Compose",
        [&](PassBuilder& pb, Compose& d)
        {
            d.ao      = pb.read(ao, Use::PixelSrv);
            d.full    = pb.read(full, Use::PixelSrv);
            d.compose = compose = pb.write(compose, Use::RenderTarget);
        },
        [gtao, rr](PassContext& c, const Compose& d)
        {
            gtao->drawCompose(c.cmd(), gtao->paramsAddress(static_cast<uint32_t>(rr->frameIndex())), rgSrvTable(c, { d.ao, d.full, d.full, d.full, d.full }));
            profileGpuEnd(c.cmd()); // GTAO
        });

    // Lighting and LocalVolumes read it (lightingReads); the graph plans the barrier into them.
    g.blackboard().set(GtaoOutput{ compose });
}

void SceneRenderer::addSsrPasses(FeatureBuilder& b, Renderer& renderer, const Camera3D& camera, const SsrSettings& settings, const Sky::Environment* env)
{
    using Rg::PassBuilder;
    using Rg::PassContext;
    using Rg::TextureHandle;
    using Rg::Use;
    using R = RenderResource;

    SsrSettings drawSettings = settings;
    drawSettings.enabled     = settings.enabled && renderer.debugState().ssrEnabled;
    const TextureHandle depth    = b.handle(R::Depth);
    const TextureHandle attrib   = b.handle(R::Attrib);
    const TextureHandle velocity = b.handle(R::Velocity);
    const TextureHandle histCurr = b.handle(R::SsrHistory);
    const HistoryView   history  = renderer.targets() ? renderer.targets()->historyView(m_ssrHistory) : HistoryView{};
    const bool          run      = drawSettings.enabled && m_ssr.isValid() && renderer.device() != nullptr && renderer.depthSrvCpu().ptr != 0 && renderer.attribSrvCpu().ptr != 0 &&
                     renderer.velocitySrvCpu().ptr != 0 && depth.valid() && attrib.valid() && velocity.valid() && histCurr.valid() && history.valid();
    Renderer* const    rr  = &renderer;
    SsrPipeline* const ssr = &m_ssr;
    if (!run)
    {
        markHistoryUnwritten(m_ssrHistory, "Ssr");
        // No SsrOutput: lighting binds the dummy. The pass keeps the list entry (the order check
        // needs one) and does nothing else now that Lighting binds its own target.
        b.opaque("Ssr", {}, {}, [](PassContext&) {});
        return;
    }

    const TextureHandle histPrev     = b.prev(R::SsrHistory);
    const bool          resetHistory = !history.prevValid || !histPrev.valid() || !m_havePrevViewProj || (wantsTaa(renderer) && !m_taaHistory.prevValid);
    // Without a valid prev (graph SsrHistory) the shader ignores t3; the other slot's SRV fills it.
    const D3D12_CPU_DESCRIPTOR_HANDLE resetPrev = history.prevSrv;
    const Camera3D* const             cam       = &camera;
    const bool                        havePrev  = m_havePrevViewProj;
    const Math::Matrix4f              prev      = m_prevViewProj;
    const bool                        debug     = renderer.debugState().ssrDebug == 2;

    const auto          importSsr = [&](SsrTarget t) { return importTracked(b.graph(), SsrPipeline::name(t), ssr->resource(t), textureViews(ssr->srv(t), ssr->rtv(t))); };
    TextureHandle       half      = importSsr(SsrTarget::Half);
    TextureHandle       full      = importSsr(SsrTarget::Full);
    const TextureHandle scene     = importSsr(ssr->sceneColorRead()); // last frame's capture
    TextureHandle       conf      = debug ? importSsr(SsrTarget::Debug) : TextureHandle{};

    struct Trace
    {
        TextureHandle depth, attrib, scene, half;
    };
    b.addPass<Trace>(
        "Ssr.Trace",
        [&](PassBuilder& pb, Trace& d)
        {
            d.depth  = pb.read(depth, Use::PixelSrv);
            d.attrib = pb.read(attrib, Use::PixelSrv);
            d.scene  = pb.read(scene, Use::PixelSrv);
            d.half = half = pb.write(half, Use::RenderTarget);
        },
        [ssr, rr, cam, havePrev, prev, env, drawSettings, resetHistory](PassContext& c, const Trace& d)
        {
            profileGpuBegin(c.cmd(), "SSR", ProfileColor::Ssr);
            const SsrFrameParams params = ssr->uploadParams(*cam, havePrev ? prev : cam->GetViewProj(), env, drawSettings, resetHistory, static_cast<uint32_t>(rr->frameIndex()));
            ssr->drawStep(c.cmd(), SsrStep::Trace, params, rgSrvTable(c, { d.depth, d.attrib, d.scene, d.scene, d.scene }), rgRtv(c, d.half));
        });

    struct Upsample
    {
        TextureHandle half, depth, attrib, prev, velocity, full, hist;
        bool          closeScope;
    };
    TextureHandle histOut;
    b.addPass<Upsample>(
        "Ssr.Upsample",
        [&](PassBuilder& pb, Upsample& d)
        {
            d.half     = pb.read(half, Use::PixelSrv);
            d.depth    = pb.read(depth, Use::PixelSrv);
            d.attrib   = pb.read(attrib, Use::PixelSrv);
            d.prev     = histPrev.valid() ? pb.read(histPrev, Use::PixelSrv) : TextureHandle{};
            d.velocity = pb.read(velocity, Use::PixelSrv);
            d.full = full = pb.write(full, Use::RenderTarget);
            d.hist = histOut = pb.write(histCurr, Use::RenderTarget);
            d.closeScope     = !debug;
        },
        [ssr, rr, resetPrev](PassContext& c, const Upsample& d)
        {
            const D3D12_GPU_DESCRIPTOR_HANDLE table = d.prev.valid() ? rgSrvTable(c, { d.half, d.depth, d.attrib, d.prev, d.velocity })
                                                                     : rgTable(c, { rgSrvCpu(c, d.half), rgSrvCpu(c, d.depth), rgSrvCpu(c, d.attrib), resetPrev, rgSrvCpu(c, d.velocity) });
            ssr->drawStep(c.cmd(), SsrStep::Upsample, ssr->paramsAddress(static_cast<uint32_t>(rr->frameIndex())), table, rgRtv(c, d.full), rgRtv(c, d.hist));
            if (d.closeScope)
                profileGpuEnd(c.cmd()); // SSR
        });
    b.set(R::SsrHistory, histOut);

    if (debug)
    {
        struct Debug
        {
            TextureHandle full, conf;
        };
        b.addPass<Debug>(
            "Ssr.Debug",
            [&](PassBuilder& pb, Debug& d)
            {
                d.full = pb.read(full, Use::PixelSrv);
                d.conf = conf = pb.write(conf, Use::RenderTarget);
            },
            [ssr, rr](PassContext& c, const Debug& d)
            {
                ssr->drawStep(c.cmd(), SsrStep::Debug, ssr->paramsAddress(static_cast<uint32_t>(rr->frameIndex())), rgSrvTable(c, { d.full, d.full, d.full, d.full, d.full }), rgRtv(c, d.conf));
                profileGpuEnd(c.cmd()); // SSR
            });
    }

    // Lighting reads it (lightingReads); the graph plans the barrier into it. The debug overlay moves
    // the confidence tile itself.
    b.graph().blackboard().set(SsrOutput{ full });
}

void SceneRenderer::addSsrCapturePasses(FeatureBuilder& b, Renderer& renderer)
{
    using Rg::TextureHandle;
    using Rg::Use;
    const TextureHandle hdr = b.handle(RenderResource::Hdr);
    if (!m_ssr.isValid() || !renderer.hasSceneBuffers() || !hdr.valid())
        return;
    SsrPipeline* const ssr     = &m_ssr;
    Renderer* const    rr      = &renderer;
    const SsrTarget    written = ssr->beginCapture(); // water and next frame's trace read it
    TextureHandle      dst     = importTracked(b.graph(), SsrPipeline::name(written), ssr->resource(written), textureViews(ssr->srv(written), ssr->rtv(written)));

    struct Capture
    {
        TextureHandle hdr, dst;
    };
    b.addPass<Capture>(
        "SsrCapture",
        [&](Rg::PassBuilder& pb, Capture& d)
        {
            d.hdr = pb.read(hdr, Use::PixelSrv);
            d.dst = dst = pb.write(dst, Use::RenderTarget);
        },
        [ssr, rr](Rg::PassContext& c, const Capture& d)
        {
            const GpuScope capture(c.cmd(), "SSR Capture", ProfileColor::Ssr);
            ssr->drawStep(c.cmd(), SsrStep::Capture, ssr->uploadCaptureParams(static_cast<uint32_t>(rr->frameIndex())), rgSrvTable(c, { d.hdr, d.hdr, d.hdr, d.hdr, d.hdr }), rgRtv(c, d.dst));
        });

    // Water and the underwater ceiling read it (HdrDrawInputs.h); the graph plans the barrier into it.
    b.graph().blackboard().set(SsrCaptureOutput{ dst });
}

void SceneRenderer::bindHdr(Rg::PassContext& c, const Renderer& renderer, Rg::TextureHandle hdr)
{
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rgRtv(c, hdr);
    c.cmd()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    c.cmd()->RSSetViewports(1, &renderer.viewport());
    c.cmd()->RSSetScissorRects(1, &renderer.scissor());
}

void SceneRenderer::bindHdrDepthRead(Rg::PassContext& c, const Renderer& renderer, Rg::TextureHandle hdr, Rg::TextureHandle depth)
{
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rgRtv(c, hdr);
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = cpuHandle(c.dsv(depth, true));
    c.cmd()->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    c.cmd()->RSSetViewports(1, &renderer.viewport());
    c.cmd()->RSSetScissorRects(1, &renderer.scissor());
}

D3D12_GPU_DESCRIPTOR_HANDLE SceneRenderer::shadowTable(Rg::PassContext& c, const Renderer& renderer)
{
    return rgTable(c, { renderer.lightingViews().shadow });
}

WaterTables SceneRenderer::waterTables(Rg::PassContext& c, const Renderer& renderer, Rg::TextureHandle depth, Rg::TextureHandle sceneColor) const
{
    // No capture this frame: the SSR slots hold the pipeline's dummies and ssrReady is false.
    const LightingViews v = renderer.lightingViews();
    WaterTables         t;
    t.sceneColor = sceneColor.valid();
    t.table = rgTable(c, { v.height, v.shadow, t.sceneColor ? rgSrvCpu(c, sceneColor) : m_waterPipeline.ssrDummyColorCpu(), t.sceneColor ? rgSrvCpu(c, depth) : m_waterPipeline.ssrDummyDepthCpu() });
    return t;
}

Rg::TextureHandle SceneRenderer::importUnderwaterCopy(FeatureBuilder& b, Renderer& renderer)
{
    if (!m_underwater.isValid() || !m_underwater.ensureCopy(renderer))
        return {};
    return importTracked(b.graph(), "DE.Underwater.HdrCopy", m_underwater.copyResource(), textureViews(m_underwater.copySrv()));
}

void SceneRenderer::copyHdr(Rg::PassContext& c, const UnderwaterTargets& d)
{
    c.cmd()->CopyResource(c.resource(d.copy), c.resource(d.hdr));
}

void SceneRenderer::drawUnderwater(Rg::PassContext& c, const Renderer& renderer, const UnderwaterTargets& d, const UnderwaterFrame& frame) const
{
    bindHdr(c, renderer, d.hdr);
    m_underwater.draw(c.cmd(), rgSrvTable(c, { d.copy, d.depth }), frame);
}

void SceneRenderer::logMissingSceneTargets(const char* entry)
{
    static bool logged = false;
    if (logged)
        return;
    logged = true;
    DE_LOG_ERROR(LogCategory::Render, "SceneRenderer: {} has no scene targets to draw into (logs once)", entry ? entry : "?");
}

void SceneRenderer::beginGBufferPass(Rg::PassContext& c, const Renderer& renderer, const GBufferTargets& d)
{
    ID3D12GraphicsCommandList* const  cmd     = c.cmd();
    const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[4] = { rgRtv(c, d.albedo), rgRtv(c, d.attrib), rgRtv(c, d.velocity), rgRtv(c, d.ao) };
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv     = cpuHandle(c.dsv(d.depth));
    // Clear values match the targets' optimized clears (SceneBuffers::create).
    cmd->ClearRenderTargetView(rtvs[0], SceneBuffers::kAlbedoClear, 0, nullptr);
    cmd->ClearRenderTargetView(rtvs[1], SceneBuffers::kAttribClear, 0, nullptr);
    cmd->ClearRenderTargetView(rtvs[2], SceneBuffers::kVelocityClear, 0, nullptr);
    cmd->ClearRenderTargetView(rtvs[3], SceneBuffers::kAoClear, 0, nullptr);
    cmd->OMSetRenderTargets(4, rtvs, FALSE, &dsv);
    cmd->RSSetViewports(1, &renderer.viewport());
    cmd->RSSetScissorRects(1, &renderer.scissor());
}

bool SceneRenderer::decalPassReady() const
{
    return m_decalPipeline.isValid() && m_decalLibrary.isValid() && m_decalGpu.isValid() && m_decalCube.valid();
}

void SceneRenderer::addDecalsPasses(FeatureBuilder& b, Renderer& renderer, const Camera3D& camera, const DeferredResolveInput& in)
{
    using Rg::PassContext;
    SceneRenderer* const              self = this;
    Renderer* const                   rr   = &renderer;
    const Camera3D* const             cam  = &camera;
    const DeferredResolveInput* const inp  = &in; // viewProj is filled by the host's GBuffer pass
    // Off, not ready, or nothing alive: no pass at all (no UAV round trip on the G-buffer).
    const bool draw = renderer.debugState().decalsEnabled && decalPassReady() && m_decalPool.aliveCount() > 0;
    if (!draw)
        storeDecalStats(0, 0, 0, 0);
    else
        addDecalPass(b,
                     [self, rr, cam, inp](PassContext& c, const DecalTargets& d)
                     {
                         if (!inp->viewProj)
                         {
                             self->storeDecalStats(0, 0, 0, 0);
                             return;
                         }
                         self->drawDecalsPass(c, *rr, *cam, *inp->viewProj, d);
                     });
    if (!addHdrClearPass(b,
                         [rr](PassContext& c, const HdrTarget& d)
                         {
                             // Must match the D3D12_CLEAR_VALUE passed at HDR resource creation (warning #820).
                             c.cmd()->ClearRenderTargetView(rgRtv(c, d.hdr), rr->hdrClearColor(), 0, nullptr);
                         }))
        b.opaque("Decals", {}, {}, [](PassContext&) { logMissingSceneTargets("Decals"); });
}

namespace
{
    bool resolveInputOk(const DeferredResolveInput& in)
    {
        if (in.viewProj && in.prevViewProj && in.gtao && in.ssr && in.lighting)
            return true;
        static bool logged = false;
        if (!logged)
        {
            logged = true;
            DE_LOG_ERROR(LogCategory::Render, "SceneRenderer: a lighting pass is missing a required input (logs once)");
        }
        return false;
    }

    void bindHdrTarget(ID3D12GraphicsCommandList* cmd, const Renderer& renderer, D3D12_CPU_DESCRIPTOR_HANDLE rtv)
    {
        cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        cmd->RSSetViewports(1, &renderer.viewport());
        cmd->RSSetScissorRects(1, &renderer.scissor());
    }
} // namespace

void SceneRenderer::addLightingPasses(FeatureBuilder& b, Renderer& renderer, const DeferredResolveInput& in)
{
    using Rg::PassBuilder;
    using Rg::PassContext;
    const LightingReads reads = lightingReads(b);
    if (!reads.valid() || !m_lighting.isValid())
    {
        b.opaque("Lighting", {}, {}, [](PassContext&) {}); // no scene buffers or PSO: nothing to light
        return;
    }
    SceneRenderer* const              self = this;
    Renderer* const                   rr   = &renderer;
    const DeferredResolveInput* const inp  = &in; // filled by the host's GBuffer pass: read at execute
    Rg::TextureHandle                 hdrOut;
    b.addPass<LightingReads>(
        "Lighting", [&](PassBuilder& pb, LightingReads& d) { hdrOut = declareLightingUses(pb, reads, true, d); },
        [self, rr, inp](PassContext& c, const LightingReads& d)
        {
            if (!resolveInputOk(*inp))
                return;
            self->m_resolveLighting = *inp->lighting;
            // The SSR bit is sampled here, as addSsrPasses drew with it.
            fillSsrLightingConstants(self->m_resolveLighting, *inp->ssr, rr->debugState().ssrEnabled, self->m_ssr.isValid());
            const LightingViews v = rr->lightingViews();
            // Register order t0..t9 (SceneBuffers::kLighting*).
            const D3D12_GPU_DESCRIPTOR_HANDLE table = rgTable(c, { v.albedo, v.attrib, rgSrvCpu(c, d.depth), v.shadow, v.height, rgSrvCpu(c, d.ao), v.iblIrradiance, v.iblPrefilter, v.iblBrdfLut,
                                                                   d.ssr.valid() ? rgSrvCpu(c, d.ssr) : rr->ssrDummyCpu() });
            bindHdrTarget(c.cmd(), *rr, rgRtv(c, d.hdr));
            self->drawDeferredLighting(c.cmd(), table, self->m_resolveLighting);
        });
    b.set(RenderResource::Hdr, hdrOut);
}

void SceneRenderer::addLocalVolumesPasses(FeatureBuilder& b, Renderer& renderer, World& world, const Camera3D& camera, const DeferredResolveInput& in)
{
    using Rg::PassBuilder;
    using Rg::PassContext;
    const LightingReads reads = lightingReads(b);
    if (!reads.valid() || !m_localLightVolumes.isValid())
    {
        b.opaque("LocalVolumes", {}, {}, [](PassContext&) {});
        return;
    }
    SceneRenderer* const              self = this;
    Renderer* const                   rr   = &renderer;
    World* const                      w    = &world;
    const Camera3D* const             cam  = &camera;
    const DeferredResolveInput* const inp  = &in;
    Rg::TextureHandle                 hdrOut;
    b.addPass<LightingReads>(
        "LocalVolumes", [&](PassBuilder& pb, LightingReads& d) { hdrOut = declareLightingUses(pb, reads, false, d); }, // additive
        [self, rr, w, cam, inp](PassContext& c, const LightingReads& d)
        {
            if (!resolveInputOk(*inp))
                return;
            // A flashlight pool is visible light. Heat is already in the emissive meshes.
            if (self->m_resolveLighting.thermal > 1.0e-4f)
                return;
            const LightingViews v = rr->lightingViews();
            // LocalLightVolumePipeline::kTable* order.
            const D3D12_GPU_DESCRIPTOR_HANDLE table = rgTable(c, { v.albedo, v.attrib, rgSrvCpu(c, d.depth), rgSrvCpu(c, d.ao), v.height, v.localShadow, v.localShadowRecords });
            bindHdrTarget(c.cmd(), *rr, rgRtv(c, d.hdr));
            self->drawLocalLights(c.cmd(), table, *rr, *w, *cam, *inp->viewProj, self->m_resolveLighting);
        });
    b.set(RenderResource::Hdr, hdrOut);
}

void SceneRenderer::drawDeferredLighting(ID3D12GraphicsCommandList* cmd, D3D12_GPU_DESCRIPTOR_HANDLE table, const LightingConstants& lc) const
{
    m_lighting.draw(cmd, table, m_shadows, lc);
}

void SceneRenderer::drawLocalLights(ID3D12GraphicsCommandList* cmd, D3D12_GPU_DESCRIPTOR_HANDLE table, Renderer& renderer, World& world, const Camera3D& camera, const Math::Matrix4f& viewProj,
                                    const LightingConstants& lc)
{
    m_localLightVolumes.draw(cmd, table, renderer, world, m_localLightGpu, m_pointVolumeMesh, m_spotVolumeMesh, camera, viewProj, lc, &m_localShadows, m_fogOccluders, m_fogOccluderCount);
}

void SceneRenderer::setFogOccluders(const Math::AABox3f* boxes, int count)
{
    if (!boxes || count <= 0)
    {
        m_fogOccluders     = nullptr;
        m_fogOccluderCount = 0;
        return;
    }
    m_fogOccluders     = boxes;
    m_fogOccluderCount = count;
}

void SceneRenderer::tickDecals(World& world, float dt)
{
    m_decalPool.tick(world, dt);
}

bool SceneRenderer::spawnDecal(const DecalSpawnDesc& desc, DecalId* outId)
{
    return m_decalPool.spawn(desc, outId);
}

bool SceneRenderer::decalsReady() const
{
    return m_decalPipeline.isValid() && m_decalLibrary.isValid() && m_decalGpu.isValid() && m_decalCube.valid();
}

DecalFrameStats SceneRenderer::decalStats() const
{
    DecalFrameStats stats = m_decalStats;
    stats.alive           = m_decalPool.aliveCount();
    stats.recycleCount    = m_decalPool.recycleCount();
    return stats;
}

void SceneRenderer::storeDecalStats(uint32_t drawn, uint32_t culled, uint32_t inside, uint32_t triangles)
{
    m_decalStats.alive        = m_decalPool.aliveCount();
    m_decalStats.drawn        = drawn;
    m_decalStats.culled       = culled;
    m_decalStats.inside       = inside;
    m_decalStats.triangles    = triangles;
    m_decalStats.recycleCount = m_decalPool.recycleCount();
}

void SceneRenderer::drawDecalVolumes(ID3D12GraphicsCommandList* cmd, Renderer& renderer, LinePipeline& lines, const Math::Matrix4f& viewProj)
{
    if (!cmd || renderer.debugState().decalsDebug != 1 || !lines.isValid() || !m_decalVolumeMesh.valid())
        return;

    DecalDebugVolume volumes[DecalPool::kCapacity];
    const uint32_t count = m_decalPool.copyAliveVolumes(volumes, DecalPool::kCapacity);
    if (count == 0)
        return;

    const GpuScope scope(cmd, "Decal Volumes", ProfileColor::Gizmos);
    lines.bind(cmd);
    for (uint32_t i = 0; i < count; ++i)
    {
        LineFrameConstants lc{};
        copyMatrix(lc.worldViewProj, volumes[i].world * viewProj);
        decalVolumeColor(volumes[i].kind, lc.color);
        lc.color[3] = 1.0f;
        lines.setConstants(cmd, lc);
        m_decalVolumeMesh.draw(cmd);
    }
}

void SceneRenderer::drawDecalsPass(Rg::PassContext& c, Renderer& renderer, const Camera3D& camera, const Math::Matrix4f& viewProj, const DecalTargets& d)
{
    ID3D12GraphicsCommandList* const cmd = c.cmd();
    if (!decalPassReady())
    {
        storeDecalStats(0, 0, 0, 0);
        return;
    }

    DecalGpuInstance outside[DecalPool::kCapacity];
    DecalGpuInstance inside[DecalPool::kCapacity];
    uint32_t outsideCount = 0;
    uint32_t insideCount  = 0;
    const Frustum3f frustum(camera.GetCullViewProj());
    m_decalPool.buildVisible(frustum, camera.GetPosition(), outside, DecalPool::kCapacity, &outsideCount, inside, DecalPool::kCapacity, &insideCount);
    const uint32_t alive = m_decalPool.aliveCount();
    const uint32_t drawn = outsideCount + insideCount;
    const uint32_t culled = alive > drawn ? alive - drawn : 0;
    // Outside boxes are 12 tris. The inside path is one fullscreen triangle.
    storeDecalStats(drawn, culled, insideCount, outsideCount * 12u + insideCount);
    if (outsideCount == 0 && insideCount == 0)
        return;

    ID3D12Device* device = renderer.device();
    if (!m_decalPipeline.syncTargets(device, renderer.albedoResource(), renderer.attribResource()))
        return;

    DecalPassConstants cb{};
    copyMatrix(cb.viewProj, viewProj);
    copyMatrix(cb.invViewProj, viewProj.Inverse());
    const uint32_t frame = renderer.frameIndex();
    m_decalPipeline.uploadPass(frame, cb);
    m_decalGpu.upload(frame, outside, outsideCount, inside, insideCount);
    DecalTables tables;
    tables.depth = rgTable(c, { rgSrvCpu(c, d.depth) });
    tables.uavs  = rgTable(c, { m_decalPipeline.albedoUavCpu(), m_decalPipeline.attribUavCpu() });
    for (uint32_t i = 0; i < static_cast<uint32_t>(DecalDefId::Count); ++i)
    {
        const DecalDefId                  id     = static_cast<DecalDefId>(i);
        const D3D12_CPU_DESCRIPTOR_HANDLE albedo = m_decalLibrary.albedoSrv(id);
        const D3D12_CPU_DESCRIPTOR_HANDLE normal = m_decalLibrary.normalSrv(id);
        if (albedo.ptr == 0 || normal.ptr == 0)
            return;
        tables.textures[i] = rgTable(c, { albedo, normal });
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = cpuHandle(c.dsv(d.depth, true));
    if (tables.depth.ptr == 0 || tables.uavs.ptr == 0 || dsv.ptr == 0)
        return;

    beginDecalEvent(cmd);
    cmd->RSSetViewports(1, &renderer.viewport());
    cmd->RSSetScissorRects(1, &renderer.scissor());
    cmd->OMSetRenderTargets(0, nullptr, FALSE, &dsv);

    const auto drawRange = [&](const DecalGpuInstance* list, uint32_t count, D3D12_GPU_VIRTUAL_ADDRESS base, bool fullscreen) -> bool
    {
        bool drew = false;
        uint32_t start = 0;
        while (start < count)
        {
            const DecalDefId id = decalDefForGpuInstance(list[start]);
            if (id == DecalDefId::Count)
            {
                ++start;
                continue;
            }
            uint32_t end = start + 1;
            while (end < count && decalDefForGpuInstance(list[end]) == id)
                ++end;
            if (drew)
                m_decalPipeline.uavBarrier(cmd);
            const D3D12_GPU_VIRTUAL_ADDRESS va = base + static_cast<UINT64>(start) * sizeof(DecalGpuInstance);
            if (fullscreen)
                m_decalPipeline.drawInside(cmd, tables, va, end - start, id);
            else
                m_decalPipeline.drawOutside(cmd, tables, m_decalCube, va, end - start, id);
            drew  = true;
            start = end;
        }
        return drew;
    };

    const bool drewOutside = drawRange(outside, outsideCount, m_decalGpu.outsideGpuVa(), false);
    bool drewInside = false;
    if (insideCount > 0)
    {
        if (drewOutside)
            m_decalPipeline.uavBarrier(cmd);
        cmd->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
        drewInside = drawRange(inside, insideCount, m_decalGpu.insideGpuVa(), true);
    }
    if (drewOutside || drewInside)
        m_decalPipeline.uavBarrier(cmd);
    cmd->EndEvent();
}

void SceneRenderer::drawCloudVolumes(ID3D12GraphicsCommandList* cmd, D3D12_GPU_DESCRIPTOR_HANDLE depthTable, Renderer& renderer, World& world, const Camera3D& camera, const Math::Matrix4f& viewProj,
                                     const CloudVolumeFrame& frame)
{
    m_cloudVolumes.draw(cmd, depthTable, renderer, world, m_cloudVolumeGpu, camera, viewProj, frame);
}

bool SceneRenderer::syncAutoExposureSize(Renderer& renderer)
{
    // Renderer::resize recreates the pyramid (one GPU drain for every graph target); this only
    // notices that through the generation and drops the stale 1x1. No wait here.
    if (!m_autoExposureEnabled || !m_autoExposure.isValid())
        return false;
    const uint64_t generation = m_autoExposure.generation();
    if (generation == m_autoExposureGeneration)
        return false;
    m_autoExposureGeneration = generation;
    m_autoExposure.invalidateReadback();
    return true;
}

void SceneRenderer::addPostPasses(FeatureBuilder& b, Renderer& renderer, const Math::Matrix4f& viewProj, TonemapSettings tonemap, bool meterAutoExposure)
{
    using Rg::PassBuilder;
    using Rg::PassContext;
    using Rg::TextureHandle;
    using Rg::Use;
    using R = RenderResource;

    // This runs while execute() builds the graph, before any pass executes. The host fills viewProj
    // in its GBuffer pass (beginCameraFrame applies the TAA jitter there), so the matrices are read
    // in the pass execs through this pointer, never here. The previous view-proj is captured now:
    // the host calls endCameraFrame after execute().
    const PostMatrices   matrices(viewProj, m_havePrevViewProj, m_prevViewProj);
    const bool           holdTemporal = m_holdTemporal;
    if (m_autoExposureEnabled)
        syncAutoExposureSize(renderer);

    Rg::RenderGraph& g      = b.graph();
    Renderer*        rr     = &renderer;
    const uint32_t   width  = renderer.width();
    const uint32_t   height = renderer.height();
    TextureHandle    hdr    = b.handle(R::Hdr);
    const TextureHandle depth    = b.handle(R::Depth);
    const TextureHandle velocity = b.handle(R::Velocity);
    TextureHandle       post     = b.handle(R::Post);
    const TextureHandle swap     = b.handle(R::Swap);
    if (!hdr.valid() || !depth.valid() || !swap.valid())
    {
        markHistoryUnwritten(m_taaHistory, "Taa");
        return;
    }

    // The umbrella profiler scope opens in the first pass and closes in Tonemap (always last). No
    // pass here can be culled: each writes an import, a history or the readback (SideEffect).
    bool postScopeOpen = false;
    auto openPost      = [&postScopeOpen]() {
        const bool first = !postScopeOpen;
        postScopeOpen    = true;
        return first;
    };

    // ---- Auto exposure: reduce -> downsample chain -> copy the 1x1 into this frame's readback.
    if (meterAutoExposure && m_autoExposure.prepare())
    {
        AutoExposurePipeline* ae     = &m_autoExposure;
        const uint32_t        levels = ae->levelCount();
        TextureHandle         level[AutoExposurePipeline::kMaxLevels]{};
        for (uint32_t i = 0; i < levels; ++i)
            level[i] = importTracked(g, AutoExposurePipeline::levelName(i), ae->levelResource(i), textureViews(ae->levelSrv(i), ae->levelRtv(i)));

        struct Reduce
        {
            TextureHandle hdr, depth, dst;
        };
        const bool openScope = openPost();
        b.addPass<Reduce>(
            "Post.AutoExposure.Reduce",
            [&](PassBuilder& pb, Reduce& d) {
                d.hdr   = pb.read(hdr, Use::PixelSrv);
                d.depth = pb.read(depth, Use::PixelSrv);
                d.dst = level[0] = pb.write(level[0], Use::RenderTarget);
            },
            [ae, width, height, openScope](PassContext& c, const Reduce& d) {
                if (openScope)
                    profileGpuBegin(c.cmd(), "Post", ProfileColor::Post);
                profileGpuBegin(c.cmd(), "Auto Exposure", ProfileColor::AutoExposure);
                ae->drawReduce(c.cmd(), rgSrvTable(c, { d.hdr, d.depth }), width, height);
            });
        for (uint32_t i = 1; i < levels; ++i)
        {
            struct Down
            {
                TextureHandle src, dst;
                uint32_t      index;
            };
            b.addPass<Down>(
                "Post.AutoExposure.Down",
                [&](PassBuilder& pb, Down& d) {
                    d.src = pb.read(level[i - 1], Use::PixelSrv);
                    d.dst = level[i] = pb.write(level[i], Use::RenderTarget);
                    d.index          = i;
                },
                [ae](PassContext& c, const Down& d) { ae->drawDownsample(c.cmd(), d.index, rgSrvTable(c, { d.src })); });
        }
        struct Readback
        {
            TextureHandle last;
        };
        b.addPass<Readback>(
            "Post.AutoExposure.Readback",
            [&](PassBuilder& pb, Readback& d) {
                pb.flags(Rg::PassFlags::SideEffect); // the CPU reads the copy next time this frame index runs
                d.last = pb.read(level[levels - 1], Use::CopySrc);
            },
            [ae, rr](PassContext& c, const Readback&) {
                ae->copyToReadback(c.cmd(), rr->frameIndex());
                profileGpuEnd(c.cmd()); // Auto Exposure
            });
    }

    // ---- Bloom: extract -> downsample -> additive upsample -> additive composite into HDR.
    if (renderer.debugState().bloom && m_bloom.prepare())
    {
        const BloomPipeline* bloom    = &m_bloom;
        const float          strength = BloomPipeline::kDefaultStrength;
        const float          exposure = tonemap.exposure;
        constexpr UINT       kMips    = BloomPipeline::kMipCount;
        TextureHandle        mip[kMips]{};
        for (UINT i = 0; i < kMips; ++i)
            mip[i] = importTracked(g, BloomPipeline::mipName(i), bloom->mipResource(i), textureViews(bloom->mipSrv(i), bloom->mipRtv(i)));

        struct Step
        {
            TextureHandle src, dst;
            uint32_t      dstW, dstH, srcW, srcH;
            BloomStep     step;
            bool          openPost, openBloom, closeBloom;
        };
        // One pass: `src` sampled, `dst` drawn (read too when additive).
        const auto addStep = [&](const char* name, BloomStep step, TextureHandle src, TextureHandle& dst, uint32_t dstW, uint32_t dstH, uint32_t srcW, uint32_t srcH,
                                 bool first, bool last) {
            const bool additive = step == BloomStep::Upsample || step == BloomStep::Composite;
            const bool openP    = first && openPost();
            b.addPass<Step>(
                name,
                [&](PassBuilder& pb, Step& d) {
                    d.src  = pb.read(src, Use::PixelSrv);
                    d.dst  = additive ? pb.readWrite(dst, Use::RenderTarget) : pb.write(dst, Use::RenderTarget);
                    dst    = d.dst;
                    d.dstW = dstW;
                    d.dstH = dstH;
                    d.srcW = srcW;
                    d.srcH = srcH;
                    d.step = step;
                    d.openPost   = openP;
                    d.openBloom  = first;
                    d.closeBloom = last;
                },
                [bloom, strength, exposure](PassContext& c, const Step& d) {
                    if (d.openPost)
                        profileGpuBegin(c.cmd(), "Post", ProfileColor::Post);
                    if (d.openBloom)
                        profileGpuBegin(c.cmd(), "Bloom", ProfileColor::Bloom);
                    bloom->drawStep(c.cmd(), d.step, rgSrvTable(c, { d.src }), rgRtv(c, d.dst), d.dstW, d.dstH, d.srcW, d.srcH, strength, exposure);
                    if (d.closeBloom)
                        profileGpuEnd(c.cmd()); // Bloom
                });
        };
        addStep("Post.Bloom.Extract", BloomStep::Extract, hdr, mip[0], bloom->mipWidth(0), bloom->mipHeight(0), width, height, true, false);
        for (UINT i = 0; i < BloomPipeline::kDownCount; ++i)
            addStep("Post.Bloom.Down", BloomStep::Downsample, mip[i], mip[i + 1], bloom->mipWidth(i + 1), bloom->mipHeight(i + 1), bloom->mipWidth(i), bloom->mipHeight(i), false,
                    false);
        for (UINT i = kMips - 1; i > 0; --i)
            addStep("Post.Bloom.Up", BloomStep::Upsample, mip[i], mip[i - 1], bloom->mipWidth(i - 1), bloom->mipHeight(i - 1), bloom->mipWidth(i), bloom->mipHeight(i), false,
                    false);
        addStep("Post.Bloom.Composite", BloomStep::Composite, mip[0], hdr, width, height, bloom->mipWidth(0), bloom->mipHeight(0), false, true);
        b.set(R::Hdr, hdr);
    }

    // ---- TAA: scene HDR + last frame's history -> post and this frame's history.
    bool                 usedPostHdr = false;
    const HistoryView    taaHistory  = wantsTaa(renderer) && renderer.targets() ? renderer.targets()->historyView(m_taaHistory) : HistoryView{};
    const TextureHandle  histCurr    = b.handle(R::TaaHistory);
    const bool           useTaa      = taaHistory.valid() && histCurr.valid() && post.valid() && velocity.valid();
    if (useTaa)
    {
        TaaSettings taa{};
        taa.blend = 0.1f;
        // A held frame (and the one after it) gets no valid prev from the graph (FrameView::holdTemporal).
        const TextureHandle histPrev = b.prev(R::TaaHistory);
        taa.reset                    = !taaHistory.prevValid || !histPrev.valid();
        // Reset: the shader ignores the history, but the table still needs a valid view. The other
        // slot's SRV (outside the graph, not read) fills it.
        const D3D12_CPU_DESCRIPTOR_HANDLE resetPrev = taaHistory.prevSrv;

        struct Taa
        {
            TextureHandle hdr, prev, velocity, depth, post, hist;
        };
        const TaaPipeline* pipe  = &m_taa;
        const bool         openP = openPost();
        TextureHandle      histOut;
        b.addPass<Taa>(
            "Post.Taa",
            [&](PassBuilder& pb, Taa& d) {
                d.hdr      = pb.read(hdr, Use::PixelSrv);
                d.prev     = histPrev.valid() ? pb.read(histPrev, Use::PixelSrv) : TextureHandle{};
                d.velocity = pb.read(velocity, Use::PixelSrv);
                d.depth    = pb.read(depth, Use::PixelSrv);
                d.post = post = pb.write(post, Use::RenderTarget);
                d.hist = histOut = pb.write(histCurr, Use::RenderTarget);
            },
            [pipe, taa, matrices, resetPrev, width, height, openP](PassContext& c, const Taa& d) {
                if (openP)
                    profileGpuBegin(c.cmd(), "Post", ProfileColor::Post);
                TaaSettings s = taa;
                matrices.fill(s.invViewProj, s.prevViewProj);
                TaaTargets t{};
                t.srvTable   = d.prev.valid() ? rgSrvTable(c, { d.hdr, d.prev, d.velocity, d.depth })
                                              : rgTable(c, { rgSrvCpu(c, d.hdr), resetPrev, rgSrvCpu(c, d.velocity), rgSrvCpu(c, d.depth) });
                t.postRtv    = rgRtv(c, d.post);
                t.historyRtv = rgRtv(c, d.hist);
                t.width      = width;
                t.height     = height;
                pipe->draw(c.cmd(), s, t);
            });
        b.set(R::Post, post);
        b.set(R::TaaHistory, histOut);
        usedPostHdr = true;
    }
    else
        markHistoryUnwritten(m_taaHistory, "Taa");

    // ---- Motion blur: post -> scene HDR after TAA, scene HDR -> post without it.
    const bool useMb = !holdTemporal && renderer.debugState().motionBlur && m_motionBlur.isValid() && velocity.valid() && post.valid();
    if (useMb)
    {
        MotionBlurSettings mb{};
        mb.strength  = 1.0f;
        mb.maxPixels = 40.0f;
        mb.readPost  = useTaa;

        struct Mb
        {
            TextureHandle src, velocity, depth, dst;
        };
        const MotionBlurPipeline* pipe  = &m_motionBlur;
        const bool                openP = openPost();
        TextureHandle&            src   = useTaa ? post : hdr;
        TextureHandle&            dst   = useTaa ? hdr : post;
        b.addPass<Mb>(
            "Post.MotionBlur",
            [&](PassBuilder& pb, Mb& d) {
                d.src      = pb.read(src, Use::PixelSrv);
                d.velocity = pb.read(velocity, Use::PixelSrv);
                d.depth    = pb.read(depth, Use::PixelSrv);
                d.dst = dst = pb.write(dst, Use::RenderTarget);
            },
            [pipe, mb, matrices, width, height, openP](PassContext& c, const Mb& d) {
                if (openP)
                    profileGpuBegin(c.cmd(), "Post", ProfileColor::Post);
                MotionBlurSettings s = mb;
                matrices.fill(s.invViewProj, s.prevViewProj);
                MotionBlurTargets t{};
                t.srvTable = rgSrvTable(c, { d.src, d.velocity, d.depth });
                t.rtv      = rgRtv(c, d.dst);
                t.width    = width;
                t.height   = height;
                pipe->draw(c.cmd(), s, t);
            });
        b.set(useTaa ? R::Hdr : R::Post, dst);
        usedPostHdr = !useTaa;
    }

    // ---- Tonemap into the back buffer (closes the umbrella scope).
    tonemap.usePostHdr = usedPostHdr;
    struct Tone
    {
        TextureHandle src, depth, dst;
    };
    const TonemapPipeline* tonePipe   = &m_tonemap;
    const bool             openP      = openPost();
    const TextureHandle    toneSource = usedPostHdr ? post : hdr;
    TextureHandle          swapOut;
    b.addPass<Tone>(
        "Post.Tonemap",
        [&](PassBuilder& pb, Tone& d) {
            d.src   = pb.read(toneSource, Use::PixelSrv);
            d.depth = pb.read(depth, Use::PixelSrv);
            d.dst = swapOut = pb.write(swap, Use::RenderTarget);
        },
        [tonePipe, tonemap, width, height, openP](PassContext& c, const Tone& d) {
            if (openP)
                profileGpuBegin(c.cmd(), "Post", ProfileColor::Post);
            TonemapTargets t{};
            t.srvTable = rgSrvTable(c, { d.src, d.depth });
            t.rtv      = rgRtv(c, d.dst);
            t.width    = width;
            t.height   = height;
            tonePipe->draw(c.cmd(), tonemap, t);
            profileGpuEnd(c.cmd()); // Post
        });
    b.set(R::Swap, swapOut);
}

} // namespace Dark
