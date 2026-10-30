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
#include "Render/PassTable.h"
#include "Render/Profile.h"
#include "Render/MaterialSurface.h"
#include "Render/GpuUpload.h"
#include "Render/Frustum3f.h"
#include "Assets/Material.h"
#include "Assets/Model.h"
#include "Character/HealthComponent.h"
#include "Gameplay/ThrowableBelt.h"
#include "Weapons/WeaponLoadoutComponent.h"
#include "Animation/AnimGraphTick.h"
#include "Animation/AnimGraphComponent.h"
#include "Sky/CloudVolume.h"
#include "Terrain/TerrainLodFile.h"
#include "Sky/Environment.h"
#include "Render/SkyPipeline.h"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

using namespace Dark;
using namespace Dark::EditorDetail;
using namespace Math;

WeaponLoadout* EditorApp::playWeapons()
{
    if (!m_playMode || !m_playPlayer.valid() || !world().alive(m_playPlayer))
        return nullptr;
    WeaponLoadoutComponent* wlc = world().get<WeaponLoadoutComponent>(m_playPlayer);
    if (!wlc || !wlc->loadout)
        return nullptr;
    return wlc->loadout.get();
}

void EditorApp::drawPlayCrosshair(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd || !m_playMode || !m_crosshair.isValid() || m_sceneMode != SceneMode::Scene3D)
        return;
    if (!m_playPlayer.valid() || !world().alive(m_playPlayer))
        return;
    const HealthComponent* hp = world().get<HealthComponent>(m_playPlayer);
    if (!hp || !hp->health.alive())
        return;

    bool throwableSlot = false;
    if (const ThrowableBeltComponent* carried = world().get<ThrowableBeltComponent>(m_playPlayer))
        throwableSlot = carried->belt.any();

    WeaponKind kind = WeaponKind::Melee;
    if (const WeaponLoadout* weapons = playWeapons())
        kind = weapons->activeKind();

    const GpuScope hud(cmd, "HUD", ProfileColor::Hud);
    m_crosshair.draw(cmd, renderer().width(), renderer().height(), kind, throwableSlot);
}

void EditorApp::submitPlayProjectiles(std::vector<int>& rejected)
{
    WeaponLoadout* weapons = playWeapons();
    if (!weapons)
        return;
    GpuResourceCache& gpu    = renderer().gpuResources();
    const auto        tracer = assets().getAs<Model>(m_tracerModelId);
    if (!tracer || !tracer->hasOpaque() || !gpu.ensureModel(tracer))
        return;
    GpuModel* gm = gpu.model(tracer->id);
    if (!gm)
        return;

    const float scale = weapons->projectile().desc().radius * 2.0f;
    int         shot  = 0;
    for (const LiveProjectile& s : weapons->projectile().live())
    {
        const int index = shot++;
        if (!s.alive)
            continue;
        const Matrix4f worldMat = Matrix4f::ScaleMatrixXYZ(scale, scale, scale) * Matrix4f::TranslationMatrix(s.position.x, s.position.y, s.position.z);
        const Matrix4f prev     = Matrix4f::ScaleMatrixXYZ(scale, scale, scale) * Matrix4f::TranslationMatrix(s.prevPosition.x, s.prevPosition.y, s.prevPosition.z);
        if (!m_drawList.submitStaticModel(gpu, *gm, worldMat, StaticPopulation::Projectiles, false, tracer->bounds(), &prev))
            rejected.push_back(index);
    }
}

void EditorApp::drawPlayProjectiles(ID3D12GraphicsCommandList* cmd, const Matrix4f& viewProj, const Matrix4f& prevViewProj, bool gbuffer, bool instanced,
                                    const MeshForwardBatchConstants* forwardBatch, const MeshFrameConstants* forwardLit, const std::vector<int>* rejectedShots)
{
    WeaponLoadout* weapons = playWeapons();
    const auto     model   = assets().getAs<Model>(m_tracerModelId);
    if (!cmd || !weapons || !model || !model->hasOpaque())
        return;

    bool any = false;
    for (const LiveProjectile& s : weapons->projectile().live())
    {
        if (s.alive)
        {
            any = true;
            break;
        }
    }
    if (!any)
        return;

    GpuResourceCache& gpu = renderer().gpuResources();
    if (!instanced && !gpu.ensureModel(model))
        return;

    const GpuScope  projectiles(cmd, "Projectiles", ProfileColor::Projectiles);
    const float     scale = weapons->projectile().desc().radius * 2.0f;
    const DebugFill fill  = renderer().debugState().fill;
    auto            drawShot = [&](const LiveProjectile& s) {
        const Matrix4f worldMat = Matrix4f::ScaleMatrixXYZ(scale, scale, scale) * Matrix4f::TranslationMatrix(s.position.x, s.position.y, s.position.z);
        if (gbuffer)
            drawModelOpaqueGBuffer(cmd, gpu, m_meshPipeline, *model, worldMat, viewProj, prevViewProj, fill);
        else if (forwardLit)
            drawModelForward(cmd, gpu, m_meshPipeline, m_shadows, *model, false, worldMat, viewProj, *forwardLit, fill);
    };

    if (instanced)
    {
        if (gbuffer)
            m_drawList.recordOpaque(cmd, gpu, ViewId{ 0 }, StaticPopulation::Projectiles);
        else if (forwardBatch)
            m_drawList.recordForward(cmd, gpu, ViewId{ 0 }, StaticPopulation::Projectiles, *forwardBatch, m_shadows.receiverCbvGpu());
        if (!rejectedShots || rejectedShots->empty() || !gpu.ensureModel(model))
            return;
        int shot = 0;
        for (const LiveProjectile& s : weapons->projectile().live())
        {
            const int index = shot++;
            if (!s.alive)
                continue;
            bool rejected = false;
            for (int r : *rejectedShots)
            {
                if (r == index)
                {
                    rejected = true;
                    break;
                }
            }
            if (rejected)
                drawShot(s);
        }
        return;
    }

    for (const LiveProjectile& s : weapons->projectile().live())
    {
        if (s.alive)
            drawShot(s);
    }
}

void EditorApp::renderScene3D(ID3D12GraphicsCommandList* cmd)
{
    m_foliagePipeline.setWind(m_wind, m_windClock.time, m_windClock.prevTime);
    GpuResourceCache& gpu = renderer().gpuResources();
    PassId   playedPasses[24]{};
    uint32_t playedCount = 0;
    auto markPass = [&](PassId id) {
        if (playedCount < 24)
            playedPasses[playedCount++] = id;
    };
    m_camera.ClearSubpixelJitter();
    Vector3f lightDir{};
    Vector3f sunColor{};
    Vector3f ambientColor{};
    gatherEditorLighting(lightDir, sunColor, ambientColor);
    const float ambientScale = (ambientColor.x + ambientColor.y + ambientColor.z) * (1.0f / 3.0f);
    const bool drawTerrain = m_haveTerrain && m_terrainMaterial.isValid() && m_scene.terrainPipeline().isValid();
    const Frustum3f cameraCull(m_camera.GetCullViewProj());
    const bool selectLocalShadows = renderer().scenePath() == ScenePath::HybridDeferred
        && renderer().debugState().localLights
        && renderer().debugState().lightingActive();
    m_scene.localShadows().update(world(), m_camera, cameraCull, renderer().frameIndex(), selectLocalShadows);
    AABox3f sceneBounds = drawTerrain
        ? m_terrain.shadowBounds(m_camera)
        : AABox3f(Vector3f(-22.0f, -2.0f, -22.0f), Vector3f(22.0f, 16.0f, 22.0f));
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent&) {
        if (const auto* xf = world().get<TransformComponent>(e))
            sceneBounds.ExpandToInclude(xf->position);
    });
    m_shadows.update(m_camera, lightDir, sceneBounds, 0.8f, 0.20f, renderer().frameIndex());

    const bool instanceStatic = m_drawList.gpuReady() && renderer().debugState().instancedStatic && renderer().debugState().fill == DebugFill::Solid;
    const bool instanceSkinned = instanceStatic && m_skinnedInstances.isValid()
        && (renderer().scenePath() == ScenePath::HybridDeferred || m_skinnedInstances.forwardReady());
    std::unordered_set<EntityID> acceptedProp;
    std::unordered_set<EntityID> acceptedModel;
    std::unordered_set<EntityID> acceptedSkinned;
    std::vector<int> rejectedShots;
    uint8_t shadowViewIndex = 1;
    if (instanceStatic)
    {
        m_drawList.beginFrame(renderer().frameIndex());
        m_drawList.setIndirect(renderer().debugState().instancedIndirect);
        const AABox3f propLocal(Vector3f(-0.5f, -0.5f, -0.5f), Vector3f(0.5f, 0.5f, 0.5f));
        world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
            if (!isScene3DType(so.type))
                return;
            const auto* xf = world().get<TransformComponent>(e);
            const Mesh* mesh = meshForType(so.type);
            if (!xf || !mesh || !mesh->valid() || world().get<LocalLightComponent>(e))
                return;
            RenderItem item;
            item.mesh       = mesh;
            item.kind       = RenderItemKind::Static;
            item.population = StaticPopulation::OpaqueMeshes;
            item.world      = makeWorldMatrix(*xf);
            item.prevWorld  = item.world;
            item.worldAabb  = propLocal.Transformed(item.world);
            item.castShadow = true;
            float cr = so.color[0];
            float cg = so.color[1];
            float cb = so.color[2];
            if (m_selected.valid() && m_selected.id() == e.id())
            {
                cr = cr * 0.55f + 1.0f * 0.45f;
                cg = cg * 0.55f + 0.85f * 0.45f;
                cb = cb * 0.55f + 0.20f * 0.45f;
            }
            item.color[0] = cr;
            item.color[1] = cg;
            item.color[2] = cb;
            float emissive = 0.0f;
            item.material  = NULL_ASSET;
            float alphaModeMask = 0.0f;
            const Material* surfaceMat = nullptr;
            if (const MeshComponent* mc = world().get<MeshComponent>(e))
            {
                emissive = mc->emissive;
                if (auto mat = assets().getAs<Material>(mc->matAssetID))
                {
                    gpu.ensureMaterial(mat);
                    surfaceMat    = mat.get();
                    item.material = mat->id;
                }
            }
            if (!surfaceMat && m_propMaterial && m_propMaterial->isValid())
            {
                gpu.ensureMaterial(m_propMaterial);
                surfaceMat    = m_propMaterial.get();
                item.material = m_propMaterial->id;
            }
            if (surfaceMat)
            {
                MeshGBufferConstants surface{};
                applyMaterialSurface(*surfaceMat, surface);
                alphaModeMask = surface.alphaModeMask;
                emissive      = Max(emissive, surfaceMat->emissive());
            }
            item.color[3]  = emissive;
            item.alphaMask = alphaModeMask > 0.5f;
            if (m_drawList.submit(item))
                acceptedProp.insert(e.id());
        });
        world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            if (!mc.visible)
                return;
            const auto* xf = world().get<TransformComponent>(e);
            const auto model = assets().getAs<Model>(mc.modelAssetID);
            if (!xf || !model || !model->valid() || !model->hasOpaque() || !gpu.ensureModel(model))
                return;
            GpuModel* gm = gpu.model(model->id);
            if (!gm)
                return;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            if (m_drawList.submitStaticModel(gpu, *gm, worldMat, StaticPopulation::OpaqueModels, mc.castShadow, model->bounds(), nullptr))
                acceptedModel.insert(e.id());
            if (instanceSkinned && model->skinned())
            {
                const AnimGraphComponent* ag = world().get<AnimGraphComponent>(e);
                const AnimPose* pose = skinnedPose(*model, ag);
                Matrix4f prevW = worldMat;
                if (ag && ag->prevWorldValid)
                    prevW = ag->prevWorld;
                if (submitSkinnedModelOpaque(m_skinnedInstances, gpu, *gm, pose, worldMat, prevW, StaticPopulation::OpaqueModels, mc.castShadow, model->bounds()))
                    acceptedSkinned.insert(e.id());
            }
        });
        submitPlayProjectiles(rejectedShots);
    }

    // A point light culls foliage once against its range sphere and redraws that list on each face.
    auto drawShadowCasters = [&](const Matrix4f& lightViewProj, const Frustum3f& casterFrustum, const Sphere3f* foliageSphere, bool beginFoliage) {
        if (drawTerrain)
            m_terrain.drawDepth(cmd, &casterFrustum);
        else if (m_showSolid && m_groundMesh.valid())
        {
            // CreateGroundPlane(40, 0, 10) is the ±20 square on Y=0.
            const AABox3f ground(Vector3f(-20.0f, 0.0f, -20.0f), Vector3f(20.0f, 0.0f, 20.0f));
            if (casterFrustum.Intersects(ground))
                m_groundMesh.draw(cmd);
        }
        const bool recordThisView = instanceStatic && shadowViewIndex < kMaxStaticViews;
        const uint32_t skinView   = shadowViewIndex;
        if (recordThisView)
        {
            float lightVp[16];
            copyMatrix(lightVp, lightViewProj);
            const ViewId view{ shadowViewIndex };
            if (!m_drawList.gpuCullActive())
                m_drawList.buildView(casterFrustum, lightVp, lightVp, RenderPassKind::Depth, view);
            m_drawList.recordDepth(cmd, view, lightVp);
            ++shadowViewIndex;
            m_shadows.pipeline().bind(cmd);
        }
        if (instanceSkinned)
        {
            float skinVp[16];
            copyMatrix(skinVp, lightViewProj);
            m_skinnedInstances.recordDepth(cmd, skinView, skinVp);
            m_shadows.pipeline().bind(cmd);
        }
        world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
            if (!isScene3DType(so.type))
                return;
            const auto* xf = world().get<TransformComponent>(e);
            const Mesh* mesh = meshForType(so.type);
            if (!xf || !mesh || !mesh->valid() || world().get<LocalLightComponent>(e))
                return;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            // CreateCube(1) and CreateSphere(0.5) both occupy ±0.5.
            const AABox3f local(Vector3f(-0.5f, -0.5f, -0.5f), Vector3f(0.5f, 0.5f, 0.5f));
            if (!casterFrustum.Intersects(local.Transformed(worldMat)))
                return;
            if (recordThisView && acceptedProp.count(e.id()) != 0)
                return;
            const Matrix4f wvp = worldMat * lightViewProj;
            m_shadows.pipeline().setWvp(cmd, wvp.m_afEntry);
            mesh->draw(cmd);
        });
        world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            if (!mc.visible || !mc.castShadow)
                return;
            const auto* xf = world().get<TransformComponent>(e);
            const auto model = assets().getAs<Model>(mc.modelAssetID);
            if (!xf || !model || !model->valid())
                return;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            if (model->bounds().IsValid() && !casterFrustum.Intersects(model->bounds().Transformed(worldMat)))
                return;
            if (!gpu.ensureModel(model))
                return;
            const bool accepted = recordThisView && acceptedModel.count(e.id()) != 0;
            const bool skinnedListed = instanceSkinned && acceptedSkinned.count(e.id()) != 0;
            if (model->skinned())
            {
                const AnimPose* pose = skinnedPose(*model, world().get<AnimGraphComponent>(e));
                if (accepted && skinnedListed)
                    return;
                if (accepted)
                {
                    if (pose)
                        drawSkinnedModelDepthSkinnedOnly(cmd, gpu, m_shadows.pipeline(), lightViewProj, m_skinnedShadowPipeline, m_skinRing, *model, *pose, worldMat);
                    return;
                }
                if (skinnedListed)
                {
                    drawModelDepth(cmd, gpu, m_shadows.pipeline(), lightViewProj, *model, worldMat);
                    return;
                }
                if (pose)
                    drawSkinnedModelDepth(cmd, gpu, m_shadows.pipeline(), lightViewProj, m_skinnedShadowPipeline, m_skinRing, *model, *pose, worldMat);
                else
                    drawModelDepth(cmd, gpu, m_shadows.pipeline(), lightViewProj, *model, worldMat);
            }
            else if (!accepted)
                drawModelDepth(cmd, gpu, m_shadows.pipeline(), lightViewProj, *model, worldMat);
        });
        if (m_haveTerrain && m_terrain.valid())
        {
            if (beginFoliage)
                m_foliagePipeline.beginDepthView(renderer(), assets(), m_foliagePrototypes, m_terrain, &m_foliage, m_foliageDensity, m_camera, casterFrustum, foliageSphere);
            m_foliagePipeline.drawPreparedDepth(cmd, lightViewProj);
        }
    };

    // Cull every static view before any of them draw. The argument buffer stays unordered until the last rewrite.
    const bool deferredEarly = renderer().scenePath() == ScenePath::HybridDeferred;
    const bool gpuCull       = instanceStatic && renderer().debugState().instancedIndirect && m_drawList.cullReady();
    if (gpuCull)
    {
        m_drawList.setGpuCull(true);
        uint8_t cullView = 1;
        auto addDepthView = [&](const Matrix4f& lightViewProj, const Frustum3f& lightFrustum) {
            if (cullView >= kMaxStaticViews)
                return;
            float lightVp[16];
            copyMatrix(lightVp, lightViewProj);
            m_drawList.buildView(lightFrustum, lightVp, lightVp, RenderPassKind::Depth, ViewId{ cullView });
            ++cullView;
        };
        if (m_shadows.isValid() && m_shadows.enabled())
        {
            for (int i = 0; i < m_shadows.cascadeCount(); ++i)
                addDepthView(m_shadows.cascade(i).viewProj, Frustum3f(m_shadows.cascade(i).viewProj));
        }
        LocalShadowSystem& cullLocals = m_scene.localShadows();
        for (int f = 0; f < cullLocals.faceCountThisFrame(); ++f)
        {
            const LocalShadowFaceDraw& face = cullLocals.face(f);
            addDepthView(face.viewProj, face.frustum);
        }
        if (deferredEarly || m_drawList.forwardReady() || instanceSkinned)
        {
            const Matrix4f camVp   = m_scene.beginCameraFrame(m_camera, renderer());
            const Matrix4f camPrev = m_scene.havePrevViewProj() ? m_scene.prevViewProj() : camVp;
            float drawVp[16];
            float drawPrev[16];
            copyMatrix(drawVp, camVp);
            copyMatrix(drawPrev, camPrev);
            m_drawList.buildView(Frustum3f(m_camera.GetCullViewProj()), drawVp, drawPrev, RenderPassKind::GBuffer, ViewId{ 0 });
        }
        CullViewDesc skinViews[kMaxStaticViews];
        const uint32_t skinViewCount = m_drawList.exportGpuViews(skinViews, kMaxStaticViews);
        m_drawList.dispatchCull(cmd);
        if (instanceSkinned)
            m_skinnedInstances.dispatchCull(cmd, skinViews, skinViewCount);
    }

    if (m_shadows.isValid())
        markPass(PassId::Shadows);
    if (m_shadows.isValid() && m_shadows.enabled())
    {
        m_shadows.beginCapture(cmd);
        for (int i = 0; i < m_shadows.cascadeCount(); ++i)
        {
            m_shadows.beginCascade(cmd, i);
            const Frustum3f casterFrustum(m_shadows.cascade(i).viewProj);
            drawShadowCasters(m_shadows.cascade(i).viewProj, casterFrustum, nullptr, true);
        }
        m_shadows.endCapture(cmd);
    }
    else if (m_shadows.isValid())
    {
        m_shadows.endCapture(cmd);
    }

    LocalShadowSystem& localShadows = m_scene.localShadows();
    if (localShadows.faceCountThisFrame() > 0 || localShadows.isValid())
        markPass(PassId::LocalShadows);
    if (localShadows.faceCountThisFrame() > 0)
    {
        localShadows.beginCapture(cmd);
        for (int f = 0; f < localShadows.faceCountThisFrame(); ++f)
        {
            localShadows.beginFace(cmd, f);
            const LocalShadowFaceDraw& face = localShadows.face(f);
            Sphere3f pointSphere;
            const Sphere3f* foliageSphere = nullptr;
            if (face.firstFace)
            {
                const LocalLightComponent* light = world().get<LocalLightComponent>(face.light);
                const TransformComponent* xf = world().get<TransformComponent>(face.light);
                if (light && xf && light->type == LocalLightType::Point)
                {
                    pointSphere = Sphere3f(xf->position, Min(light->range, 80.0f));
                    foliageSphere = &pointSphere;
                }
            }
            drawShadowCasters(face.viewProj, face.frustum, foliageSphere, face.firstFace);
        }
        localShadows.endCapture(cmd);
    }
    else if (localShadows.isValid())
    {
        localShadows.endCapture(cmd);
    }

    const bool deferred = renderer().scenePath() == ScenePath::HybridDeferred;
    const bool instanceColor = instanceStatic && deferred;
    const bool instanceForward = instanceStatic && !deferred && m_drawList.forwardReady();
    const Matrix4f viewProj = m_scene.beginCameraFrame(m_camera, renderer());
    const Matrix4f prevViewProj = m_scene.havePrevViewProj() ? m_scene.prevViewProj() : viewProj;
    if (deferred)
    {
        markPass(PassId::GBuffer);
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
        {
            m_terrain.drawGBuffer(cmd, m_scene.terrainPipeline(), m_terrainMaterial, m_camera, &frustum, &renderer().debugState(), &prevViewProj);
            drawEditorGrass(cmd, viewProj, prevViewProj, frustum);
        }
        else
        {
            m_terrain.draw(cmd, m_scene.terrainPipeline(), m_terrainMaterial, m_camera, &frustum, nullptr, &m_shadows, &renderer().debugState());
            if (!m_loggedForwardGrassSkip && m_grassPipeline.isValid() && m_grassField.params().enabled)
            {
                m_loggedForwardGrassSkip = true;
                DE_LOG_INFO(LogCategory::Render, "Editor: forward frame skips grass");
            }
        }
    }
    else if (m_showSolid && m_groundMesh.valid() && m_groundMaterial)
    {
        const GpuScope meshes(cmd, "Opaque Meshes", ProfileColor::OpaqueMeshes);
        const float* c = m_groundMaterial->baseColor();
        drawMesh(m_groundMesh, Matrix4f{}, m_groundMaterial.get(), c[0], c[1], c[2]);
    }

    auto drawGrid = [&]() {
        if (!m_showGrid || !m_gridMesh.valid())
            return;
        markPass(PassId::EditorGrid);
        const GpuScope grid(cmd, "Editor Grid", ProfileColor::Grid);
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
        markPass(PassId::EditorGizmos);
        const GpuScope gizmos(cmd, "Light Gizmos", ProfileColor::Gizmos);
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
            else if (so.type == SceneObjectType::CloudVolume)
            {
                CloudShape shape = CloudShape::Ellipsoid;
                if (const CloudVolumeComponent* cloud = world().get<CloudVolumeComponent>(e))
                    shape = cloud->desc.shape;
                if (shape == CloudShape::Box)
                {
                    if (!m_cloudBoxGizmo.valid())
                        return;
                    gizmo    = &m_cloudBoxGizmo;
                    worldMat = makeWorldMatrix(*xf);
                }
                else
                {
                    if (!m_pointLightGizmo.valid())
                        return;
                    gizmo    = &m_pointLightGizmo;
                    const Vector3f half = cloudHalfExtents(*xf);
                    worldMat = Matrix4f::ScaleMatrixXYZ(half.x, half.y, half.z) * xf->rotation.ToMatrix4()
                        * Matrix4f::TranslationMatrix(xf->position.x, xf->position.y, xf->position.z);
                }
                cr = 0.78f;
                cg = 0.86f;
                cb = 1.00f;
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

    if ((instanceColor || instanceForward) && !m_drawList.gpuCullActive())
    {
        float drawVp[16];
        float drawPrev[16];
        copyMatrix(drawVp, viewProj);
        copyMatrix(drawPrev, prevViewProj);
        const Frustum3f cameraFrustum(m_camera.GetCullViewProj());
        m_drawList.buildView(cameraFrustum, drawVp, drawPrev, RenderPassKind::GBuffer, ViewId{ 0 });
    }

    MeshForwardBatchConstants forwardLighting{};
    const D3D12_GPU_VIRTUAL_ADDRESS forwardShadow = m_shadows.receiverCbvGpu();
    if (instanceForward || instanceSkinned)
    {
        forwardLighting.lightDirWS[0] = lightDir.x;
        forwardLighting.lightDirWS[1] = lightDir.y;
        forwardLighting.lightDirWS[2] = lightDir.z;
        forwardLighting.ambientScale  = ambientScale;
        forwardLighting.lightColor[0] = sunColor.x;
        forwardLighting.lightColor[1] = sunColor.y;
        forwardLighting.lightColor[2] = sunColor.z;
        const Vector3f cam = m_camera.GetPosition();
        forwardLighting.cameraPos[0] = cam.x;
        forwardLighting.cameraPos[1] = cam.y;
        forwardLighting.cameraPos[2] = cam.z;
        forwardLighting.lighting     = renderer().debugState().lightingActive() ? 1.0f : 0.0f;
    }

    uint32_t draws = 0;
    {
        const GpuScope meshes(cmd, "Opaque Meshes", ProfileColor::OpaqueMeshes);
        if (instanceColor)
            m_drawList.recordOpaque(cmd, gpu, ViewId{ 0 }, StaticPopulation::OpaqueMeshes);
        if (instanceForward)
            m_drawList.recordForward(cmd, gpu, ViewId{ 0 }, StaticPopulation::OpaqueMeshes, forwardLighting, forwardShadow);
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
        if ((instanceColor || instanceForward) && acceptedProp.count(e.id()) != 0)
            return;
        drawMesh(*mesh, makeWorldMatrix(*xf), material, cr, cg, cb, emissive);
        ++draws;
    });
    }

    if (deferred)
    {
        const GpuScope models(cmd, "Opaque Models", ProfileColor::OpaqueModels);
        if (instanceColor)
            m_drawList.recordOpaque(cmd, gpu, ViewId{ 0 }, StaticPopulation::OpaqueModels);
        if (instanceSkinned)
        {
            float drawVp[16];
            float drawPrev[16];
            copyMatrix(drawVp, viewProj);
            copyMatrix(drawPrev, prevViewProj);
            m_skinnedInstances.recordGBuffer(cmd, gpu, StaticPopulation::OpaqueModels, drawVp, drawPrev);
        }
        world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            if (!mc.visible)
                return;
            const auto* xf = world().get<TransformComponent>(e);
            const auto model = assets().getAs<Model>(mc.modelAssetID);
            if (!xf || !model || !model->hasOpaque() || !gpu.ensureModel(model))
                return;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            const bool accepted = instanceColor && acceptedModel.count(e.id()) != 0;
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
                const bool skinnedListed = instanceSkinned && acceptedSkinned.count(e.id()) != 0;
                if (!(accepted && skinnedListed))
                {
                    if (accepted)
                        drawSkinnedModelOpaqueGBufferSkinnedOnly(cmd, gpu, m_skinnedPipeline, m_meshPipeline, m_skinRing, *model, *pose, worldMat, prevW, viewProj, prevViewProj, fill);
                    else if (skinnedListed)
                        drawModelOpaqueGBuffer(cmd, gpu, m_meshPipeline, *model, worldMat, viewProj, prevViewProj, fill);
                    else
                        drawSkinnedModelOpaqueGBuffer(cmd, gpu, m_skinnedPipeline, m_meshPipeline, m_skinRing, *model, *pose, worldMat, prevW, viewProj, prevViewProj, fill);
                }
                ++draws;
            }
            else if (!accepted)
            {
                drawModelOpaqueGBuffer(cmd, gpu, m_meshPipeline, *model, worldMat, viewProj, prevViewProj, fill);
                ++draws;
            }
        });
        drawPlayProjectiles(cmd, viewProj, prevViewProj, true, instanceColor, nullptr, nullptr, &rejectedShots);
        if (m_haveTerrain && m_terrain.valid())
        {
            const Frustum3f foliageFrustum(m_camera.GetCullViewProj());
            m_foliagePipeline.drawGBuffer(cmd, renderer(), assets(), m_foliagePrototypes, m_terrain, &m_foliage, m_foliageDensity, m_camera, viewProj, prevViewProj, foliageFrustum);
        }
    }
    else
    {
        const GpuScope models(cmd, "Opaque Models", ProfileColor::OpaqueModels);
        if (instanceForward)
            m_drawList.recordForward(cmd, gpu, ViewId{ 0 }, StaticPopulation::OpaqueModels, forwardLighting, forwardShadow);
        if (instanceSkinned)
        {
            float drawVp[16];
            copyMatrix(drawVp, viewProj);
            m_skinnedInstances.recordForward(cmd, gpu, StaticPopulation::OpaqueModels, forwardLighting, drawVp, forwardShadow);
        }
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
            if (!mc.visible)
                return;
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
                const bool accepted = instanceForward && acceptedModel.count(e.id()) != 0;
                const bool skinnedListed = instanceSkinned && acceptedSkinned.count(e.id()) != 0;
                if (!(accepted && skinnedListed))
                {
                    if (accepted)
                        drawSkinnedModelForwardSkinnedOnly(cmd, gpu, m_skinnedPipeline, m_meshPipeline, m_shadows, m_skinRing, *model, *pose, worldMat, viewProj, lit, fill);
                    else if (skinnedListed)
                        drawModelForward(cmd, gpu, m_meshPipeline, m_shadows, *model, false, worldMat, viewProj, lit, fill);
                    else
                        drawSkinnedModelForward(cmd, gpu, m_skinnedPipeline, m_meshPipeline, m_shadows, m_skinRing, *model, false, *pose, worldMat, viewProj, lit, fill);
                }
            }
            else if (!(instanceForward && acceptedModel.count(e.id()) != 0))
                drawModelForward(cmd, gpu, m_meshPipeline, m_shadows, *model, false, worldMat, viewProj, lit, fill);
            ++draws;
        });
        drawPlayProjectiles(cmd, viewProj, prevViewProj, false, instanceForward, &forwardLighting, &lit, &rejectedShots);
    }

    if (deferred)
    {
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
        fillSsrLightingConstants(lc, m_ssr, renderer().debugState().ssrEnabled, m_scene.ssr().isValid() && renderer().hasGBuffer());
        // The pass list is an order check after the frame. These five run inside resolveDeferred in this order.
        markPass(PassId::Decals);
        markPass(PassId::Gtao);
        markPass(PassId::Ssr);
        markPass(PassId::Lighting);
        markPass(PassId::LocalVolumes);
        DeferredResolveInput resolve;
        resolve.viewProj     = &viewProj;
        resolve.prevViewProj = &prevViewProj;
        resolve.gtao         = &m_ssao;
        resolve.ssr          = &m_ssr;
        resolve.ssrEnv       = nullptr;
        resolve.lighting     = &lc;
        m_scene.resolveDeferred(cmd, renderer(), world(), m_camera, resolve);
        renderer().bindHdr(true);
        if (m_skyPipeline.isValid())
        {
            markPass(PassId::Sky);
            const float waterLevel = m_water.params().waterLevel;
            m_skyPipeline.upload(renderer().frameIndex(), m_camera, m_env, 1.0f, waterLevel, 0.0f);
            m_skyPipeline.draw(cmd, renderer().frameIndex(), &m_shadows);
        }
        markPass(PassId::SsrCapture);
        m_scene.captureSsrSceneColor(cmd, renderer());
        renderer().bindHdrDepthRead();
        if (m_haveTerrain && m_scene.waterPipeline().isValid())
        {
            const Frustum3f waterFrustum(m_camera.GetCullViewProj());
            ID3D12DescriptorHeap*       heightHeap = nullptr;
            D3D12_GPU_DESCRIPTOR_HANDLE heightGpu{};
            if (m_terrain.heightTexture().valid())
            {
                heightHeap = m_terrain.heightTexture().srvHeap();
                heightGpu  = m_terrain.heightTexture().gpuHandle();
            }
            D3D12_CPU_DESCRIPTOR_HANDLE waterSceneColor{};
            D3D12_CPU_DESCRIPTOR_HANDLE waterDepth{};
            const SsrSettings*          waterSsr = nullptr;
            if (m_scene.ssr().hasSceneColor())
            {
                waterSceneColor = m_scene.ssr().sceneColorSrvCpu();
                waterDepth      = renderer().depthSrvCpu();
                waterSsr        = &m_ssr;
            }
            const bool anyWater = m_water.wetChunkCount() > 0 || !m_placedWater.empty() || !m_editorStreams.empty();
            if (anyWater)
            {
                markPass(PassId::Water);
                const GpuScope water(cmd, "Water", ProfileColor::Water);
                drawPlacedWater(cmd, waterFrustum, heightHeap, heightGpu, waterSceneColor, waterDepth, waterSsr);
            }
        }
        {
            markPass(PassId::Clouds);
            CloudVolumeFrame cf{};
            cf.sunDir       = lightDir;
            cf.sunColor     = sunColor;
            cf.ambientColor = ambientColor;
            cf.time         = m_cloudTime;
            m_scene.drawCloudVolumes(cmd, renderer(), world(), m_camera, viewProj, cf);
        }
        drawGrid();
        drawLightGizmos();
    }
    else
    {
        drawLightGizmos();
    }
    {
        LinePipeline& volumeLines = m_linePipeline3D.isValid() ? m_linePipeline3D : m_linePipeline;
        m_scene.drawDecalVolumes(cmd, renderer(), volumeLines, viewProj);
    }

    {
        markPass(PassId::Translucent);
        const GpuScope translucent(cmd, "Translucent", ProfileColor::Translucent);
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
        std::vector<Entity> translucentEntities;
        std::vector<TranslucentDraw> translucentOrder;
        world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            if (!mc.visible)
                return;
            const auto* xf = world().get<TransformComponent>(e);
            const auto model = assets().getAs<Model>(mc.modelAssetID);
            if (!xf || !model || !model->hasTranslucent() || !gpu.ensureModel(model))
                return;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            if (model->skinned() && !skinnedPose(*model, world().get<AnimGraphComponent>(e)))
                return;
            TranslucentDraw draw;
            draw.user      = static_cast<uint32_t>(translucentEntities.size());
            draw.worldAabb = model->bounds().IsValid() ? model->bounds().Transformed(worldMat) : AABox3f::Empty();
            translucentEntities.push_back(e);
            translucentOrder.push_back(draw);
        });
        sortTranslucent(translucentOrder.data(), static_cast<uint32_t>(translucentOrder.size()), m_camera.GetView());
        for (const TranslucentDraw& draw : translucentOrder)
        {
            const Entity e = translucentEntities[draw.user];
            const ModelComponent* mc = world().get<ModelComponent>(e);
            const auto* xf = world().get<TransformComponent>(e);
            const auto model = (mc && xf) ? assets().getAs<Model>(mc->modelAssetID) : AssetRef<Model>{};
            if (!xf || !model || !gpu.ensureModel(model))
                continue;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            if (model->skinned())
            {
                const AnimPose* pose = skinnedPose(*model, world().get<AnimGraphComponent>(e));
                if (!pose)
                    continue;
                drawSkinnedModelForward(cmd, gpu, m_skinnedTransparentPipeline, m_meshTransparentPipeline, m_shadows, m_skinRing, *model, true, *pose, worldMat, viewProj, lit, fill);
            }
            else
                drawModelForward(cmd, gpu, m_meshTransparentPipeline, m_shadows, *model, true, worldMat, viewProj, lit, fill);
            ++draws;
        }
    }

    {
        markPass(PassId::Particles);
        const GpuScope particles(cmd, "Particles", ProfileColor::Particles);
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
        if (WeaponLoadout* weapons = playWeapons())
        {
            const ParticleEmitter& impact = weapons->projectile().impactEmitter();
            if (impact.aliveCount() > 0)
                m_particleRenderer.draw(cmd, m_camera, impact, true);
        }
        m_leafRenderer.draw(cmd, m_camera, m_leafField);
    }

    {
        const bool aces = renderer().hasSceneBuffers() && renderer().debugState().aces && renderer().debugState().lightingActive();
        TonemapSettings ts{};
        ts.mode     = aces ? 1.0f : 0.0f;
        ts.exposure = 1.0f;
        markPass(PassId::Post);
        m_scene.applyPost(renderer(), cmd, viewProj, ts);
    }

    if (renderer().hasSceneBuffers())
    {
        const bool ssaoTile = renderer().debugState().ssaoDebug == 1;
        const bool ssrTile  = renderer().debugState().ssrDebug != 0;
        const bool shadowTiles = renderer().debugState().shadowMapTiles && m_scene.localShadows().isValid();
        if (m_debugOverlay.isValid() && (((m_showGBuffer || m_showVelocity || ssaoTile || ssrTile) && renderer().hasGBuffer()) || shadowTiles))
        {
            markPass(PassId::Overlay);
            const GpuScope overlay(cmd, "Debug Overlay", ProfileColor::DebugOverlay);
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
            if (shadowTiles)
            {
                const LONG sw = static_cast<LONG>(renderer().width());
                const LONG sh = static_cast<LONG>(renderer().height());
                LocalShadowSystem& local = m_scene.localShadows();
                const int n = local.slicesPerFrame();
                const LONG gap = 8;
                LONG tw = tile;
                LONG x0 = pad;
                const LONG y = sh - pad - tile;
                if (n > 0)
                {
                    const LONG need = n * tw + (n - 1) * gap;
                    if (x0 + need > sw - pad)
                    {
                        const LONG avail = sw - pad - x0 - (n - 1) * gap;
                        if (avail > 64)
                            tw = avail / n;
                    }
                }
                for (int i = 0; i < n; ++i)
                {
                    const LONG x = x0 + i * (tw + gap);
                    m_debugOverlay.drawArray(
                        cmd,
                        renderer().device(),
                        local.srvCpu(),
                        x,
                        y,
                        tw,
                        tile,
                        local.debugSlice(i),
                        1.25f,
                        true);
                }
            }
            cmd->RSSetViewports(1, &renderer().viewport());
            cmd->RSSetScissorRects(1, &renderer().scissor());
        }
    }

    m_scene.endCameraFrame(m_camera, viewProj);
    drawPlayCrosshair(cmd);
    renderer().stats().drawCalls = draws;
    uint32_t tableCount = 0;
    const PassNode* table = deferred ? editorDeferredPasses(tableCount) : editorForwardPasses(tableCount);
    (void)passSequenceOk(table, tableCount, playedPasses, playedCount);
}

void EditorApp::drawEditorGrass(ID3D12GraphicsCommandList* cmd, const Matrix4f& viewProj, const Matrix4f& prevViewProj, const Frustum3f& frustum)
{
    if (!cmd || !m_grassPipeline.isValid() || !m_grassField.params().enabled)
        return;

    const GpuScope               grassScope(cmd, "Grass", ProfileColor::Grass);
    Terrain::GrassFrameConstants grassFrame{};
    copyMatrix(grassFrame.viewProj, viewProj);
    copyMatrix(grassFrame.prevViewProj, prevViewProj);
    grassFrame.heightMetres = m_grassField.params().heightMetres;
    grassFrame.flexibility  = m_grassField.params().flexibility;
    grassFrame.faceEdge     = m_grassField.params().faceEdge;
    grassFrame.faceYaw      = m_grassField.params().faceYaw;
    grassFrame.faceLean     = m_grassField.params().faceLean;
    grassFrame.faceSteep    = m_grassField.params().faceSteep;
    {
        const Vector3f eye   = m_camera.GetPosition();
        const Vector3f right = m_camera.GetRight();
        const Vector3f look  = m_camera.GetLook();
        grassFrame.cameraPos[0]   = eye.x;
        grassFrame.cameraPos[1]   = eye.y;
        grassFrame.cameraPos[2]   = eye.z;
        grassFrame.cameraRight[0] = right.x;
        grassFrame.cameraRight[1] = right.y;
        grassFrame.cameraRight[2] = right.z;
        grassFrame.cameraLook[0]  = look.x;
        grassFrame.cameraLook[1]  = look.y;
        grassFrame.cameraLook[2]  = look.z;
    }
    m_grassField.writeFrameInteraction(grassFrame);

    GrassLodSpan grassLods[Terrain::kGrassLodCount]{};
    for (int lod = 0; lod < Terrain::kGrassLodCount; ++lod)
    {
        const Terrain::GrassField::BladeSpan span = m_grassField.blades(lod);
        grassLods[lod].blades                     = span.data;
        grassLods[lod].count                      = span.count;
        grassLods[lod].staleMask                  = m_grassField.lodStaleMask(lod);
    }
    const uint32_t                      residentTiles = Terrain::grassRegionBudget().residentTiles;
    std::vector<Terrain::GrassTileDraw> grassTiles(residentTiles);
    const uint32_t                      grassDraws = m_grassField.visibleTiles(frustum, grassTiles.data(), residentTiles);
    if (m_grassPipeline.draw(cmd, renderer(), grassLods, m_grassField.tileWind(), m_grassField.tileWindCount(), grassFrame, grassTiles.data(), grassDraws))
    {
        const uint32_t slot = renderer().frameIndex() % Terrain::kGrassFrameSlots;
        for (int lod = 0; lod < Terrain::kGrassLodCount; ++lod)
            m_grassField.clearLodStale(lod, slot);
    }
}
