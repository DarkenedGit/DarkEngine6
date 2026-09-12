#include "EditorApp.h"

#include "Editor/EditorInternals.h"
#include "Editor/EditorObject.h"
#include "Scene/SceneFile.h"
#include "ECS/Components.h"
#include "Network/Replication.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "Core/UiPalette.h"
#include "Ui/Icons.h"
#include "Ui/ImGuiTheme.h"
#include "Input/InputCodes.h"
#include "Collision/StaticCollision.h"
#include "Math/AABox3f.h"
#include "Math/AABox2f.h"
#include "Math/MathHelper.h"
#include "Math/Matrix4f.h"
#include "Math/Quaternion.h"
#include "Math/Sphere3f.h"
#include "Math/Vector2f.h"
#include "Math/Vector3f.h"
#include "Math/Ray3f.h"
#include "Render/LineMesh.h"
#include "Render/MeshGen.h"
#include "Render/TaaJitter.h"
#include "Render/ModelDraw.h"
#include "Assets/Model.h"
#include "Animation/AnimGraphTick.h"
#include "Animation/AnimGraphComponent.h"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace Dark;
using namespace Dark::EditorDetail;
using namespace Math;

void EditorApp::renderScene3D(ID3D12GraphicsCommandList* cmd)
{
    m_camera.ClearSubpixelJitter();
    const Vector3f lightDir(0.35f, 0.85f, -0.35f);
    AABox3f        sceneBounds(Vector3f(-22.0f, -2.0f, -22.0f), Vector3f(22.0f, 16.0f, 22.0f));
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent&) {
        if (const auto* xf = world().get<TransformComponent>(e))
            sceneBounds.ExpandToInclude(xf->position);
    });
    m_shadows.update(m_camera, lightDir, sceneBounds, 0.8f, 0.20f, renderer().frameIndex());
    if (m_shadows.isValid() && m_shadows.enabled())
    {
        m_shadows.beginCapture(cmd);
        for (int i = 0; i < m_shadows.cascadeCount(); ++i)
        {
            m_shadows.beginCascade(cmd, i);
            if (m_showSolid && m_groundMesh.valid())
                m_groundMesh.draw(cmd);
            world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
                if (!isScene3DType(so.type))
                    return;
                const auto* xf = world().get<TransformComponent>(e);
                const Mesh* mesh = meshForType(so.type);
                if (!xf || !mesh || !mesh->valid() || world().get<LocalLightComponent>(e))
                    return;
                const Matrix4f worldMat = makeWorldMatrix(*xf);
                const Matrix4f wvp      = worldMat * m_shadows.cascade(i).viewProj;
                m_shadows.pipeline().setWvp(cmd, wvp.m_afEntry);
                mesh->draw(cmd);
            });
            world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
                if (!mc.castShadow)
                    return;
                const auto* xf = world().get<TransformComponent>(e);
                const auto model = assets().getAs<Model>(mc.modelAssetID);
                if (!xf || !model || !model->valid())
                    return;
                const Matrix4f worldMat = makeWorldMatrix(*xf);
                if (model->skinned())
                {
                    const AnimPose* pose = skinnedPose(*model, world().get<AnimGraphComponent>(e));
                    if (pose)
                        drawSkinnedModelDepth(cmd, m_shadows, i, m_skinnedShadowPipeline, m_skinRing, *model, *pose, worldMat);
                    else
                        drawModelDepth(cmd, m_shadows, i, *model, worldMat);
                }
                else
                    drawModelDepth(cmd, m_shadows, i, *model, worldMat);
            });
        }
        m_shadows.endCapture(cmd);
    }
    else if (m_shadows.isValid())
    {
        m_shadows.endCapture(cmd);
    }

    const bool deferred = renderer().scenePath() == ScenePath::HybridDeferred;
    const bool useTaa   = deferred && renderer().debugState().taa && m_taa.isValid();
    if (useTaa)
    {
        float jx = 0.0f, jy = 0.0f;
        taaHaltonJitter(renderer().frameIndex(), jx, jy);
        m_camera.SetSubpixelJitter(jx, jy, renderer().width(), renderer().height());
    }
    const Matrix4f viewProj     = m_camera.GetViewProj();
    const Matrix4f prevViewProj = m_havePrevViewProj ? m_prevViewProj : viewProj;
    if (deferred)
    {
        renderer().bindGBuffer();
        renderer().clearGBuffer();
    }
    else if (renderer().hasSceneBuffers())
    {
        renderer().bindHdr(true);
        renderer().clearHdr();
    }
    else
    {
        renderer().bindSceneTargets();
    }

    const DebugFill fill = renderer().debugState().fill;
    auto drawMesh = [&](const Mesh& mesh, const Matrix4f& world, Material* material, float cr, float cg, float cb, float emissive = 0.0f) {
        m_meshPipeline.bind(cmd, fill);
        if (material && material->isValid())
            material->bind(cmd, MeshPipeline::kRootAlbedoSrv);
        const Matrix4f wvp = world * viewProj;
        if (deferred)
        {
            MeshGBufferConstants gcb{};
            copyMatrix(gcb.worldViewProj, wvp);
            copyMatrix(gcb.world, world);
            copyMatrix(gcb.prevWorldViewProj, world * prevViewProj);
            gcb.color[0] = cr;
            gcb.color[1] = cg;
            gcb.color[2] = cb;
            gcb.color[3] = emissive;
            m_meshPipeline.setGBufferConstants(cmd, gcb);
        }
        else
        {
            m_shadows.bindReceiverCbv(cmd, MeshPipeline::kRootShadowCbv);
            MeshFrameConstants cbData{};
            copyMatrix(cbData.worldViewProj, wvp);
            copyMatrix(cbData.world, world);
            cbData.color[0] = cr;
            cbData.color[1] = cg;
            cbData.color[2] = cb;
            cbData.color[3] = 1.0f;
            cbData.lightDirWS[0] = lightDir.x;
            cbData.lightDirWS[1] = lightDir.y;
            cbData.lightDirWS[2] = lightDir.z;
            cbData.ambientScale  = 0.22f;
            cbData.lightColor[0] = 1.0f;
            cbData.lightColor[1] = 0.96f;
            cbData.lightColor[2] = 0.88f;
            const Vector3f cam = m_camera.GetPosition();
            cbData.cameraPos[0] = cam.x;
            cbData.cameraPos[1] = cam.y;
            cbData.cameraPos[2] = cam.z;
            cbData.lighting     = renderer().debugState().lighting ? 1.0f : 0.0f;
            m_meshPipeline.setConstants(cmd, cbData);
        }
        mesh.draw(cmd, fill == DebugFill::Points);
    };

    if (m_showSolid && m_groundMesh.valid())
        drawMesh(m_groundMesh, Matrix4f{}, m_groundMaterial.get(), 0.45f, 0.48f, 0.52f);

    auto drawGrid = [&]() {
        if (!m_showGrid || !m_gridMesh.valid())
            return;
        LinePipeline& lines = m_linePipeline3D.isValid() ? m_linePipeline3D : m_linePipeline;
        lines.bind(cmd);
        LineFrameConstants lc{};
        copyMatrix(lc.worldViewProj, viewProj);
        lc.color[0] = 0.25f;
        lc.color[1] = 0.55f;
        lc.color[2] = 0.75f;
        lc.color[3] = 1.0f;
        lines.setConstants(cmd, lc);
        m_gridMesh.draw(cmd);
    };

    auto drawLightGizmos = [&]() {
        LinePipeline& lines = m_linePipeline3D.isValid() ? m_linePipeline3D : m_linePipeline;
        if (!lines.isValid())
            return;
        lines.bind(cmd);
        world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
            if (!isLocalLightType(so.type))
                return;
            const auto* xf    = world().get<TransformComponent>(e);
            const auto* light = world().get<LocalLightComponent>(e);
            if (!xf || !light)
                return;
            const LineMesh* gizmo = (light->type == LocalLightType::Spot) ? &m_spotLightGizmo : &m_pointLightGizmo;
            if (!gizmo->valid())
                return;
            const Matrix4f worldMat = (light->type == LocalLightType::Spot)
                ? makeSpotLightGizmoWorld(*xf, light->range, light->outerConeDeg)
                : makePointLightGizmoWorld(xf->position, light->range);
            const bool selected = m_selected.valid() && m_selected.id() == e.id();
            float      cr = light->color.x, cg = light->color.y, cb = light->color.z;
            if (selected)
            {
                cr = cr * 0.35f + 1.00f * 0.65f;
                cg = cg * 0.35f + 0.85f * 0.65f;
                cb = cb * 0.35f + 0.20f * 0.65f;
            }
            if (!light->enabled)
            {
                cr *= 0.35f;
                cg *= 0.35f;
                cb *= 0.35f;
            }
            LineFrameConstants lc{};
            copyMatrix(lc.worldViewProj, worldMat * viewProj);
            lc.color[0] = cr;
            lc.color[1] = cg;
            lc.color[2] = cb;
            lc.color[3] = 1.0f;
            lines.setConstants(cmd, lc);
            gizmo->draw(cmd);
        });
    };

    if (!deferred)
        drawGrid();

    uint32_t draws = 0;
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        if (!isScene3DType(so.type))
            return;
        const auto* xf = world().get<TransformComponent>(e);
        if (!xf)
            return;
        const Mesh* mesh = meshForType(so.type);
        if (!mesh || !mesh->valid() || world().get<LocalLightComponent>(e))
            return;
        const bool selected = m_selected.valid() && m_selected.id() == e.id();
        float cr = so.color[0], cg = so.color[1], cb = so.color[2];
        if (so.type == SceneObjectType::ParticleEmitter)
        {
            cr = 0.2f;
            cg = 0.9f;
            cb = 1.0f;
        }
        if (selected)
        {
            cr = cr * 0.55f + 1.0f * 0.45f;
            cg = cg * 0.55f + 0.85f * 0.45f;
            cb = cb * 0.55f + 0.20f * 0.45f;
        }
        float emissive = 0.0f;
        if (const MeshComponent* mc = world().get<MeshComponent>(e))
            emissive = mc->emissive;
        drawMesh(*mesh, makeWorldMatrix(*xf), m_propMaterial.get(), cr, cg, cb, emissive);
        ++draws;
    });

    if (deferred)
    {
        world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            const auto* xf = world().get<TransformComponent>(e);
            const auto model = assets().getAs<Model>(mc.modelAssetID);
            if (!xf || !model || !model->hasOpaque())
                return;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            if (model->skinned())
            {
                AnimGraphComponent* ag = world().get<AnimGraphComponent>(e);
                const AnimPose* pose = skinnedPose(*model, ag);
                if (!pose)
                    return;
                Matrix4f prevW = worldMat;
                if (ag)
                {
                    if (ag->prevWorldValid)
                        prevW = ag->prevWorld;
                    ag->prevWorld = worldMat;
                    ag->prevWorldValid = true;
                }
                drawSkinnedModelOpaqueGBuffer(cmd, m_skinnedPipeline, m_meshPipeline, m_skinRing, *model, *pose, worldMat, prevW, viewProj, prevViewProj, fill);
            }
            else
                drawModelOpaqueGBuffer(cmd, m_meshPipeline, *model, worldMat, viewProj, prevViewProj, fill);
            ++draws;
        });
    }
    else
    {
        MeshFrameConstants lit{};
        lit.lightDirWS[0] = lightDir.x;
        lit.lightDirWS[1] = lightDir.y;
        lit.lightDirWS[2] = lightDir.z;
        lit.ambientScale  = 0.22f;
        lit.lightColor[0] = 1.0f;
        lit.lightColor[1] = 0.96f;
        lit.lightColor[2] = 0.88f;
        const Vector3f cam = m_camera.GetPosition();
        lit.cameraPos[0] = cam.x;
        lit.cameraPos[1] = cam.y;
        lit.cameraPos[2] = cam.z;
        lit.lighting     = renderer().debugState().lighting ? 1.0f : 0.0f;
        world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            const auto* xf = world().get<TransformComponent>(e);
            const auto model = assets().getAs<Model>(mc.modelAssetID);
            if (!xf || !model || !model->hasOpaque())
                return;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            if (model->skinned())
            {
                AnimGraphComponent* ag = world().get<AnimGraphComponent>(e);
                const AnimPose* pose = skinnedPose(*model, ag);
                if (!pose)
                    return;
                if (ag)
                {
                    ag->prevWorld = worldMat;
                    ag->prevWorldValid = true;
                }
                drawSkinnedModelForward(cmd, m_skinnedPipeline, m_meshPipeline, m_shadows, m_skinRing, *model, false, *pose, worldMat, viewProj, lit, fill);
            }
            else
                drawModelForward(cmd, m_meshPipeline, m_shadows, *model, false, worldMat, viewProj, lit, fill);
            ++draws;
        });
    }

    if (deferred)
    {
        renderer().bindHdr(false);
        renderer().clearHdr();
        LightingConstants lc{};
        copyMatrix(lc.invViewProj, viewProj.Inverse());
        const Vector3f cam = m_camera.GetPosition();
        lc.cameraPos[0]    = cam.x;
        lc.cameraPos[1]    = cam.y;
        lc.cameraPos[2]    = cam.z;
        lc.fogDensity      = 0.0f;
        lc.lightDirWS[0]   = lightDir.x;
        lc.lightDirWS[1]   = lightDir.y;
        lc.lightDirWS[2]   = lightDir.z;
        lc.lighting        = renderer().debugState().lighting ? 1.0f : 0.0f;
        lc.lightColor[0]   = 1.0f;
        lc.lightColor[1]   = 0.96f;
        lc.lightColor[2]   = 0.88f;
        lc.emissiveGain    = 4.0f;
        lc.ambientColor[0] = 0.22f;
        lc.ambientColor[1] = 0.22f;
        lc.ambientColor[2] = 0.22f;
        m_lighting.draw(cmd, renderer(), m_shadows, lc);
        m_localLightVolumes.draw(cmd, renderer(), world(), m_localLightGpu, m_pointVolumeMesh, m_spotVolumeMesh, m_camera, viewProj, lc);
        renderer().bindHdr(true);
        drawGrid();
        drawLightGizmos();
    }
    else
    {
        drawLightGizmos();
    }

    {
        MeshFrameConstants lit{};
        lit.lightDirWS[0] = lightDir.x;
        lit.lightDirWS[1] = lightDir.y;
        lit.lightDirWS[2] = lightDir.z;
        lit.ambientScale  = 0.22f;
        lit.lightColor[0] = 1.0f;
        lit.lightColor[1] = 0.96f;
        lit.lightColor[2] = 0.88f;
        const Vector3f cam = m_camera.GetPosition();
        lit.cameraPos[0] = cam.x;
        lit.cameraPos[1] = cam.y;
        lit.cameraPos[2] = cam.z;
        lit.lighting     = renderer().debugState().lighting ? 1.0f : 0.0f;
        world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            const auto* xf = world().get<TransformComponent>(e);
            const auto model = assets().getAs<Model>(mc.modelAssetID);
            if (!xf || !model || !model->hasTranslucent())
                return;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            if (model->skinned())
            {
                const AnimPose* pose = skinnedPose(*model, world().get<AnimGraphComponent>(e));
                if (!pose)
                    return;
                drawSkinnedModelForward(cmd, m_skinnedTransparentPipeline, m_meshTransparentPipeline, m_shadows, m_skinRing, *model, true, *pose, worldMat, viewProj, lit, fill);
            }
            else
                drawModelForward(cmd, m_meshTransparentPipeline, m_shadows, *model, true, worldMat, viewProj, lit, fill);
            ++draws;
        });
    }

    world().each<EditorObjectComponent>([&](Entity, EditorObjectComponent& so) {
        if (so.type != SceneObjectType::ParticleEmitter || so.emitterIndex < 0)
            return;
        if (so.emitterIndex >= static_cast<int>(m_emitters.size()) || !m_emitters[static_cast<size_t>(so.emitterIndex)])
            return;
        ParticleEmitter& em = *m_emitters[static_cast<size_t>(so.emitterIndex)];
        m_particleRenderer.draw(cmd, m_camera, em, em.desc().additiveBlend);
        ++draws;
    });

    if (deferred)
    {
        const uint32_t bw = renderer().width();
        const uint32_t bh = renderer().height();
        if (bw != m_bloomW || bh != m_bloomH)
        {
            renderer().waitForGpu();
            if (!m_bloom.resize(renderer().device(), bw, bh))
                DE_LOG_WARN(LogCategory::Render, "EditorApp: BloomPipeline resize failed — bloom disabled");
            m_bloomW = bw;
            m_bloomH = bh;
        }
        if (renderer().debugState().bloom && m_bloom.isValid())
            m_bloom.draw(cmd, renderer(), BloomPipeline::kDefaultStrength);
    }

    bool usedPostHdr = false;
    if (useTaa)
    {
        if (renderer().width() != m_taaHistoryW || renderer().height() != m_taaHistoryH)
        {
            m_taaHistoryValid = false;
            m_taaHistoryW     = renderer().width();
            m_taaHistoryH     = renderer().height();
        }
        TaaSettings taa{};
        copyMatrix(taa.invViewProj, viewProj.Inverse());
        copyMatrix(taa.prevViewProj, prevViewProj);
        taa.blend = 0.1f;
        taa.reset = !m_taaHistoryValid;
        m_taa.draw(cmd, renderer(), taa);
        m_taaHistoryValid = true;
        usedPostHdr       = true;
    }
    const bool useMb = deferred && renderer().debugState().motionBlur && m_motionBlur.isValid();
    if (useMb)
    {
        MotionBlurSettings mb{};
        copyMatrix(mb.invViewProj, viewProj.Inverse());
        copyMatrix(mb.prevViewProj, prevViewProj);
        mb.strength  = 1.0f;
        mb.maxPixels = 40.0f;
        mb.readPost  = useTaa;
        m_motionBlur.draw(cmd, renderer(), mb);
        usedPostHdr = !useTaa;
    }

    if (renderer().hasSceneBuffers())
    {
        renderer().bindColorTargetOnly();
        const bool aces = renderer().hasSceneBuffers() && renderer().debugState().aces && renderer().debugState().lighting;
        TonemapSettings ts{};
        ts.mode       = aces ? 1.0f : 0.0f;
        ts.exposure   = 1.0f;
        ts.usePostHdr = usedPostHdr;
        m_tonemap.draw(cmd, renderer(), ts);
        if ((m_showGBuffer || m_showVelocity) && m_debugOverlay.isValid() && renderer().hasGBuffer())
        {
            m_debugOverlay.beginFrame(renderer().frameIndex());
            const LONG tile = 160;
            const LONG pad  = 12;
            if (m_showGBuffer)
            {
                const D3D12_CPU_DESCRIPTOR_HANDLE albedo = renderer().albedoSrvCpu();
                const D3D12_CPU_DESCRIPTOR_HANDLE attrib = renderer().attribSrvCpu();
                if (albedo.ptr != 0)
                    m_debugOverlay.drawColor(cmd, renderer().device(), albedo, pad, pad, tile, tile);
                if (attrib.ptr != 0)
                    m_debugOverlay.drawColor(cmd, renderer().device(), attrib, pad + tile + 8, pad, tile, tile);
            }
            renderer().transitionVelocity(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            const D3D12_CPU_DESCRIPTOR_HANDLE velocity = renderer().velocitySrvCpu();
            if (velocity.ptr != 0)
            {
                LONG x = pad;
                if (m_showGBuffer)
                    x = pad + 2 * (tile + 8);
                m_debugOverlay.drawVelocity(cmd, renderer().device(), velocity, x, pad, tile, tile, 24.0f);
            }
        }
    }

    m_prevViewProj     = viewProj;
    m_havePrevViewProj = true;
    m_camera.ClearSubpixelJitter();
    renderer().stats().drawCalls = draws;
}
