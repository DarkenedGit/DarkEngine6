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
#include "Render/MaterialSurface.h"
#include "Render/GpuUpload.h"
#include "Render/Frustum3f.h"
#include "Assets/Material.h"
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
    GpuResourceCache& gpu = renderer().gpuResources();
    m_camera.ClearSubpixelJitter();
    Vector3f lightDir{};
    Vector3f sunColor{};
    Vector3f ambientColor{};
    gatherEditorLighting(lightDir, sunColor, ambientColor);
    const float ambientScale = (ambientColor.x + ambientColor.y + ambientColor.z) * (1.0f / 3.0f);
    const bool drawTerrain = m_haveTerrain && m_terrainMaterial.isValid() && m_scene.terrainPipeline().isValid();
    AABox3f sceneBounds(Vector3f(-22.0f, -2.0f, -22.0f), Vector3f(22.0f, 16.0f, 22.0f));
    if (drawTerrain)
        sceneBounds.ExpandToInclude(m_terrain.bounds());
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
            if (drawTerrain)
            {
                const Frustum3f casterFrustum(m_shadows.cascade(i).viewProj);
                m_terrain.drawDepth(cmd, &casterFrustum);
            }
            else if (m_showSolid && m_groundMesh.valid())
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
                if (!xf || !model || !model->valid() || !gpu.ensureModel(model))
                    return;
                const Matrix4f worldMat = makeWorldMatrix(*xf);
                if (model->skinned())
                {
                    const AnimPose* pose = skinnedPose(*model, world().get<AnimGraphComponent>(e));
                    if (pose)
                        drawSkinnedModelDepth(cmd, gpu, m_shadows, i, m_skinnedShadowPipeline, m_skinRing, *model, *pose, worldMat);
                    else
                        drawModelDepth(cmd, gpu, m_shadows, i, *model, worldMat);
                }
                else
                    drawModelDepth(cmd, gpu, m_shadows, i, *model, worldMat);
            });
        }
        m_shadows.endCapture(cmd);
    }
    else if (m_shadows.isValid())
    {
        m_shadows.endCapture(cmd);
    }

    const bool deferred = renderer().scenePath() == ScenePath::HybridDeferred;
    const Matrix4f viewProj = m_scene.beginCameraFrame(m_camera, renderer());
    const Matrix4f prevViewProj = m_scene.havePrevViewProj() ? m_scene.prevViewProj() : viewProj;
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
            gpu.bindMaterial(cmd, *material, MeshPipeline::kRootAlbedoSrv);
        const Matrix4f wvp = world * viewProj;
        if (deferred)
        {
            MeshGBufferConstants gcb{};
            copyMatrix(gcb.worldViewProj, wvp);
            copyMatrix(gcb.world, world);
            copyMatrix(gcb.prevWorldViewProj, world * prevViewProj);
            if (material && material->isValid())
                applyMaterialSurface(*material, gcb);
            gcb.color[0] = cr;
            gcb.color[1] = cg;
            gcb.color[2] = cb;
            gcb.color[3] = Max(emissive, material && material->isValid() ? material->emissive() : 0.0f);
            m_meshPipeline.setGBufferConstants(cmd, gcb);
        }
        else
        {
            m_shadows.bindReceiverCbv(cmd, MeshPipeline::kRootShadowCbv);
            MeshFrameConstants cbData{};
            copyMatrix(cbData.worldViewProj, wvp);
            copyMatrix(cbData.world, world);
            if (material && material->isValid())
                applyMaterialSurface(*material, cbData);
            cbData.color[0] = cr;
            cbData.color[1] = cg;
            cbData.color[2] = cb;
            cbData.color[3] = 1.0f;
            cbData.lightDirWS[0] = lightDir.x;
            cbData.lightDirWS[1] = lightDir.y;
            cbData.lightDirWS[2] = lightDir.z;
            cbData.ambientScale  = ambientScale;
            cbData.lightColor[0] = sunColor.x;
            cbData.lightColor[1] = sunColor.y;
            cbData.lightColor[2] = sunColor.z;
            const Vector3f cam = m_camera.GetPosition();
            cbData.cameraPos[0] = cam.x;
            cbData.cameraPos[1] = cam.y;
            cbData.cameraPos[2] = cam.z;
            cbData.lighting     = renderer().debugState().lightingActive() ? 1.0f : 0.0f;
            m_meshPipeline.setConstants(cmd, cbData);
        }
        mesh.draw(cmd, fill == DebugFill::Points);
    };

    if (drawTerrain)
    {
        const Frustum3f frustum(m_camera.GetCullViewProj());
        if (deferred)
            m_terrain.drawGBuffer(cmd, m_scene.terrainPipeline(), m_terrainMaterial, m_camera, &frustum, &renderer().debugState(), &prevViewProj);
        else
            m_terrain.draw(cmd, m_scene.terrainPipeline(), m_terrainMaterial, m_camera, &frustum, nullptr, &m_shadows, &renderer().debugState());
    }
    else if (m_showSolid && m_groundMesh.valid() && m_groundMaterial)
    {
        const float* c = m_groundMaterial->baseColor();
        drawMesh(m_groundMesh, Matrix4f{}, m_groundMaterial.get(), c[0], c[1], c[2]);
    }

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
            if (!m_selected.valid() || m_selected.id() != e.id())
                return;
            const auto* xf = world().get<TransformComponent>(e);
            if (!xf)
                return;

            const LineMesh* gizmo = nullptr;
            Matrix4f        worldMat{};
            float           cr = so.color[0], cg = so.color[1], cb = so.color[2];
            if (isLocalLightType(so.type) || (so.type == SceneObjectType::ParticleEmitter && world().get<LocalLightComponent>(e)))
            {
                const auto* light = world().get<LocalLightComponent>(e);
                if (!light)
                    return;
                gizmo = (light->type == LocalLightType::Spot) ? &m_spotLightGizmo : &m_pointLightGizmo;
                worldMat = (light->type == LocalLightType::Spot)
                    ? makeSpotLightGizmoWorld(*xf, light->range, light->outerConeDeg)
                    : makePointLightGizmoWorld(xf->position, light->range);
                cr = light->color.x;
                cg = light->color.y;
                cb = light->color.z;
                if (!light->enabled)
                {
                    cr *= 0.35f;
                    cg *= 0.35f;
                    cb *= 0.35f;
                }
            }
            else if (so.type == SceneObjectType::DirectionalLight)
            {
                if (!m_dirLightGizmo.valid())
                    return;
                gizmo    = &m_dirLightGizmo;
                worldMat = Matrix4f::ScaleMatrixXYZ(1.0f, 1.0f, 6.0f) * xf->rotation.ToMatrix4()
                    * Matrix4f::TranslationMatrix(xf->position.x, xf->position.y, xf->position.z);
                if (const auto* dir = world().get<DirectionalLightComponent>(e))
                {
                    cr = dir->color.x;
                    cg = dir->color.y;
                    cb = dir->color.z;
                    if (!dir->enabled)
                    {
                        cr *= 0.35f;
                        cg *= 0.35f;
                        cb *= 0.35f;
                    }
                }
            }
            else
                return;
            if (!gizmo || !gizmo->valid())
                return;
            const bool selected = true;
            if (selected)
            {
                cr = cr * 0.35f + 1.00f * 0.65f;
                cg = cg * 0.35f + 0.85f * 0.65f;
                cb = cb * 0.35f + 0.20f * 0.65f;
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
        if (selected)
        {
            cr = cr * 0.55f + 1.0f * 0.45f;
            cg = cg * 0.55f + 0.85f * 0.45f;
            cb = cb * 0.55f + 0.20f * 0.45f;
        }
        float     emissive = 0.0f;
        Material* material = m_propMaterial.get();
        if (const MeshComponent* mc = world().get<MeshComponent>(e))
        {
            emissive = mc->emissive;
            if (auto mat = assets().getAs<Material>(mc->matAssetID))
            {
                gpu.ensureMaterial(mat);
                material = mat.get();
            }
        }
        drawMesh(*mesh, makeWorldMatrix(*xf), material, cr, cg, cb, emissive);
        ++draws;
    });

    if (deferred)
    {
        world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            const auto* xf = world().get<TransformComponent>(e);
            const auto model = assets().getAs<Model>(mc.modelAssetID);
            if (!xf || !model || !model->hasOpaque() || !gpu.ensureModel(model))
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
                drawSkinnedModelOpaqueGBuffer(cmd, gpu, m_skinnedPipeline, m_meshPipeline, m_skinRing, *model, *pose, worldMat, prevW, viewProj, prevViewProj, fill);
            }
            else
                drawModelOpaqueGBuffer(cmd, gpu, m_meshPipeline, *model, worldMat, viewProj, prevViewProj, fill);
            ++draws;
        });
    }
    else
    {
        MeshFrameConstants lit{};
        lit.lightDirWS[0] = lightDir.x;
        lit.lightDirWS[1] = lightDir.y;
        lit.lightDirWS[2] = lightDir.z;
        lit.ambientScale  = ambientScale;
        lit.lightColor[0] = sunColor.x;
        lit.lightColor[1] = sunColor.y;
        lit.lightColor[2] = sunColor.z;
        const Vector3f cam = m_camera.GetPosition();
        lit.cameraPos[0] = cam.x;
        lit.cameraPos[1] = cam.y;
        lit.cameraPos[2] = cam.z;
        lit.lighting     = renderer().debugState().lightingActive() ? 1.0f : 0.0f;
        world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            const auto* xf = world().get<TransformComponent>(e);
            const auto model = assets().getAs<Model>(mc.modelAssetID);
            if (!xf || !model || !model->hasOpaque() || !gpu.ensureModel(model))
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
                drawSkinnedModelForward(cmd, gpu, m_skinnedPipeline, m_meshPipeline, m_shadows, m_skinRing, *model, false, *pose, worldMat, viewProj, lit, fill);
            }
            else
                drawModelForward(cmd, gpu, m_meshPipeline, m_shadows, *model, false, worldMat, viewProj, lit, fill);
            ++draws;
        });
    }

    if (deferred)
    {
        renderer().bindHdr(false);
        renderer().clearHdr();
        m_scene.applyGtao(cmd, renderer(), m_camera, prevViewProj, m_ssao);
        m_scene.applySsr(cmd, renderer(), m_camera, prevViewProj, m_ssr);
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
        lc.lighting        = renderer().debugState().lightingActive() ? 1.0f : 0.0f;
        lc.lightColor[0]   = sunColor.x;
        lc.lightColor[1]   = sunColor.y;
        lc.lightColor[2]   = sunColor.z;
        lc.pbrLightColor[0] = sunColor.x;
        lc.pbrLightColor[1] = sunColor.y;
        lc.pbrLightColor[2] = sunColor.z;
        lc.emissiveGain    = 4.0f;
        lc.ambientColor[0] = ambientColor.x;
        lc.ambientColor[1] = ambientColor.y;
        lc.ambientColor[2] = ambientColor.z;
        fillIblLightingConstants(lc, m_ibl, renderer().debugState().iblEnabled, renderer().debugState().iblDebug, iblGpuReady(renderer().gpuResources(), m_iblImageId));
        m_lighting.draw(cmd, renderer(), m_shadows, lc);
        m_localLightVolumes.draw(cmd, renderer(), world(), m_localLightGpu, m_pointVolumeMesh, m_spotVolumeMesh, m_camera, viewProj, lc);
        renderer().bindHdr(true);
        m_scene.captureSsrSceneColor(cmd, renderer());
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
        lit.ambientScale  = ambientScale;
        lit.lightColor[0] = sunColor.x;
        lit.lightColor[1] = sunColor.y;
        lit.lightColor[2] = sunColor.z;
        const Vector3f cam = m_camera.GetPosition();
        lit.cameraPos[0] = cam.x;
        lit.cameraPos[1] = cam.y;
        lit.cameraPos[2] = cam.z;
        lit.lighting     = renderer().debugState().lightingActive() ? 1.0f : 0.0f;
        world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            const auto* xf = world().get<TransformComponent>(e);
            const auto model = assets().getAs<Model>(mc.modelAssetID);
            if (!xf || !model || !model->hasTranslucent() || !gpu.ensureModel(model))
                return;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            if (model->skinned())
            {
                const AnimPose* pose = skinnedPose(*model, world().get<AnimGraphComponent>(e));
                if (!pose)
                    return;
                drawSkinnedModelForward(cmd, gpu, m_skinnedTransparentPipeline, m_meshTransparentPipeline, m_shadows, m_skinRing, *model, true, *pose, worldMat, viewProj, lit, fill);
            }
            else
                drawModelForward(cmd, gpu, m_meshTransparentPipeline, m_shadows, *model, true, worldMat, viewProj, lit, fill);
            ++draws;
        });
    }

    m_particleRenderer.beginFrame(renderer().frameIndex());
    world().each<ParticleEmitterComponent>([&](Entity, ParticleEmitterComponent& pe) {
        if (!pe.runtime)
            return;
        const Material* sprite = nullptr;
        if (AssetRef<Material> mat = assets().getAs<Material>(pe.matAssetID))
        {
            gpu.ensureMaterial(mat);
            sprite = mat.get();
        }
        m_particleRenderer.draw(cmd, m_camera, *pe.runtime, pe.runtime->desc().additiveBlend, sprite);
        ++draws;
    });

    {
        const bool aces = renderer().hasSceneBuffers() && renderer().debugState().aces && renderer().debugState().lightingActive();
        TonemapSettings ts{};
        ts.mode     = aces ? 1.0f : 0.0f;
        ts.exposure = 1.0f;
        m_scene.applyPost(renderer(), cmd, viewProj, ts);
    }

    if (renderer().hasSceneBuffers())
    {
        const bool ssaoTile = renderer().debugState().ssaoDebug == 1;
        const bool ssrTile  = renderer().debugState().ssrDebug != 0;
        if ((m_showGBuffer || m_showVelocity || ssaoTile || ssrTile) && m_debugOverlay.isValid() && renderer().hasGBuffer())
        {
            // Unbind DSV / HDR so we can sample G-buffer. Overlay copies from FLAG_NONE CPU SRVs.
            renderer().bindColorTargetOnly();
            renderer().transitionAlbedo(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            renderer().transitionAttrib(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            renderer().transitionVelocity(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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
            const D3D12_CPU_DESCRIPTOR_HANDLE velocity = renderer().velocitySrvCpu();
            if ((m_showGBuffer || m_showVelocity) && velocity.ptr != 0)
            {
                LONG x = pad;
                if (m_showGBuffer)
                    x = pad + 2 * (tile + 8);
                m_debugOverlay.drawVelocity(cmd, renderer().device(), velocity, x, pad, tile, tile, 24.0f);
            }
            if (ssaoTile)
            {
                m_scene.gtao().transitionAoFull(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                const D3D12_CPU_DESCRIPTOR_HANDLE aoFull = m_scene.gtao().aoFullSrvCpu();
                if (aoFull.ptr != 0)
                {
                    LONG x = pad;
                    if (m_showGBuffer)
                        x += 2 * (tile + 8);
                    if (m_showGBuffer || m_showVelocity)
                        x += tile + 8;
                    m_debugOverlay.draw2D(cmd, renderer().device(), aoFull, x, pad, tile, tile, 1.0f, false);
                }
            }
            if (ssrTile)
            {
                LONG x = pad;
                if (m_showGBuffer)
                    x += 2 * (tile + 8);
                if (m_showGBuffer || m_showVelocity)
                    x += tile + 8;
                if (ssaoTile)
                    x += tile + 8;
                if (renderer().debugState().ssrDebug == 1)
                {
                    m_scene.ssr().transitionFull(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    const D3D12_CPU_DESCRIPTOR_HANDLE full = m_scene.ssr().fullSrvCpu();
                    if (full.ptr != 0)
                        m_debugOverlay.drawColor(cmd, renderer().device(), full, x, pad, tile, tile);
                }
                else
                {
                    m_scene.ssr().transitionDebug(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    const D3D12_CPU_DESCRIPTOR_HANDLE conf = m_scene.ssr().debugConfSrvCpu();
                    if (conf.ptr != 0)
                        m_debugOverlay.draw2D(cmd, renderer().device(), conf, x, pad, tile, tile, 1.0f, false);
                }
            }
            cmd->RSSetViewports(1, &renderer().viewport());
            cmd->RSSetScissorRects(1, &renderer().scissor());
        }
    }

    m_scene.endCameraFrame(m_camera, viewProj);
    renderer().stats().drawCalls = draws;
}
