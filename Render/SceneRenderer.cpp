#include "Render/SceneRenderer.h"

#include "Core/Log.h"
#include "ECS/World.h"
#include "Render/MeshGen.h"
#include "Render/Renderer.h"
#include "Render/TaaJitter.h"
#include "Math/MathHelper.h"

#include <cstring>

namespace Dark
{
namespace
{
MeshPass liveMeshPass(const Renderer& r)
{
    return r.scenePath() == ScenePath::HybridDeferred ? MeshPass::GBuffer : MeshPass::ForwardUnorm;
}

TerrainPass liveTerrainPass(const Renderer& r)
{
    return r.scenePath() == ScenePath::HybridDeferred ? TerrainPass::GBuffer : TerrainPass::ForwardUnorm;
}

SkyPass liveSkyPass(const Renderer& r)
{
    return r.scenePath() == ScenePath::HybridDeferred ? SkyPass::DeferredLast : SkyPass::ForwardFirst;
}

void copyMatrix(float dst[16], const Math::Matrix4f& m)
{
    std::memcpy(dst, m.m_afEntry, sizeof(float) * 16);
}
} // namespace

bool SceneRenderer::createCorePipelines(Renderer& renderer, const char* tag)
{
    if (!m_meshPipeline.create(renderer.device(), liveMeshPass(renderer)))
    {
        DE_LOG_FATAL("{}: MeshPipeline create failed", tag);
        return false;
    }
    if (!m_meshTransparentPipeline.create(renderer.device(), MeshPass::ForwardTransparent, renderer.sceneColorFormat()))
    {
        DE_LOG_FATAL("{}: transparent MeshPipeline create failed", tag);
        return false;
    }

    const SkinnedMeshPass skinnedPass =
        renderer.scenePath() == ScenePath::HybridDeferred ? SkinnedMeshPass::GBuffer : SkinnedMeshPass::Forward;
    if (!m_skinnedPipeline.create(renderer.device(), skinnedPass))
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
    if (!m_debugOverlay.create(renderer.device()))
        DE_LOG_WARN("{}: DebugOverlay create failed — depth/shadow tiles disabled", tag);

    return true;
}

bool SceneRenderer::createPostPipelines(Renderer& renderer, const char* tag)
{
    if (!renderer.hasSceneBuffers())
        return true;

    if (!m_tonemap.create(renderer.device()))
    {
        DE_LOG_FATAL("{}: TonemapPipeline create failed", tag);
        return false;
    }
    if (!m_ssr.create(renderer.device(), renderer.width(), renderer.height()))
        DE_LOG_WARN(LogCategory::Render, "{}: SsrPipeline create failed — SSR disabled", tag);
    else
    {
        m_ssrW = renderer.width();
        m_ssrH = renderer.height();
    }
    if (renderer.scenePath() != ScenePath::HybridDeferred)
        return true;

    if (!m_lighting.create(renderer.device()))
    {
        DE_LOG_FATAL("{}: DeferredLightingPipeline create failed", tag);
        return false;
    }
    if (!m_localLightVolumes.create(renderer.device()))
        DE_LOG_ERROR(LogCategory::Render, "{}: LocalLightVolumePipeline create failed — local lights disabled", tag);
    if (!m_localLightGpu.create(renderer.device()))
        DE_LOG_ERROR(LogCategory::Render, "{}: LocalLightGpuList create failed — local lights disabled", tag);

    MeshData sphereData;
    MeshData coneData;
    if (!CreateIcosahedronBounding(sphereData, 1.0f, 1) || !Mesh::tryCreate(renderer, sphereData, m_pointVolumeMesh))
        DE_LOG_ERROR(LogCategory::Render, "{}: point volume mesh failed — local lights skipped", tag);
    if (!CreateSpotVolumeCone(coneData, 16, true) || !Mesh::tryCreate(renderer, coneData, m_spotVolumeMesh))
        DE_LOG_ERROR(LogCategory::Render, "{}: spot volume mesh failed — local lights skipped", tag);

    if (!m_bloom.create(renderer.device(), renderer.width(), renderer.height()))
        DE_LOG_WARN(LogCategory::Render, "{}: BloomPipeline create failed — bloom disabled", tag);
    else
    {
        m_bloomW = renderer.width();
        m_bloomH = renderer.height();
    }
    if (!m_gtao.create(renderer.device(), renderer.width(), renderer.height()))
        DE_LOG_WARN(LogCategory::Render, "{}: GtaoPipeline create failed — SSAO disabled", tag);
    else
    {
        m_gtaoW = renderer.width();
        m_gtaoH = renderer.height();
    }
    if (!m_motionBlur.create(renderer.device()))
        DE_LOG_WARN(LogCategory::Render, "{}: MotionBlurPipeline create failed — motion blur disabled", tag);
    if (!m_taa.create(renderer.device()))
        DE_LOG_WARN(LogCategory::Render, "{}: TaaPipeline create failed — TAA disabled", tag);

    return true;
}

bool SceneRenderer::createTerrainPipelineOnly(Renderer& renderer, const char* tag)
{
    if (!m_terrainPipeline.create(renderer.device(), liveTerrainPass(renderer)))
    {
        DE_LOG_FATAL("{}: TerrainPipeline create failed", tag);
        return false;
    }
    return true;
}

bool SceneRenderer::createWorldEnvPipelines(Renderer& renderer, const char* tag)
{
    if (!createTerrainPipelineOnly(renderer, tag))
        return false;
    if (!m_waterPipeline.create(renderer.device(), renderer.sceneColorFormat()))
    {
        DE_LOG_FATAL("{}: WaterPipeline create failed", tag);
        return false;
    }
    if (!m_skyPipeline.create(renderer.device(), liveSkyPass(renderer), renderer.sceneColorFormat()))
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
    m_initialized = true;
    return true;
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
    m_pointVolumeMesh          = Mesh{};
    m_spotVolumeMesh           = Mesh{};
    m_bloom                    = BloomPipeline{};
    m_gtao                     = GtaoPipeline{};
    m_ssr                      = SsrPipeline{};
    m_motionBlur               = MotionBlurPipeline{};
    m_taa                      = TaaPipeline{};
    m_shadows                  = ShadowSystem{};
    m_debugOverlay             = DebugOverlay{};
    m_terrainPipeline          = TerrainPipeline{};
    m_waterPipeline            = WaterPipeline{};
    m_skyPipeline              = SkyPipeline{};
    m_haveWorldEnv             = false;
    m_prevViewProj             = Math::Matrix4f{};
    m_havePrevViewProj         = false;
    m_taaHistoryValid          = false;
    m_taaHistoryW              = 0;
    m_taaHistoryH              = 0;
    m_bloomW                   = 0;
    m_bloomH                   = 0;
    m_gtaoW                    = 0;
    m_gtaoH                    = 0;
    m_gtaoWasEnabled           = false;
    m_gtaoNeedReset            = true;
    m_ssrW                     = 0;
    m_ssrH                     = 0;
    m_ssrWasEnabled            = false;
    m_ssrNeedReset             = true;
    m_initialized              = false;
}

bool SceneRenderer::wantsTaa(const Renderer& renderer) const
{
    return renderer.scenePath() == ScenePath::HybridDeferred && renderer.debugState().taa && m_taa.isValid();
}

void SceneRenderer::ensureGtaoSize(Renderer& renderer, ID3D12GraphicsCommandList* cmd)
{
    if (renderer.scenePath() != ScenePath::HybridDeferred)
        return;
    const uint32_t w = renderer.width();
    const uint32_t h = renderer.height();
    if (w == m_gtaoW && h == m_gtaoH)
        return;
    renderer.waitForGpu();
    if (!m_gtao.resize(renderer.device(), w, h))
        DE_LOG_WARN(LogCategory::Render, "SceneRenderer: GtaoPipeline resize failed — SSAO disabled");
    m_gtaoW         = w;
    m_gtaoH         = h;
    m_gtaoNeedReset = true;
    if (cmd)
        m_gtao.clearAoFullIdentity(cmd);
}

void SceneRenderer::ensureSsrSize(Renderer& renderer, ID3D12GraphicsCommandList* cmd)
{
    (void)cmd;
    if (!renderer.hasSceneBuffers())
        return;
    const uint32_t w = renderer.width();
    const uint32_t h = renderer.height();
    if (w == m_ssrW && h == m_ssrH)
        return;
    renderer.waitForGpu();
    if (!m_ssr.resize(renderer.device(), w, h))
        DE_LOG_WARN(LogCategory::Render, "SceneRenderer: SsrPipeline resize failed — SSR disabled");
    m_ssrW          = w;
    m_ssrH          = h;
    m_ssrNeedReset  = true;
}

Math::Matrix4f SceneRenderer::beginCameraFrame(Camera3D& camera, Renderer& renderer)
{
    ensureGtaoSize(renderer);
    ensureSsrSize(renderer);
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

void SceneRenderer::applyGtao(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const Camera3D& camera, const Math::Matrix4f& prevViewProj,
                             const GtaoSettings& settings)
{
    ensureGtaoSize(renderer, cmd);

    GtaoSettings drawSettings = settings;
    drawSettings.enabled      = settings.enabled && renderer.debugState().ssaoEnabled;

    const bool skip = !cmd || renderer.scenePath() != ScenePath::HybridDeferred || !renderer.hasGBuffer() || !drawSettings.enabled || !m_gtao.isValid()
        || renderer.device() == nullptr || renderer.depthSrvCpu().ptr == 0 || renderer.attribSrvCpu().ptr == 0 || renderer.velocitySrvCpu().ptr == 0
        || renderer.aoSrvCpu().ptr == 0;
    if (skip)
    {
        // Restore authored MRT3. Do not bind a white SSAO tex (compose cleared to 1 would wipe ORM cavities).
        renderer.setLightingAoSrv(renderer.aoSrvCpu());
        m_gtaoWasEnabled = false;
        if (cmd)
            m_gtao.clearAoFullIdentity(cmd); // overlay-when-off samples identity, not stale/garbage
        return;
    }

    const bool resetHistory = !m_gtaoWasEnabled || m_gtaoNeedReset || !m_havePrevViewProj || (wantsTaa(renderer) && !m_taaHistoryValid);
    m_gtaoWasEnabled        = true;
    m_gtaoNeedReset         = false;

    cmd->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    m_gtao.draw(cmd, renderer, camera, prevViewProj, drawSettings, resetHistory);

    const D3D12_CPU_DESCRIPTOR_HANDLE compose = m_gtao.composeSrvCpu();
    if (compose.ptr == 0)
    {
        renderer.setLightingAoSrv(renderer.aoSrvCpu());
        return;
    }
    renderer.setLightingAoSrv(compose);
    renderer.bindHdr(false);
}

void SceneRenderer::applySsr(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const Camera3D& camera, const Math::Matrix4f& prevViewProj,
                             const SsrSettings& settings)
{
    ensureSsrSize(renderer, cmd);

    SsrSettings drawSettings = settings;
    drawSettings.enabled     = settings.enabled && renderer.debugState().ssrEnabled;

    const bool skip = !cmd || renderer.scenePath() != ScenePath::HybridDeferred || !renderer.hasGBuffer() || !drawSettings.enabled || !m_ssr.isValid()
        || renderer.device() == nullptr || renderer.depthSrvCpu().ptr == 0 || renderer.attribSrvCpu().ptr == 0 || renderer.velocitySrvCpu().ptr == 0;
    if (skip)
    {
        renderer.setLightingSsrSrv(renderer.ssrDummyCpu());
        m_ssrWasEnabled = false;
        if (cmd)
            renderer.bindHdr(false);
        return;
    }

    const bool resetHistory = !m_ssrWasEnabled || m_ssrNeedReset || !m_havePrevViewProj || (wantsTaa(renderer) && !m_taaHistoryValid);
    m_ssrWasEnabled         = true;
    m_ssrNeedReset          = false;

    cmd->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    m_ssr.draw(cmd, renderer, camera, prevViewProj, drawSettings, resetHistory);

    const D3D12_CPU_DESCRIPTOR_HANDLE full = m_ssr.fullSrvCpu();
    renderer.setLightingSsrSrv(full.ptr != 0 ? full : renderer.ssrDummyCpu());
    renderer.bindHdr(false);
}

void SceneRenderer::captureSsrSceneColor(ID3D12GraphicsCommandList* cmd, Renderer& renderer)
{
    ensureSsrSize(renderer, cmd);
    if (!cmd || !m_ssr.isValid() || !renderer.hasSceneBuffers())
        return;
    m_ssr.captureSceneColor(cmd, renderer);
}

void SceneRenderer::drawDeferredLighting(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const LightingConstants& lc) const
{
    m_lighting.draw(cmd, renderer, m_shadows, lc);
}

void SceneRenderer::drawLocalLights(
    ID3D12GraphicsCommandList* cmd,
    Renderer&                 renderer,
    World&                    world,
    const Camera3D&           camera,
    const Math::Matrix4f&     viewProj,
    const LightingConstants&  lc)
{
    m_localLightVolumes.draw(cmd, renderer, world, m_localLightGpu, m_pointVolumeMesh, m_spotVolumeMesh, camera, viewProj, lc);
}

void SceneRenderer::applyPost(Renderer& renderer, ID3D12GraphicsCommandList* cmd, const Math::Matrix4f& viewProj, TonemapSettings tonemap)
{
    const bool deferred = renderer.scenePath() == ScenePath::HybridDeferred;
    const Math::Matrix4f prevViewProj = m_havePrevViewProj ? m_prevViewProj : viewProj;

    if (deferred)
    {
        ensureGtaoSize(renderer, cmd);
        const uint32_t bw = renderer.width();
        const uint32_t bh = renderer.height();
        if (bw != m_bloomW || bh != m_bloomH)
        {
            renderer.waitForGpu();
            if (!m_bloom.resize(renderer.device(), bw, bh))
                DE_LOG_WARN(LogCategory::Render, "SceneRenderer: BloomPipeline resize failed — bloom disabled");
            m_bloomW = bw;
            m_bloomH = bh;
        }
        if (renderer.debugState().bloom && m_bloom.isValid())
            m_bloom.draw(cmd, renderer, BloomPipeline::kDefaultStrength);
    }

    bool usedPostHdr = false;
    const bool useTaa = wantsTaa(renderer);
    if (useTaa)
    {
        if (renderer.width() != m_taaHistoryW || renderer.height() != m_taaHistoryH)
        {
            m_taaHistoryValid = false;
            m_taaHistoryW     = renderer.width();
            m_taaHistoryH     = renderer.height();
        }
        TaaSettings taa{};
        copyMatrix(taa.invViewProj, viewProj.Inverse());
        copyMatrix(taa.prevViewProj, prevViewProj);
        taa.blend = 0.1f;
        taa.reset = !m_taaHistoryValid;
        m_taa.draw(cmd, renderer, taa);
        m_taaHistoryValid = true;
        usedPostHdr       = true;
    }

    const bool useMb = deferred && renderer.debugState().motionBlur && m_motionBlur.isValid();
    if (useMb)
    {
        MotionBlurSettings mb{};
        copyMatrix(mb.invViewProj, viewProj.Inverse());
        copyMatrix(mb.prevViewProj, prevViewProj);
        mb.strength  = 1.0f;
        mb.maxPixels = 40.0f;
        mb.readPost  = useTaa;
        m_motionBlur.draw(cmd, renderer, mb);
        usedPostHdr = !useTaa;
    }

    if (renderer.hasSceneBuffers())
    {
        renderer.bindColorTargetOnly();
        tonemap.usePostHdr = usedPostHdr;
        m_tonemap.draw(cmd, renderer, tonemap);
    }
}

} // namespace Dark
