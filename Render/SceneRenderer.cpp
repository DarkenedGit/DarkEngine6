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
    if (!m_motionBlur.create(renderer.device()))
        DE_LOG_WARN(LogCategory::Render, "{}: MotionBlurPipeline create failed — motion blur disabled", tag);
    if (!m_taa.create(renderer.device()))
        DE_LOG_WARN(LogCategory::Render, "{}: TaaPipeline create failed — TAA disabled", tag);

    return true;
}

bool SceneRenderer::createWorldEnvPipelines(Renderer& renderer, const char* tag)
{
    if (!m_terrainPipeline.create(renderer.device(), liveTerrainPass(renderer)))
    {
        DE_LOG_FATAL("{}: TerrainPipeline create failed", tag);
        return false;
    }
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
    if (desc.createWorldEnvironment && !createWorldEnvPipelines(renderer, tag))
        return false;
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
    m_initialized              = false;
}

bool SceneRenderer::wantsTaa(const Renderer& renderer) const
{
    return renderer.scenePath() == ScenePath::HybridDeferred && renderer.debugState().taa && m_taa.isValid();
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
