#include "SandboxFrame.h"
#include "SandboxApp.h"
#include "SandboxInternal.h"

#include "Ui/ImGuiGameChrome.h"

#include "Play/AttachedLights.h"
#include "Play/EnvironmentTick.h"
#include "Play/Carry.h"
#include "Play/CharacterAttach.h"
#include "Play/ContactGrind.h"
#include "Play/HunterCue.h"
#include "Play/ThrowableHost.h"
#include "Play/HunterTick.h"
#include "Gameplay/LootMark.h"
#include "Gameplay/PowerComponents.h"
#include "Gameplay/PowerSystem.h"
#include "Gameplay/QuestZone.h"
#include "Media/MovieCue.h"
#include "Scene/SceneSpawn.h"
#include "Gameplay/PowerCheck.h"
#include "Play/PhysicsGround.h"
#include "Play/PlayBody.h"
#include "Play/PlayLook.h"
#include "Play/PlayMotor.h"
#include "Play/ShoulderCamera.h"
#include "Play/FrameRecord.h"
#include "Play/ShadowRecord.h"
#include "Play/TerrainStream.h"
#include "Terrain/TerrainLodFile.h"
#include "Play/WeaponFire.h"

#include "ECS/Components.h"
#include "Water/WaterStream.h"

#include <format>
#include <memory>
#include "Core/EntityPins.h"
#include "Core/Log.h"
#include "Input/InputCodes.h"
#include "Scene/EntityInput.h"
#include "Collision/Collision.h"
#include "Collision/StaticCollision.h"
#include "Math/MathHelper.h"
#include "Math/Matrix4f.h"
#include "Math/Quaternion.h"
#include "Math/Ray3f.h"
#include "Math/Sphere3f.h"
#include "Math/Vector2f.h"
#include "Math/Vector3f.h"
#include "Network/NetTypes.h"
#include "Network/Replication.h"
#include "Render/DebugRenderState.h"
#include "Render/DecalBasis.h"
#include "Render/Frustum3f.h"
#include "Render/TaaJitter.h"
#include "Render/MeshGen.h"
#include "Render/ScenePath.h"
#include "Render/Fog.h"
#include "Render/Underwater.h"
#include "Render/PbrLighting.h"
#include "Render/ModelDraw.h"
#include "Render/PassTable.h"
#include "Render/Profile.h"
#include "Render/MaterialSurface.h"
#include "Render/GpuResourceCache.h"
#include "Render/GpuUpload.h"
#include "Save/PersistentId.h"
#include "Save/ProgressComponents.h"
#include "Assets/Image.h"
#include "Terrain/FoliageSpawn.h"
#include "Terrain/WorldEngineMap.h"
#include "Assets/Material.h"
#include "Assets/Model.h"
#include "Animation/AnimGraphTick.h"
#include "Animation/AnimGraphComponent.h"
#include "Animation/Locomotion.h"
#include "AI/AiComponents.h"
#include "AI/Brain.h"
#include "AI/HsmGraph.h"
#include "Scene/EntityMaster.h"
#include "Physics/PhysicsBind.h"
#include "Physics/PhysicsComponent.h"
#include "Physics/PhysicsFile.h"
#include "AI/HsmGraphComponent.h"
#include "Audio/SoundComponents.h"
#include "Animation/AnimNotify.h"
#include "Character/HealthComponent.h"
#include "Character/PlayerMotorComponent.h"
#include "Character/ArmorView.h"
#include "Character/ItemView.h"
#include "Character/ThrowableView.h"
#include "Combat/ArmorComponent.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/ThrowableBelt.h"
#include "Gameplay/ThrowableCatalog.h"
#include "Weapons/ThrowableFlight.h"
#include "Play/PlayerFlashlight.h"
#include "Play/Spawner.h"
#include "Character/ShieldView.h"
#include "Character/SkillSense.h"
#include "Character/SkillXp.h"
#include "Combat/CombatSystem.h"
#include "Combat/DefenseComponent.h"
#include "Combat/Shield.h"
#include "Combat/DamageEvent.h"
#include "Combat/PlayerMode.h"
#include "Combat/HitVolumes.h"
#include "Combat/JumpAttackComponent.h"
#include "Combat/JumpAttackDef.h"
#include "Combat/JumpAttackResolve.h"
#include "Combat/PoiseComponent.h"
#include "Combat/StatusDot.h"
#include "Combat/StatusEffectComponent.h"
#include "Particles/ParticleComponents.h"
#include "Particles/ParticleScene.h"
#include "Particles/ParticleTick.h"
#include "Particles/StatusFxDriver.h"
#include "Ui/HudTagComponent.h"
#include "Weapons/HittableComponent.h"
#include "Animation/SkeletonDebug.h"
#include "Render/LinePipeline.h"
#include "Scene/SceneCatalog.h"
#include "Scene/SceneFile.h"
#include "Sky/CloudVolume.h"
#include "Terrain/SplatMap.h"
#include "Terrain/TerrainGrid.h"
#include "Terrain/TerrainTileFile.h"
#include "Water/WaterWaves.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace Dark;
using namespace Math;
using namespace Terrain;
using namespace Audio;

// SceneRenderer owns shared pipelines; keep existing call sites stable.
#define m_meshPipeline m_scene.meshPipeline()
#define m_meshTransparentPipeline m_scene.meshTransparentPipeline()
#define m_skinnedPipeline m_scene.skinnedPipeline()
#define m_skinnedTransparentPipeline m_scene.skinnedTransparentPipeline()
#define m_skinnedShadowPipeline m_scene.skinnedShadowPipeline()
#define m_skinRing m_scene.skinRing()
#define m_tonemap m_scene.tonemap()
#define m_lighting m_scene.lighting()
#define m_localLightVolumes m_scene.localLightVolumes()
#define m_localLightGpu m_scene.localLightGpu()
#define m_pointVolumeMesh m_scene.pointVolumeMesh()
#define m_spotVolumeMesh m_scene.spotVolumeMesh()
#define m_bloom m_scene.bloom()
#define m_motionBlur m_scene.motionBlur()
#define m_taa m_scene.taa()
#define m_shadows m_scene.shadows()
#define m_debugOverlay m_scene.debugOverlay()
#define m_terrainPipeline m_scene.terrainPipeline()
#define m_waterPipeline m_scene.waterPipeline()
#define m_skyPipeline m_scene.skyPipeline()


SandboxApp& SandboxFrame::app() const
{
    return *m_app;
}

void SandboxFrame::drawProjectiles(ID3D12GraphicsCommandList* cmd, const Matrix4f& viewProj, MeshFrameConstants& cb, bool instanced, const std::vector<int>* rejectedShots)
{
    WeaponLoadout* w = app().localWeapons();
    const Model* model = app().assets().find<Model>(app().m_demo.tracerModelId());
    if (!cmd || !w || !model || !model->hasOpaque())
        return;
    bool any = false;
    for (const LiveProjectile& s : w->projectile().live())
    {
        if (s.alive)
        {
            any = true;
            break;
        }
    }
    if (!any)
        return;

    GpuResourceCache& gpu = app().renderer().gpuResources();
    if (!instanced && !gpu.retainModel(app().assets(), *model))
        return;

    const GpuScope projectiles(cmd, "Projectiles", ProfileColor::Projectiles);
    const float     radius = w->projectile().desc().radius;
    const float     scale  = radius * 2.0f;
    const DebugFill fill   = app().renderer().debugState().fill;
    if (instanced)
    {
        MeshForwardBatchConstants lighting{};
        fillForwardLighting(lighting, cb);
        m_drawList.recordForward(cmd, gpu, ViewId{ 0 }, StaticPopulation::Projectiles, lighting, m_shadows.receiverCbvGpu());
        if (!rejectedShots || rejectedShots->empty() || !gpu.retainModel(app().assets(), *model))
            return;
        int shot = 0;
        for (const LiveProjectile& s : w->projectile().live())
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
            if (!rejected)
                continue;
            const Matrix4f world = Matrix4f::ScaleMatrixXYZ(scale, scale, scale) * Matrix4f::TranslationMatrix(s.position.x, s.position.y, s.position.z);
            drawModelForward(cmd, gpu, m_meshPipeline, m_shadows, *model, false, world, viewProj, cb, fill);
        }
        return;
    }

    for (const LiveProjectile& s : w->projectile().live())
    {
        if (!s.alive)
            continue;
        const Matrix4f world = Matrix4f::ScaleMatrixXYZ(scale, scale, scale) * Matrix4f::TranslationMatrix(s.position.x, s.position.y, s.position.z);
        drawModelForward(cmd, gpu, m_meshPipeline, m_shadows, *model, false, world, viewProj, cb, fill);
    }
}

void SandboxFrame::drawProjectilesGBuffer(ID3D12GraphicsCommandList* cmd, const Matrix4f& viewProj, const Matrix4f& prevViewProj, bool instanced, const std::vector<int>* rejectedShots)
{
    WeaponLoadout* w = app().localWeapons();
    const Model* model = app().assets().find<Model>(app().m_demo.tracerModelId());
    if (!cmd || !w || !model || !model->hasOpaque())
        return;
    bool any = false;
    for (const LiveProjectile& s : w->projectile().live())
    {
        if (s.alive)
        {
            any = true;
            break;
        }
    }
    if (!any)
        return;

    GpuResourceCache& gpu = app().renderer().gpuResources();
    if (!instanced && !gpu.retainModel(app().assets(), *model))
        return;

    const GpuScope projectiles(cmd, "Projectiles", ProfileColor::Projectiles);
    if (instanced)
    {
        m_drawList.recordOpaque(cmd, gpu, ViewId{ 0 }, StaticPopulation::Projectiles);
        if (!rejectedShots || rejectedShots->empty() || !gpu.retainModel(app().assets(), *model))
            return;
        const float     radius = w->projectile().desc().radius;
        const float     scale  = radius * 2.0f;
        const DebugFill fill   = app().renderer().debugState().fill;
        int             shot   = 0;
        for (const LiveProjectile& s : w->projectile().live())
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
            if (!rejected)
                continue;
            const Matrix4f world = Matrix4f::ScaleMatrixXYZ(scale, scale, scale) * Matrix4f::TranslationMatrix(s.position.x, s.position.y, s.position.z);
            drawModelOpaqueGBuffer(cmd, gpu, m_meshPipeline, *model, world, viewProj, prevViewProj, fill);
        }
        return;
    }

    const float     radius = w->projectile().desc().radius;
    const float     scale  = radius * 2.0f;
    const DebugFill fill   = app().renderer().debugState().fill;
    for (const LiveProjectile& s : w->projectile().live())
    {
        if (!s.alive)
            continue;
        const Matrix4f world = Matrix4f::ScaleMatrixXYZ(scale, scale, scale) * Matrix4f::TranslationMatrix(s.position.x, s.position.y, s.position.z);
        drawModelOpaqueGBuffer(cmd, gpu, m_meshPipeline, *model, world, viewProj, prevViewProj, fill);
    }
}

void SandboxFrame::dropDeadPrevWorlds()
{
    for (auto it = m_prevWorldByEntity.begin(); it != m_prevWorldByEntity.end();)
    {
        if (!app().world().alive(Entity{ it->first }))
            it = m_prevWorldByEntity.erase(it);
        else
            ++it;
    }
}

void SandboxFrame::render()
{
    if (!app().renderer().beginFrame())
    {
        app().requestQuit();
        return;
    }
    frameNoteBeginFrame(app().jobs(), app().renderer().frameIndex());

    // The scene is still unbuilt while the start menu is waiting for a pick.
    // shadowBounds is safe on an empty grid; the water and post path is not.
    if (!app().m_sceneStarted)
    {
        Dark::setGameNavEnabled(app().m_hud.m_menu.visible());
        if (app().m_hud.m_imgui.isReady())
            app().m_hud.m_imgui.beginFrame();
        if (app().m_hud.m_imgui.isReady())
            Dark::drawMainMenuImGui(app().m_hud.m_menu, app().m_hud.m_imgui.fonts(), app().m_sceneStarted);
        else
            app().m_hud.m_menu.draw(app().renderer());
        if (app().m_hud.m_imgui.isReady())
            app().m_hud.m_imgui.render(app().renderer());
        frameJoinRender(app().jobs(), app().renderer().frameIndex());
        app().renderer().endFrame();
        return;
    }

    // A destroyed mesh keeps its last matrix until the slot is pruned. The id
    // includes the generation, so a recycle misses, until that generation wraps.
    dropDeadPrevWorlds();

    app().m_foliagePipeline.setWind(app().m_wind, app().m_windClock.time, app().m_windClock.prevTime);

    Dark::setGameNavEnabled(app().m_hud.m_menu.visible() || app().m_gameplayPaused);
    if (app().m_hud.m_imgui.isReady())
        app().m_hud.m_imgui.beginFrame();

    auto* cmd = app().renderer().commandList();
    GpuResourceCache& gpu = app().renderer().gpuResources();
    if (m_skinRing.isValid())
        m_skinRing.beginFrame(app().renderer().frameIndex());
    if (m_skinnedInstances.isValid())
        m_skinnedInstances.beginFrame(app().renderer().frameIndex());

    PassId   playedPasses[24]{};
    uint32_t playedCount = 0;
    auto markPass = [&](PassId id) {
        if (playedCount < 24)
            playedPasses[playedCount++] = id;
    };

    const Frustum3f cameraCull(app().m_viewCamera.GetCullViewProj());
    const bool selectLocalShadows = app().renderer().scenePath() == ScenePath::HybridDeferred
        && app().renderer().debugState().localLights
        && app().renderer().debugState().lightingActive();
    m_scene.localShadows().update(app().world(), app().m_viewCamera, cameraCull, app().renderer().frameIndex(), selectLocalShadows);

    AABox3f sceneBounds = app().m_terrain.shadowBounds(app().m_viewCamera);
    app().world().each<MeshComponent>([&](Entity e, MeshComponent& mc) {
        if (mc.primitive == PrimitiveMesh::None)
            return;
        if (const TransformComponent* xf = app().world().get<TransformComponent>(e))
            sceneBounds.ExpandToInclude(xf->position);
    });
    app().world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
        if (!mc.visible)
            return;
        const TransformComponent* xf = app().world().get<TransformComponent>(e);
        if (!xf)
            return;
        const Model* model = app().assets().find<Model>(mc.modelAssetID);
        if (!model || !model->bounds().IsValid())
            return;
        sceneBounds.ExpandToInclude(model->bounds().Transformed(makeWorldMatrix(*xf)));
    });
    if (app().m_chaseOk)
        app().m_chase.expandBounds(sceneBounds);
    m_shadows.update(
        app().m_viewCamera,
        app().m_env.lightDir(),
        sceneBounds,
        app().m_env.sunElevation(),
        app().m_env.weather.cloudCoverage,
        app().renderer().frameIndex());

    const bool instanceStatic = m_drawList.gpuReady() && app().renderer().debugState().instancedStatic && app().renderer().debugState().fill == DebugFill::Solid;
    const bool instanceSkinned = instanceStatic && m_skinnedInstances.isValid()
        && (app().renderer().scenePath() == ScenePath::HybridDeferred || m_skinnedInstances.forwardReady());
    m_acceptedMesh.clear();
    m_acceptedModel.clear();
    m_acceptedSkinned.clear();
    m_rejectedShots.clear();
    FrameEntitySet& acceptedMesh = m_acceptedMesh;
    FrameEntitySet& acceptedModel = m_acceptedModel;
    FrameEntitySet& acceptedSkinned = m_acceptedSkinned;
    std::vector<int>& rejectedShots = m_rejectedShots;
    uint8_t shadowViewIndex = 1;
    if (instanceStatic)
    {
        m_drawList.beginFrame(app().renderer().frameIndex());
        m_drawList.setIndirect(app().renderer().debugState().instancedIndirect);
        app().world().each<MeshComponent>([&](Entity e, MeshComponent& mc) {
            if (app().world().has<ModelComponent>(e))
                return;
            if (const HealthComponent* hp = app().world().get<HealthComponent>(e); hp && !hp->health.alive())
                return;
            const Mesh* gpuMesh = sandboxPrimitiveMesh(mc.primitive, app().m_demo.cubeMesh(), app().m_crossMesh);
            if (!gpuMesh)
                return;
            const TransformComponent* xf = app().world().get<TransformComponent>(e);
            if (!xf)
                return;
            RenderItem item;
            item.mesh       = gpuMesh;
            item.kind       = RenderItemKind::Static;
            item.population = StaticPopulation::OpaqueMeshes;
            item.world      = makeWorldMatrix(*xf);
            const auto prev = m_prevWorldByEntity.find(e.id());
            item.prevWorld  = prev != m_prevWorldByEntity.end() ? prev->second : item.world;
            const AABox3f local = sandboxPrimitiveBounds(mc.primitive);
            item.worldAabb  = local.IsValid() ? local.Transformed(item.world) : AABox3f::Empty();
            item.color[0]   = 1.0f;
            item.color[1]   = 1.0f;
            item.color[2]   = 1.0f;
            item.color[3]   = 0.0f;
            item.material   = NULL_ASSET;
            float alphaModeMask = 0.0f;
            if (const Material* mat = app().assets().find<Material>(mc.matAssetID))
            {
                gpu.retainMaterial(app().assets(), *mat);
                item.material = mc.matAssetID;
                MeshGBufferConstants surface{};
                applyMaterialSurface(*mat, surface);
                item.color[0]   = surface.color[0];
                item.color[1]   = surface.color[1];
                item.color[2]   = surface.color[2];
                alphaModeMask   = surface.alphaModeMask;
            }
            if (const NetworkedComponent* nc = app().world().get<NetworkedComponent>(e))
                unpackRgba8(nc->colorRgba8, item.color);
            item.color[3]   = mc.emissive;
            item.castShadow = mc.castShadow;
            item.alphaMask  = alphaModeMask > 0.5f;
            if (m_drawList.submit(item))
                acceptedMesh.insert(e.id());
        });
        app().world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            if (!mc.visible || app().camouflageHides(e))
                return;
            const TransformComponent* xf = app().world().get<TransformComponent>(e);
            const Model* model = app().assets().find<Model>(mc.modelAssetID);
            if (!xf || xf->scale.MagnitudeSqrd() < 1.0e-12f || !model || !model->valid() || !model->hasOpaque() || !gpu.retainModel(app().assets(), *model))
                return;
            GpuModel* gm = gpu.model(model->id);
            if (!gm)
                return;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            if (m_drawList.submitStaticModel(gpu, *gm, worldMat, StaticPopulation::OpaqueModels, mc.castShadow, model->bounds(), nullptr))
                acceptedModel.insert(e.id());
            if (instanceSkinned && model->skinned())
            {
                const AnimGraphComponent* ag = app().world().get<AnimGraphComponent>(e);
                const AnimPose* pose = skinnedPose(*model, ag);
                Matrix4f prevW = worldMat;
                if (ag && ag->prevWorldValid)
                    prevW = ag->prevWorld;
                if (submitSkinnedModelOpaque(m_skinnedInstances, gpu, *gm, pose, worldMat, prevW, StaticPopulation::OpaqueModels, mc.castShadow, model->bounds()))
                    acceptedSkinned.insert(e.id());
            }
        });
        if (WeaponLoadout* w = app().localWeapons())
        {
            const Model* tracer = app().assets().find<Model>(app().m_demo.tracerModelId());
            if (tracer && tracer->hasOpaque() && gpu.retainModel(app().assets(), *tracer))
            {
                if (GpuModel* gm = gpu.model(tracer->id))
                {
                    const float scale = w->projectile().desc().radius * 2.0f;
                    int shot = 0;
                    for (const LiveProjectile& s : w->projectile().live())
                    {
                        const int index = shot++;
                        if (!s.alive)
                            continue;
                        const Matrix4f world = Matrix4f::ScaleMatrixXYZ(scale, scale, scale) * Matrix4f::TranslationMatrix(s.position.x, s.position.y, s.position.z);
                        const Matrix4f prev  = Matrix4f::ScaleMatrixXYZ(scale, scale, scale) * Matrix4f::TranslationMatrix(s.prevPosition.x, s.prevPosition.y, s.prevPosition.z);
                        if (!m_drawList.submitStaticModel(gpu, *gm, world, StaticPopulation::Projectiles, false, tracer->bounds(), &prev))
                            rejectedShots.push_back(index);
                    }
                }
            }
        }
    }

    // Opaque casters only — same set as G-buffer / forward color. Water, particles, blood, lines stay out.
    // A point light culls foliage once against its range sphere and redraws that list on each face.
    // Views are built here. recordDirectionalShadows dispatches them in execute order.
    const bool deferredEarly = app().renderer().scenePath() == ScenePath::HybridDeferred;
    const bool gpuCull       = instanceStatic && app().renderer().debugState().instancedIndirect && m_drawList.cullReady();
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
            const Matrix4f camVp   = m_scene.beginCameraFrame(app().m_viewCamera, app().renderer());
            const Matrix4f camPrev = m_scene.havePrevViewProj() ? m_scene.prevViewProj() : camVp;
            float drawVp[16];
            float drawPrev[16];
            copyMatrix(drawVp, camVp);
            copyMatrix(drawPrev, camPrev);
            m_drawList.buildView(Frustum3f(app().m_viewCamera.GetCullViewProj()), drawVp, drawPrev, RenderPassKind::GBuffer, ViewId{ 0 });
        }
    }

    if (m_shadows.isValid())
        markPass(PassId::Shadows);
    ShadowCasterHost shadowHost{};
    shadowHost.renderer          = &app().renderer();
    shadowHost.world             = &app().world();
    shadowHost.assets            = &app().assets();
    shadowHost.gpu               = &gpu;
    shadowHost.shadows           = &m_shadows;
    shadowHost.drawList          = &m_drawList;
    shadowHost.skinned           = &m_skinnedInstances;
    shadowHost.skinnedShadow     = &m_skinnedShadowPipeline;
    shadowHost.skinRing          = &m_skinRing;
    shadowHost.terrain           = &app().m_terrain;
    shadowHost.foliage           = &app().m_foliagePipeline;
    shadowHost.foliagePrototypes = &app().m_foliagePrototypes;
    shadowHost.foliageRecords    = app().m_foliage.empty() ? nullptr : &app().m_foliage;
    shadowHost.foliageDensity    = &app().m_foliageDensity;
    shadowHost.camera            = &app().m_viewCamera;
    shadowHost.drawTerrain       = app().m_showTerrain;
    shadowHost.instanceStatic    = instanceStatic;
    shadowHost.instanceSkinned   = instanceSkinned;
    shadowHost.rejectTinyScale   = true;
    shadowHost.foliageActive     = true;
    shadowHost.acceptedModel     = &acceptedModel;
    shadowHost.acceptedSkinned   = &acceptedSkinned;
    shadowHost.jobs              = &app().jobs();
    shadowHost.skipModel         = &SandboxApp::skipShadowModel;
    shadowHost.drawPrimitives    = &SandboxApp::drawPrimitiveShadowCasters;
    SandboxShadowUser shadowUser{ &app(), &acceptedMesh };
    shadowHost.user = &shadowUser;
    ensureDirectionalShadowModels(shadowHost);
    recordDirectionalShadows(shadowHost, cmd, shadowViewIndex);

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
                const LocalLightComponent* light = app().world().get<LocalLightComponent>(face.light);
                const TransformComponent* xf = app().world().get<TransformComponent>(face.light);
                if (light && xf && light->type == LocalLightType::Point)
                {
                    pointSphere = Sphere3f(xf->position, Min(light->range, 80.0f));
                    foliageSphere = &pointSphere;
                }
            }
            recordShadowCasterView(shadowHost, cmd, face.viewProj, face.frustum, foliageSphere, face.firstFace, true, shadowViewIndex);
        }
        localShadows.endCapture(cmd);
    }
    else if (localShadows.isValid())
    {
        // An idle frame still has to leave this half of the array as a shader resource.
        localShadows.endCapture(cmd);
    }

    const bool deferred = app().renderer().scenePath() == ScenePath::HybridDeferred;
    const bool visionPicture = app().m_vision.infrared > 0.0f || app().m_vision.ultraviolet > 0.0f || app().m_vision.echo > 0.0f;
    const float underDraw = (deferred && !visionPicture) ? Math::Clamp(app().m_play.runtime().underAmount, 0.0f, 1.0f) : 0.0f;
    const bool instanceColor = instanceStatic && deferred;
    const bool instanceForward = instanceStatic && !deferred && m_drawList.forwardReady();
    const Matrix4f viewProjEarly = m_scene.beginCameraFrame(app().m_viewCamera, app().renderer());
    (void)viewProjEarly;
    if (deferred)
    {
        markPass(PassId::GBuffer);
        app().renderer().bindGBuffer();
        app().renderer().clearGBuffer();
        const bool uvDraw = app().m_vision.ultraviolet > 0.0f;
        const bool thermalDraw = !uvDraw && app().m_vision.infrared > 0.0f;
        m_meshPipeline.setUltraviolet(uvDraw);
        m_skinnedPipeline.setUltraviolet(uvDraw);
        m_terrainPipeline.setUltraviolet(uvDraw);
        app().m_foliagePipeline.setUltraviolet(uvDraw);
        app().m_grassPipeline.setUltraviolet(uvDraw);
        m_drawList.setUltraviolet(uvDraw);
        m_skinnedInstances.setUltraviolet(uvDraw);
        m_meshPipeline.setThermal(thermalDraw);
        m_skinnedPipeline.setThermal(thermalDraw);
        m_terrainPipeline.setThermal(thermalDraw);
        app().m_foliagePipeline.setThermal(thermalDraw);
        app().m_grassPipeline.setThermal(thermalDraw);
        m_drawList.setThermal(thermalDraw);
        m_skinnedInstances.setThermal(thermalDraw);
    }
    else if (app().renderer().hasSceneBuffers())
    {
        app().renderer().bindHdr(true);
        app().renderer().clearHdr();
    }
    else
    {
        app().renderer().bindSceneTargets();
    }

    const Matrix4f  viewProj = app().m_viewCamera.GetViewProj();
    const Matrix4f  prevViewProj = m_scene.havePrevViewProj() ? m_scene.prevViewProj() : viewProj;
    const Frustum3f frustum(app().m_viewCamera.GetCullViewProj());
    const DebugFill fill     = app().renderer().debugState().fill;
    const Vector3f  camPos   = app().m_viewCamera.GetPosition();
    uint32_t        meshDraws = 0;

    const float skyExposure = deferred ? 1.0f : app().m_env.exposure();
    const float fogScale    = (app().renderer().debugState().lightingActive() ? 1.0f : 0.0f) * (1.0f - underDraw);
    const float ultraviolet = (deferred && app().m_vision.ultraviolet > 0.0f) ? app().m_vision.ultraviolet : 0.0f;
    const float thermal     = (deferred && !(app().m_vision.ultraviolet > 0.0f)) ? app().m_vision.infrared : 0.0f;
    m_skyPipeline.upload(app().renderer().frameIndex(), app().m_viewCamera, app().m_env, skyExposure, app().m_water.params().waterLevel, fogScale, thermal, ultraviolet);
    if (!deferred)
    {
        markPass(PassId::Sky);
        m_skyPipeline.draw(cmd, app().renderer().frameIndex(), &m_shadows);
    }

    const Material* material = nullptr;
    if (app().m_demo.cube().valid())
    {
        if (auto* meshComp = app().world().get<MeshComponent>(app().m_demo.cube()))
            material = app().assets().find<Material>(meshComp->matAssetID);
        else if (const ModelComponent* mo = app().world().get<ModelComponent>(app().m_demo.cube()))
        {
            if (const Model* model = app().assets().find<Model>(mo->modelAssetID))
            {
                if (const Model::Part* part = model->partAt(0))
                    material = part->material.get();
            }
        }
    }

    if (deferred)
    {
        {
            const GpuScope gbuffer(cmd, "GBuffer", ProfileColor::GBuffer);
            if (app().m_showTerrain)
                app().m_terrain.drawGBuffer(cmd, m_terrainPipeline, app().m_terrainMaterial, app().m_viewCamera, &frustum, &app().renderer().debugState(), &prevViewProj);
            if (app().m_grassPipeline.isValid() && app().m_grassField.params().enabled)
            {
                const GpuScope      grassScope(cmd, "Grass", ProfileColor::Grass);
                GrassFrameConstants grassFrame{};
                copyMatrix(grassFrame.viewProj, viewProj);
                copyMatrix(grassFrame.prevViewProj, prevViewProj);
                grassFrame.heightMetres = app().m_grassField.params().heightMetres;
                grassFrame.flexibility  = app().m_grassField.params().flexibility;
                grassFrame.faceEdge     = app().m_grassField.params().faceEdge;
                grassFrame.faceYaw      = app().m_grassField.params().faceYaw;
                grassFrame.faceLean     = app().m_grassField.params().faceLean;
                grassFrame.faceSteep    = app().m_grassField.params().faceSteep;
                grassFrame.cameraPos[0]   = camPos.x;
                grassFrame.cameraPos[1]   = camPos.y;
                grassFrame.cameraPos[2]   = camPos.z;
                grassFrame.cameraRight[0] = app().m_viewCamera.GetRight().x;
                grassFrame.cameraRight[1] = app().m_viewCamera.GetRight().y;
                grassFrame.cameraRight[2] = app().m_viewCamera.GetRight().z;
                grassFrame.cameraLook[0]  = app().m_viewCamera.GetLook().x;
                grassFrame.cameraLook[1]  = app().m_viewCamera.GetLook().y;
                grassFrame.cameraLook[2]  = app().m_viewCamera.GetLook().z;
                app().m_grassField.writeFrameInteraction(grassFrame);
                GrassLodSpan grassLods[kGrassLodCount]{};
                for (int lod = 0; lod < kGrassLodCount; ++lod)
                {
                    const GrassField::BladeSpan span = app().m_grassField.blades(lod);
                    grassLods[lod].blades            = span.data;
                    grassLods[lod].count             = span.count;
                    grassLods[lod].staleMask         = app().m_grassField.lodStaleMask(lod);
                }
                const uint32_t residentTiles = grassRegionBudget().residentTiles;
                std::vector<GrassTileDraw> grassTiles(residentTiles);
                const uint32_t grassDraws = app().m_grassField.visibleTiles(frustum, grassTiles.data(), residentTiles);
                // This slot has seen the CPU blades. Leave the other slot stale until its fence.
                if (app().m_grassPipeline.draw(cmd, app().renderer(), grassLods, app().m_grassField.tileWind(), app().m_grassField.tileWindCount(), grassFrame, grassTiles.data(), grassDraws))
                {
                    const uint32_t slot = app().renderer().frameIndex() % kGrassFrameSlots;
                    for (int lod = 0; lod < kGrassLodCount; ++lod)
                        app().m_grassField.clearLodStale(lod, slot);
                }
            }
            if (instanceColor && !m_drawList.gpuCullActive())
            {
                float drawVp[16];
                float drawPrev[16];
                copyMatrix(drawVp, viewProj);
                copyMatrix(drawPrev, prevViewProj);
                m_drawList.buildView(frustum, drawVp, drawPrev, RenderPassKind::GBuffer, ViewId{ 0 });
            }
            {
                const GpuScope meshes(cmd, "Opaque Meshes", ProfileColor::OpaqueMeshes);
                if (instanceColor)
                    m_drawList.recordOpaque(cmd, gpu, ViewId{ 0 }, StaticPopulation::OpaqueMeshes);
                m_meshPipeline.bind(cmd, fill);
        if (material && material->isValid())
            app().renderer().gpuResources().bindMaterial(cmd, *material, MeshPipeline::kRootAlbedoSrv);
        MeshGBufferConstants gcb{};
        if (material)
            applyMaterialSurface(*material, gcb);
        else
        {
            gcb.color[0] = 1.0f;
            gcb.color[1] = 1.0f;
            gcb.color[2] = 1.0f;
            gcb.color[3] = 0.0f;
        }
        app().world().each<MeshComponent>([&](Entity e, MeshComponent& mc) {
            if (app().world().has<ModelComponent>(e))
                return;
            if (const HealthComponent* hp = app().world().get<HealthComponent>(e); hp && !hp->health.alive())
                return;
            const Mesh* gpuMesh = sandboxPrimitiveMesh(mc.primitive, app().m_demo.cubeMesh(), app().m_crossMesh);
            if (!gpuMesh)
                return;
            const TransformComponent* xf = app().world().get<TransformComponent>(e);
            if (!xf)
                return;
            if (const Material* mat = app().assets().find<Material>(mc.matAssetID))
            {
                gpu.retainMaterial(app().assets(), *mat);
                gpu.bindMaterial(cmd, *mat, MeshPipeline::kRootAlbedoSrv);
                applyMaterialSurface(*mat, gcb);
            }
            const Matrix4f worldMat  = makeWorldMatrix(*xf);
            if (instanceColor && acceptedMesh.contains(e.id()))
            {
                m_prevWorldByEntity[e.id()] = worldMat;
                return;
            }
            const Matrix4f prevWorld = m_prevWorldByEntity.count(e.id()) ? m_prevWorldByEntity[e.id()] : worldMat;
            fillMeshGBufferXforms(gcb, worldMat, viewProj, prevViewProj, prevWorld);
            if (const NetworkedComponent* nc = app().world().get<NetworkedComponent>(e))
                unpackRgba8(nc->colorRgba8, gcb.color);
            gcb.color[3] = mc.emissive;
            m_meshPipeline.setGBufferConstants(cmd, gcb);
            gpuMesh->draw(cmd, fill == DebugFill::Points);
            m_prevWorldByEntity[e.id()] = worldMat;
            ++meshDraws;
        });
            }
            drawProjectilesGBuffer(cmd, viewProj, prevViewProj, instanceColor, &rejectedShots);
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
                app().world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            if (!mc.visible || app().camouflageHides(e))
                return;
            const TransformComponent* xf = app().world().get<TransformComponent>(e);
            const Model* model = app().assets().find<Model>(mc.modelAssetID);
            if (!xf || xf->scale.MagnitudeSqrd() < 1.0e-12f || !model || !model->hasOpaque() || !gpu.retainModel(app().assets(), *model))
                return;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            const bool accepted = instanceColor && acceptedModel.contains(e.id());
            if (model->skinned())
            {
                AnimGraphComponent* ag = app().world().get<AnimGraphComponent>(e);
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
                const bool skinnedListed = instanceSkinned && acceptedSkinned.contains(e.id());
                if (accepted && skinnedListed)
                    return;
                if (accepted)
                    drawSkinnedModelOpaqueGBufferSkinnedOnly(cmd, gpu, m_skinnedPipeline, m_meshPipeline, m_skinRing, *model, *pose, worldMat, prevW, viewProj, prevViewProj, fill);
                else if (skinnedListed)
                    drawModelOpaqueGBuffer(cmd, gpu, m_meshPipeline, *model, worldMat, viewProj, prevViewProj, fill);
                else
                    drawSkinnedModelOpaqueGBuffer(cmd, gpu, m_skinnedPipeline, m_meshPipeline, m_skinRing, *model, *pose, worldMat, prevW, viewProj, prevViewProj, fill);
            }
            else if (!accepted)
                drawModelOpaqueGBuffer(cmd, gpu, m_meshPipeline, *model, worldMat, viewProj, prevViewProj, fill);
        });
            }
            if (app().m_terrain.valid())
                app().m_foliagePipeline.drawGBuffer(cmd, app().renderer(), app().assets(), app().m_foliagePrototypes, app().m_terrain, app().m_foliage.empty() ? nullptr : &app().m_foliage, app().m_foliageDensity, app().m_viewCamera, viewProj, prevViewProj, frustum);
        }

        m_meshPipeline.setUltraviolet(false);
        m_skinnedPipeline.setUltraviolet(false);
        m_terrainPipeline.setUltraviolet(false);
        app().m_foliagePipeline.setUltraviolet(false);
        app().m_grassPipeline.setUltraviolet(false);
        m_drawList.setUltraviolet(false);
        m_skinnedInstances.setUltraviolet(false);
        m_meshPipeline.setThermal(false);
        m_skinnedPipeline.setThermal(false);
        m_terrainPipeline.setThermal(false);
        app().m_foliagePipeline.setThermal(false);
        app().m_grassPipeline.setThermal(false);
        m_drawList.setThermal(false);
        m_skinnedInstances.setThermal(false);

        LightingConstants lc{};
        copyMatrix(lc.invViewProj, viewProj.Inverse());
        lc.cameraPos[0]     = camPos.x;
        lc.cameraPos[1]     = camPos.y;
        lc.cameraPos[2]     = camPos.z;
        lc.lightDirWS[0]    = app().m_env.lightDir().x;
        lc.lightDirWS[1]    = app().m_env.lightDir().y;
        lc.lightDirWS[2]    = app().m_env.lightDir().z;
        lc.lighting         = app().renderer().debugState().lightingActive() ? 1.0f : 0.0f;
        lc.lightColor[0]    = app().m_env.lightColor().x;
        lc.lightColor[1]    = app().m_env.lightColor().y;
        lc.lightColor[2]    = app().m_env.lightColor().z;
        {
            const Vector3f pbrSun = pbrSunLightColor(app().m_env.lightColor());
            lc.pbrLightColor[0]   = pbrSun.x;
            lc.pbrLightColor[1]   = pbrSun.y;
            lc.pbrLightColor[2]   = pbrSun.z;
        }
        lc.emissiveGain     = 4.0f;
        lc.ambientColor[0]  = app().m_env.ambientColor().x;
        lc.ambientColor[1]  = app().m_env.ambientColor().y;
        lc.ambientColor[2]  = app().m_env.ambientColor().z;
        FogGpu fog = makeFogGpu(&app().m_env, app().m_water.params().waterLevel, lc.lighting > 0.5f);
        const float air = 1.0f - underDraw;
        fog.fogDensity *= air;
        fog.heightFogDensity *= air;
        fog.volumetricFogDensity *= air;
        fillFogHeightMap(fog, &app().m_terrain.coarse());
        applyFogToLighting(lc, fog);
        fillIblLightingConstants(lc, app().m_ibl, app().renderer().debugState().iblEnabled, app().renderer().debugState().iblDebug, iblGpuReady(app().renderer().gpuResources(), app().m_iblImageId));
        lc.iblNight = 1.0f - app().m_env.dayFactor();
        lc.thermal  = app().m_vision.ultraviolet > 0.0f ? -app().m_vision.ultraviolet : app().m_vision.infrared;
        fillSsrLightingConstants(lc, app().m_ssr, app().renderer().debugState().ssrEnabled, m_scene.ssr().isValid() && app().renderer().hasGBuffer());
        // The pass list is an order check after the frame. These five run inside resolveDeferred in this order.
        markPass(PassId::Decals);
        markPass(PassId::Gtao);
        markPass(PassId::Ssr);
        markPass(PassId::Lighting);
        markPass(PassId::LocalVolumes);
        DeferredResolveInput resolve;
        resolve.viewProj     = &viewProj;
        resolve.prevViewProj = &prevViewProj;
        resolve.gtao         = &app().m_ssao;
        resolve.ssr          = &app().m_ssr;
        resolve.ssrEnv       = &app().m_env;
        resolve.lighting     = &lc;
        {
            const FacilityState& facility = app().m_play.facility();
            m_scene.setFogOccluders(facility.blocks, facility.blockCount);
        }
        m_scene.resolveDeferred(cmd, app().renderer(), app().world(), app().m_viewCamera, resolve);

        app().renderer().bindHdr(true);
        markPass(PassId::Sky);
        m_skyPipeline.draw(cmd, app().renderer().frameIndex(), &m_shadows);
    }
    else
    {
        if (!app().m_loggedForwardGrassSkip && app().m_grassPipeline.isValid() && app().m_grassField.params().enabled)
        {
            app().m_loggedForwardGrassSkip = true;
            DE_LOG_INFO(LogCategory::Render, "SandboxApp: forward frame skips grass");
        }
        const GpuScope forward(cmd, "Forward Opaque", ProfileColor::ForwardOpaque);
        if (app().m_showTerrain)
        {
            app().m_terrain.draw(
                cmd, m_terrainPipeline, app().m_terrainMaterial, app().m_viewCamera, &frustum, &app().m_env, &m_shadows,
                &app().renderer().debugState());
        }

        m_meshPipeline.bind(cmd, fill);
        if (material && material->isValid())
            app().renderer().gpuResources().bindMaterial(cmd, *material, MeshPipeline::kRootAlbedoSrv);
        m_shadows.bindReceiverCbv(cmd, MeshPipeline::kRootShadowCbv);

        MeshFrameConstants cb{};
        if (material)
            applyMaterialSurface(*material, cb);
        else
        {
            cb.color[0] = 1.0f;
            cb.color[1] = 1.0f;
            cb.color[2] = 1.0f;
            cb.color[3] = 1.0f;
        }
        cb.lightDirWS[0] = app().m_env.lightDir().x;
        cb.lightDirWS[1] = app().m_env.lightDir().y;
        cb.lightDirWS[2] = app().m_env.lightDir().z;
        cb.ambientScale  = 0.22f;
        {
            const Vector3f pbrSun = pbrSunLightColor(app().m_env.lightColor());
            cb.lightColor[0]      = pbrSun.x;
            cb.lightColor[1]      = pbrSun.y;
            cb.lightColor[2]      = pbrSun.z;
        }
        cb.cameraPos[0]  = camPos.x;
        cb.cameraPos[1]  = camPos.y;
        cb.cameraPos[2]  = camPos.z;
        cb.lighting      = app().renderer().debugState().lightingActive() ? 1.0f : 0.0f;
        if (instanceForward && !m_drawList.gpuCullActive())
        {
            float drawVp[16];
            float drawPrev[16];
            copyMatrix(drawVp, viewProj);
            copyMatrix(drawPrev, prevViewProj);
            m_drawList.buildView(frustum, drawVp, drawPrev, RenderPassKind::GBuffer, ViewId{ 0 });
        }
        MeshForwardBatchConstants forwardLighting{};
        fillForwardLighting(forwardLighting, cb);
        const D3D12_GPU_VIRTUAL_ADDRESS forwardShadow = m_shadows.receiverCbvGpu();

        {
            const GpuScope meshes(cmd, "Opaque Meshes", ProfileColor::OpaqueMeshes);
            if (instanceForward)
            {
                m_drawList.recordForward(cmd, gpu, ViewId{ 0 }, StaticPopulation::OpaqueMeshes, forwardLighting, forwardShadow);
                m_meshPipeline.bind(cmd, fill);
                if (material && material->isValid())
                    app().renderer().gpuResources().bindMaterial(cmd, *material, MeshPipeline::kRootAlbedoSrv);
                m_shadows.bindReceiverCbv(cmd, MeshPipeline::kRootShadowCbv);
            }
            app().world().each<MeshComponent>([&](Entity e, MeshComponent& mc) {
            if (app().world().has<ModelComponent>(e))
                return;
            if (const HealthComponent* hp = app().world().get<HealthComponent>(e); hp && !hp->health.alive())
                return;
            const Mesh* gpuMesh = sandboxPrimitiveMesh(mc.primitive, app().m_demo.cubeMesh(), app().m_crossMesh);
            if (!gpuMesh)
                return;
            const TransformComponent* xf = app().world().get<TransformComponent>(e);
            if (!xf)
                return;
            if (instanceForward && acceptedMesh.contains(e.id()))
                return;
            if (const Material* mat = app().assets().find<Material>(mc.matAssetID))
            {
                gpu.retainMaterial(app().assets(), *mat);
                gpu.bindMaterial(cmd, *mat, MeshPipeline::kRootAlbedoSrv);
                applyMaterialSurface(*mat, cb);
            }
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            copyMatrix(cb.worldViewProj, worldMat * viewProj);
            copyMatrix(cb.world, worldMat);
            if (const NetworkedComponent* nc = app().world().get<NetworkedComponent>(e))
                unpackRgba8(nc->colorRgba8, cb.color);
            m_meshPipeline.setConstants(cmd, cb);
            gpuMesh->draw(cmd, fill == DebugFill::Points);
            ++meshDraws;
        });
        }

        drawProjectiles(cmd, viewProj, cb, instanceForward, &rejectedShots);
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
            app().world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            if (!mc.visible || app().camouflageHides(e))
                return;
            const TransformComponent* xf = app().world().get<TransformComponent>(e);
            const Model* model = app().assets().find<Model>(mc.modelAssetID);
            if (!xf || xf->scale.MagnitudeSqrd() < 1.0e-12f || !model || !model->hasOpaque() || !gpu.retainModel(app().assets(), *model))
                return;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            if (model->skinned())
            {
                AnimGraphComponent* ag = app().world().get<AnimGraphComponent>(e);
                const AnimPose* pose = skinnedPose(*model, ag);
                if (!pose)
                    return;
                if (ag)
                {
                    ag->prevWorld = worldMat;
                    ag->prevWorldValid = true;
                }
                const bool accepted = instanceForward && acceptedModel.contains(e.id());
                const bool skinnedListed = instanceSkinned && acceptedSkinned.contains(e.id());
                if (accepted && skinnedListed)
                    return;
                if (accepted)
                    drawSkinnedModelForwardSkinnedOnly(cmd, gpu, m_skinnedPipeline, m_meshPipeline, m_shadows, m_skinRing, *model, *pose, worldMat, viewProj, cb, fill);
                else if (skinnedListed)
                    drawModelForward(cmd, gpu, m_meshPipeline, m_shadows, *model, false, worldMat, viewProj, cb, fill);
                else
                    drawSkinnedModelForward(cmd, gpu, m_skinnedPipeline, m_meshPipeline, m_shadows, m_skinRing, *model, false, *pose, worldMat, viewProj, cb, fill);
            }
            else if (!(instanceForward && acceptedModel.contains(e.id())))
                drawModelForward(cmd, gpu, m_meshPipeline, m_shadows, *model, false, worldMat, viewProj, cb, fill);
        });
        }
    }

    D3D12_GPU_VIRTUAL_ADDRESS waterLightsVa   = m_localLightGpu.isValid() ? m_localLightGpu.dummyGpuVa() : 0;
    uint32_t                  waterLightCount = 0;
    uint32_t                  waterIndex[kWaterLocalLightMax]{};
    if (app().renderer().debugState().localLights && app().renderer().debugState().lightingActive() && m_localLightGpu.isValid())
    {
        LocalLightCullInput in{};
        in.frustum    = &frustum;
        in.cameraPos  = camPos;
        in.cameraLook = app().m_viewCamera.GetLook();
        in.nearZ      = app().m_viewCamera.GetNearZ();
        in.viewportW  = app().renderer().width();
        in.viewportH  = app().renderer().height();
        in.viewProj      = &viewProj;
        in.localShadows  = &m_scene.localShadows();
        in.fogBoxes      = m_scene.fogOccluders();
        in.fogBoxCount   = m_scene.fogOccluderCount();
        LocalLightDrawLists lists{};
        if (gatherLocalLights(app().world(), in, lists) && lists.count > 0)
        {
            m_localLightGpu.upload(app().renderer().frameIndex(), lists);
            waterLightCount = lists.waterCount;
            std::memcpy(waterIndex, lists.waterIndex, sizeof(waterIndex));
            waterLightsVa = m_localLightGpu.lightsGpuVa();
        }
    }

    ID3D12DescriptorHeap*         heightHeap = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE   heightGpu{};
    if (app().m_terrain.heightTexture().valid())
    {
        heightHeap = app().m_terrain.heightTexture().srvHeap();
        heightGpu  = app().m_terrain.heightTexture().gpuHandle();
    }
    else if (app().renderer().lightingHeap())
    {
        heightHeap = app().renderer().lightingHeap();
        heightGpu  = app().renderer().heightTableGpu();
    }
    if (app().renderer().hasSceneBuffers())
    {
        markPass(PassId::SsrCapture);
        m_scene.captureSsrSceneColor(cmd, app().renderer());
    }
    D3D12_CPU_DESCRIPTOR_HANDLE waterSceneColor{};
    D3D12_CPU_DESCRIPTOR_HANDLE waterDepth{};
    const SsrSettings*          waterSsr = nullptr;
    if (m_scene.ssr().hasSceneColor())
    {
        waterSceneColor = m_scene.ssr().sceneColorSrvCpu();
        waterDepth      = app().renderer().depthSrvCpu();
        waterSsr        = &app().m_ssr;
    }
    const bool waterSheet = app().m_water.wetChunkCount() > 0;
    if (waterSheet || !app().m_streams.empty())
    {
        markPass(PassId::Water);
        const GpuScope waterScope(cmd, "Water", ProfileColor::Water);
        uint32_t drawIndex = 0;
        if (waterSheet)
        {
            app().m_water.draw(
                cmd,
                m_waterPipeline,
                app().m_viewCamera,
                &frustum,
                &app().m_env,
                &app().renderer().debugState(),
                waterLightsVa,
                waterLightCount,
                waterIndex,
                app().renderer().frameIndex(),
                heightHeap,
                heightGpu,
                &m_shadows,
                waterSceneColor,
                waterDepth,
                waterSsr,
                drawIndex,
                thermal,
                ultraviolet,
                underDraw);
            drawIndex = 1;
        }
        for (const SandboxApp::SandboxStream& stream : app().m_streams)
        {
            if (drawIndex >= WaterPipeline::kMaxWaterDrawsPerFrame)
            {
                DE_LOG_WARN(LogCategory::Render, "SandboxApp: water draw cap {} reached", WaterPipeline::kMaxWaterDrawsPerFrame);
                break;
            }
            WaterParams streamParams = app().m_water.params();
            streamParams.flowSpeed = stream.flowSpeed;
            if (drawStreamRibbon(
                    cmd,
                    m_waterPipeline,
                    stream.mesh,
                    stream.bounds,
                    streamParams,
                    app().m_viewCamera,
                    &frustum,
                    &app().renderer().debugState(),
                    app().m_water.time(),
                    app().renderer().frameIndex(),
                    drawIndex,
                    heightHeap,
                    heightGpu,
                    &m_shadows,
                    waterSceneColor,
                    waterDepth,
                    waterSsr,
                    &app().m_terrain.coarse(),
                    &app().m_env,
                    thermal,
                    ultraviolet))
                ++drawIndex;
        }
    }

    if (app().renderer().hasGBuffer())
    {
        markPass(PassId::Clouds);
        app().renderer().bindHdrDepthRead();
        CloudVolumeFrame cf{};
        cf.sunDir       = app().m_env.lightDir();
        cf.sunColor     = app().m_env.lightColor();
        cf.ambientColor = app().m_env.ambientColor();
        cf.time         = app().m_cloudTime;
        cf.lod          = app().m_cloudLod;
        m_scene.drawCloudVolumes(cmd, app().renderer(), app().world(), app().m_viewCamera, viewProj, cf);
    }

    bool lateDepthWrite = false;
    if (app().m_vision.camouflage && app().renderer().hasSceneBuffers() && m_scene.camouflage().isValid())
    {
        markPass(PassId::Camouflage);
        const Entity body = app().possessedBody();
        const TransformComponent* xf = body.valid() ? app().world().get<TransformComponent>(body) : nullptr;
        const ModelComponent* mc = body.valid() ? app().world().get<ModelComponent>(body) : nullptr;
        const Model* model = (mc && xf) ? app().assets().find<Model>(mc->modelAssetID) : nullptr;
        if (xf && model && gpu.retainModel(app().assets(), *model))
        {
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            AnimGraphComponent* ag = app().world().get<AnimGraphComponent>(body);
            const AnimPose* pose = model->skinned() ? skinnedPose(*model, ag) : nullptr;
            if (ag)
            {
                ag->prevWorld      = worldMat;
                ag->prevWorldValid = true;
            }
            lateDepthWrite = m_scene.camouflage().drawModel(cmd, app().renderer(), gpu, m_skinRing, *model, pose, worldMat, viewProj, camPos, app().m_cloudTime, fill);
        }
    }

    {
        markPass(PassId::Translucent);
        const GpuScope translucent(cmd, "Translucent", ProfileColor::Translucent);
        MeshFrameConstants lit{};
        lit.lightDirWS[0] = app().m_env.lightDir().x;
        lit.lightDirWS[1] = app().m_env.lightDir().y;
        lit.lightDirWS[2] = app().m_env.lightDir().z;
        lit.ambientScale  = 0.22f;
        {
            const Vector3f pbrSun = pbrSunLightColor(app().m_env.lightColor());
            lit.lightColor[0]     = pbrSun.x;
            lit.lightColor[1]     = pbrSun.y;
            lit.lightColor[2]     = pbrSun.z;
        }
        lit.cameraPos[0]  = camPos.x;
        lit.cameraPos[1]  = camPos.y;
        lit.cameraPos[2]  = camPos.z;
        lit.lighting      = app().renderer().debugState().lightingActive() ? 1.0f : 0.0f;
        std::vector<Entity> translucentEntities;
        std::vector<TranslucentDraw> translucentOrder;
        app().world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            if (!mc.visible || app().camouflageHides(e))
                return;
            const TransformComponent* xf = app().world().get<TransformComponent>(e);
            const Model* model = app().assets().find<Model>(mc.modelAssetID);
            if (!xf || xf->scale.MagnitudeSqrd() < 1.0e-12f || !model || !model->hasTranslucent() || !gpu.retainModel(app().assets(), *model))
                return;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            if (model->skinned() && !skinnedPose(*model, app().world().get<AnimGraphComponent>(e)))
                return;
            TranslucentDraw draw;
            draw.user      = static_cast<uint32_t>(translucentEntities.size());
            draw.worldAabb = model->bounds().IsValid() ? model->bounds().Transformed(worldMat) : AABox3f::Empty();
            translucentEntities.push_back(e);
            translucentOrder.push_back(draw);
        });
        sortTranslucent(translucentOrder.data(), static_cast<uint32_t>(translucentOrder.size()), app().m_viewCamera.GetView());
        for (const TranslucentDraw& draw : translucentOrder)
        {
            const Entity e = translucentEntities[draw.user];
            const ModelComponent* mc = app().world().get<ModelComponent>(e);
            const TransformComponent* xf = app().world().get<TransformComponent>(e);
            const Model* model = (mc && xf) ? app().assets().find<Model>(mc->modelAssetID) : nullptr;
            if (!xf || !model || !gpu.retainModel(app().assets(), *model))
                continue;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            if (model->skinned())
            {
                const AnimPose* pose = skinnedPose(*model, app().world().get<AnimGraphComponent>(e));
                if (!pose)
                    continue;
                drawSkinnedModelForward(cmd, gpu, m_skinnedTransparentPipeline, m_meshTransparentPipeline, m_shadows, m_skinRing, *model, true, *pose, worldMat, viewProj, lit, fill);
            }
            else
                drawModelForward(cmd, gpu, m_meshTransparentPipeline, m_shadows, *model, true, worldMat, viewProj, lit, fill);
        }
    }

    if (app().m_chaseOk && (app().m_demoScene || app().m_showDevTools))
    {
        const GpuScope paths(cmd, "Path Debug", ProfileColor::DebugOverlay);
        app().m_chase.drawPaths(cmd, app().renderer(), viewProj);
    }
    app().m_demo.drawSkeletonOverlay(cmd, viewProj);
    m_scene.drawDecalVolumes(cmd, app().renderer(), app().m_chase.linePipeline(), viewProj);

    {
        markPass(PassId::Particles);
        const GpuScope particles(cmd, "Particles", ProfileColor::Particles);
        app().m_gpuParticles.simulateAndDraw(cmd, app().renderer(), app().m_viewCamera, app().world(), app().assets(), lateDepthWrite);
        app().m_particles.beginFrame(app().renderer().frameIndex());
    if (app().m_blood.aliveCount() > 0)
        app().m_particles.draw(cmd, app().m_viewCamera, app().m_blood, false);
    app().world().each<ParticleEmitterComponent>([&](Entity, ParticleEmitterComponent& pe) {
        if (!pe.runtime)
            return;
        const Material* sprite = nullptr;
        if (const Material* mat = app().assets().find<Material>(pe.matAssetID))
        {
            gpu.retainMaterial(app().assets(), *mat);
            sprite = mat;
        }
        app().m_particles.draw(cmd, app().m_viewCamera, *pe.runtime, pe.runtime->desc().additiveBlend, sprite);
    });
    if (WeaponLoadout* wFx = app().localWeapons(); wFx && wFx->projectile().impactEmitter().aliveCount() > 0)
        app().m_particles.draw(cmd, app().m_viewCamera, wFx->projectile().impactEmitter(), true);
    app().m_leafRenderer.draw(cmd, app().m_viewCamera, app().m_leafField);
    }

    if (!app().deferredDecals())
        app().m_bloodSplats.draw(cmd, app().m_viewCamera);

    if (underDraw > 0.0f && app().renderer().hasSceneBuffers() && m_scene.underwater().isValid())
    {
        markPass(PassId::Underwater);
        UnderwaterFrame under{};
        copyMatrix(under.invViewProj, viewProj.Inverse());
        under.cameraPos[0] = camPos.x;
        under.cameraPos[1] = camPos.y;
        under.cameraPos[2] = camPos.z;
        under.waterLevel   = app().m_water.params().waterLevel;
        under.amount       = underDraw;
        m_scene.underwater().draw(cmd, app().renderer(), under);
        app().renderer().bindHdr(true);
        app().m_water.draw(
            cmd,
            m_waterPipeline,
            app().m_viewCamera,
            &frustum,
            &app().m_env,
            &app().renderer().debugState(),
            waterLightsVa,
            waterLightCount,
            waterIndex,
            app().renderer().frameIndex(),
            heightHeap,
            heightGpu,
            &m_shadows,
            waterSceneColor,
            waterDepth,
            waterSsr,
            0,
            thermal,
            ultraviolet,
            underDraw,
            true);
    }

    {
        const bool aces = useAcesTonemap(app().renderer());
        TonemapSettings post = app().playerPostFx();
        post.mode            = aces ? 1.0f : 0.0f;
        float exposure       = app().m_env.exposure();
        app().m_autoExposureResult = {};
        app().m_autoExposureResult.finalExposure = exposure;
        const bool deferredMeter = app().renderer().scenePath() == ScenePath::HybridDeferred && m_scene.autoExposure().isValid();
        if (deferredMeter)
        {
            // Resize drops the 1×1. Zero the correction before this frame can apply a stale sample.
            if (m_scene.syncAutoExposureSize(app().renderer()))
                resetAutoExposure(app().m_autoExposureState);
            // Infrared, ultraviolet, echolocation, and the underwater column swing the meter. Hold the correction while any is on.
            const bool holdExposure = app().m_vision.infrared > 0.0f || app().m_vision.ultraviolet > 0.0f || app().m_vision.echo > 0.0f || app().m_play.runtime().underAmount > 0.0f;
            const float measured = (app().m_autoExposure.mode == ExposureMode::Auto && !holdExposure)
                ? m_scene.autoExposure().readMeasuredLuma(app().renderer().frameIndex())
                : 0.0f;
            app().m_autoExposureResult = adaptExposure(app().m_autoExposureState, measured, exposure, app().m_frameDt, app().m_autoExposure);
            exposure             = app().m_autoExposureResult.finalExposure;
        }
        post.exposure = exposure;
        markPass(PassId::Post);
        const bool echoImage = deferred && app().m_vision.echo > 0.0f && app().renderer().hasGBuffer() && m_scene.echo().isValid();
        if (echoImage)
        {
            EchoPipeline& echo = m_scene.echo();
            bool capturedThisFrame = false;
            if (app().m_vision.echoCapture)
            {
                EchoSnapshotFrame snap{};
                copyMatrix(snap.invViewProj, viewProj.Inverse());
                snap.cameraPos[0] = camPos.x;
                snap.cameraPos[1] = camPos.y;
                snap.cameraPos[2] = camPos.z;
                if (echo.capture(cmd, app().renderer(), snap))
                {
                    app().m_vision.echoChirpViewProj = viewProj;
                    app().m_vision.echoChirpPos      = camPos;
                    app().m_vision.echoHaveChirp     = true;
                    app().m_vision.echoCapture       = false;
                    capturedThisFrame   = true;
                }
            }
            if (app().m_vision.echoHaveChirp && echo.hasSnapshot())
            {
                EchoCompositeFrame comp{};
                copyMatrix(comp.currInvViewProj, viewProj.Inverse());
                copyMatrix(comp.chirpViewProj, app().m_vision.echoChirpViewProj);
                comp.chirpCamera[0] = app().m_vision.echoChirpPos.x;
                comp.chirpCamera[1] = app().m_vision.echoChirpPos.y;
                comp.chirpCamera[2] = app().m_vision.echoChirpPos.z;
                float radius = Math::Min(app().m_vision.echoChirpAge * app().m_vision.echoChirp.speedMetersPerSec, app().m_vision.echoChirp.rangeMeters);
                if (capturedThisFrame)
                    radius = Math::Max(radius, EchoPipeline::kEdgeMeters);
                comp.radius   = radius;
                comp.strength = app().m_vision.echo;
                comp.maxRange = app().m_vision.echoChirp.rangeMeters;
                echo.composite(cmd, app().renderer(), comp);
            }
        }
        const bool holdExposure = app().m_vision.infrared > 0.0f || app().m_vision.ultraviolet > 0.0f || app().m_vision.echo > 0.0f || app().m_play.runtime().underAmount > 0.0f;
        m_scene.applyPost(app().renderer(), cmd, viewProj, post, deferredMeter && app().m_autoExposure.mode == ExposureMode::Auto && !holdExposure, app().m_vision.echo > 0.0f);
    }

    m_scene.endCameraFrame(app().m_viewCamera, viewProj);

    Health* hudHp = app().localHealth();
    const Entity hudBody = app().possessedBody();
    const HudTagComponent* hudTag = hudBody.valid() ? app().world().get<HudTagComponent>(hudBody) : nullptr;
    const bool movieBlocks = app().m_movie.blocksWorld();
    const bool drawHealth = !movieBlocks && !app().m_hud.m_menu.visible() && hudHp && hudTag && hudTag->kind == HudKind::HealthBar;
    const bool drawCrosshair = !movieBlocks && !app().m_hud.m_menu.visible() && hudHp && hudHp->alive() && !app().m_gameplayPaused && !app().m_dialogue.locksWorld();
    if (drawHealth || drawCrosshair)
    {
        const GpuScope hud(cmd, "HUD", ProfileColor::Hud);
        if (drawHealth)
            app().m_healthHud.draw(cmd, app().renderer().width(), app().renderer().height(), hudHp->ratio(), app().window().dpiScale());
        if (drawCrosshair)
        {
            bool throwableSlot = false;
            if (hudBody.valid())
            {
                if (const ThrowableBeltComponent* carried = app().world().get<ThrowableBeltComponent>(hudBody))
                    throwableSlot = carried->belt.any();
            }
            const WeaponKind hudKind = app().localWeapons() ? app().localWeapons()->activeKind() : WeaponKind::Melee;
            app().m_crosshair.draw(cmd, app().renderer().width(), app().renderer().height(), hudKind, throwableSlot, app().window().dpiScale());
        }
    }

    app().renderer().stats().drawCalls = app().m_terrain.lastDrawCalls() + app().m_water.lastDrawCalls() + meshDraws + 1;
    app().renderer().stats().triangles =
        app().m_terrain.lastTriangles() + app().m_water.lastTriangles() + meshDraws * (app().m_demo.cubeMesh().indexCount() / 3);

    const bool ssaoTile = app().renderer().hasGBuffer() && app().renderer().debugState().ssaoDebug == 1;
    const bool ssrTile  = app().renderer().hasGBuffer() && app().renderer().debugState().ssrDebug != 0;
    if (!movieBlocks)
    {
        if (cmd && m_debugOverlay.isValid()
            && (app().m_showShadowMaps || app().renderer().debugState().shadowMapTiles || app().m_showDepth || app().m_showGBuffer || app().m_showVelocity || ssaoTile || ssrTile))
            markPass(PassId::Overlay);
        drawDebugOverlays(cmd);
    }
    app().m_movie.draw(app().renderer());
    if (app().m_hud.m_imgui.isReady() && app().m_hud.m_menu.visible())
    {
        Dark::drawMainMenuImGui(app().m_hud.m_menu, app().m_hud.m_imgui.fonts(), app().m_sceneStarted);
        app().m_hud.m_imgui.render(app().renderer());
    }
    else if (app().m_hud.m_imgui.isReady() && !app().m_hud.m_menu.visible())
    {
        app().m_hud.drawNpcInfoOverlay();
        app().m_hud.drawGuardAwareness();
        app().m_hud.drawGuardSpeech();
        app().m_hud.drawDialogue();
        app().m_hud.drawArmorHud();
        app().m_hud.drawStaminaBar();
        app().m_hud.drawQuestHud();
        app().m_hud.drawUpgradeHint();
        app().m_hud.drawAlarm();
        app().m_hud.drawBreathBar();
        app().m_hud.drawPickupLabels();
        app().m_hud.drawTargetMarker();
        app().m_hud.drawDefenseText();
        if (app().m_showProfiler)
            app().m_hud.drawProfiler();
        if (app().m_gameplayPaused)
            app().m_hud.drawPauseOverlay();
        if (app().m_showDevTools)
            app().m_hud.drawDevTools();
        if (app().m_hud.m_showJournal)
            app().m_hud.drawJournal();
        if (app().m_hud.m_showUpgrades)
            app().m_hud.drawUpgrades();
        if (app().m_showInventory)
            app().m_hud.drawInventory();
        if (app().m_hud.m_lootTarget.valid())
            app().m_hud.drawLoot();
        app().m_hud.m_imgui.render(app().renderer());
    }
    else
        app().m_hud.m_menu.draw(app().renderer());
    uint32_t tableCount = 0;
    const PassNode* table = deferred ? sandboxDeferredPasses(tableCount) : sandboxForwardPasses(tableCount);
    (void)passSequenceOk(table, tableCount, playedPasses, playedCount);
    frameJoinRender(app().jobs(), app().renderer().frameIndex());
    app().renderer().endFrame();
}

void SandboxFrame::drawDebugOverlays(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd || !m_debugOverlay.isValid())
        return;
    const bool ssaoTile = app().renderer().hasGBuffer() && app().renderer().debugState().ssaoDebug == 1;
    const bool ssrTile  = app().renderer().hasGBuffer() && app().renderer().debugState().ssrDebug != 0;
    if (!app().m_showShadowMaps && !app().renderer().debugState().shadowMapTiles && !app().m_showDepth && !app().m_showGBuffer && !app().m_showVelocity && !ssaoTile && !ssrTile)
        return;

    const GpuScope overlay(cmd, "Debug Overlay", ProfileColor::DebugOverlay);
    // Unbind the DSV so we can sample the scene depth. Do not rebind it afterwards
    // while it remains PIXEL_SHADER_RESOURCE (endFrame does not write depth).
    app().renderer().bindColorTargetOnly();
    m_debugOverlay.beginFrame(app().renderer().frameIndex());

    const LONG sw = static_cast<LONG>(app().renderer().width());
    const LONG sh = static_cast<LONG>(app().renderer().height());
    const LONG pad = 12;
    LONG tile = sh / 5;
    if (tile < 96)
        tile = 96;
    if (tile > 220)
        tile = 220;

    if (app().m_showGBuffer && app().renderer().hasGBuffer())
    {
        app().renderer().transitionAlbedo(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        app().renderer().transitionAttrib(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        const LONG y = pad;
        const D3D12_CPU_DESCRIPTOR_HANDLE albedo = app().renderer().albedoSrvCpu();
        const D3D12_CPU_DESCRIPTOR_HANDLE attrib = app().renderer().attribSrvCpu();
        if (albedo.ptr != 0)
            m_debugOverlay.drawColor(cmd, app().renderer().device(), albedo, pad, y, tile, tile);
        if (attrib.ptr != 0)
            m_debugOverlay.drawColor(cmd, app().renderer().device(), attrib, pad + tile + 8, y, tile, tile);
    }
    if ((app().m_showGBuffer || app().m_showVelocity) && app().renderer().hasGBuffer())
    {
        app().renderer().transitionVelocity(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        const D3D12_CPU_DESCRIPTOR_HANDLE velocity = app().renderer().velocitySrvCpu();
        if (velocity.ptr != 0)
        {
            LONG x = pad;
            if (app().m_showGBuffer)
                x = pad + 2 * (tile + 8);
            m_debugOverlay.drawVelocity(cmd, app().renderer().device(), velocity, x, pad, tile, tile, 24.0f);
        }
    }
    if (ssaoTile)
    {
        m_scene.gtao().transitionAoFull(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        const D3D12_CPU_DESCRIPTOR_HANDLE aoFull = m_scene.gtao().aoFullSrvCpu();
        if (aoFull.ptr != 0)
        {
            LONG x = pad;
            if (app().m_showGBuffer)
                x += 2 * (tile + 8);
            if (app().m_showGBuffer || app().m_showVelocity)
                x += tile + 8;
            m_debugOverlay.draw2D(cmd, app().renderer().device(), aoFull, x, pad, tile, tile, 1.0f, false);
        }
    }
    if (ssrTile)
    {
        LONG x = pad;
        if (app().m_showGBuffer)
            x += 2 * (tile + 8);
        if (app().m_showGBuffer || app().m_showVelocity)
            x += tile + 8;
        if (ssaoTile)
            x += tile + 8;
        if (app().renderer().debugState().ssrDebug == 1)
        {
            m_scene.ssr().transitionFull(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            const D3D12_CPU_DESCRIPTOR_HANDLE full = m_scene.ssr().fullSrvCpu();
            if (full.ptr != 0)
                m_debugOverlay.drawColor(cmd, app().renderer().device(), full, x, pad, tile, tile);
        }
        else
        {
            m_scene.ssr().transitionDebug(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            const D3D12_CPU_DESCRIPTOR_HANDLE conf = m_scene.ssr().debugConfSrvCpu();
            if (conf.ptr != 0)
                m_debugOverlay.draw2D(cmd, app().renderer().device(), conf, x, pad, tile, tile, 1.0f, false);
        }
    }

    if (app().m_showDepth && app().renderer().depthResource())
    {
        app().renderer().transitionDepth(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        const LONG x = pad;
        const LONG y = sh - pad - tile;
        m_debugOverlay.draw2D(
            cmd, app().renderer().device(), app().renderer().depthSrvCpu(), x, y, tile, tile, 1.0f, true);
    }

    if (app().m_showShadowMaps && m_shadows.isValid())
    {
        const int n = m_shadows.cascadeCount();
        LONG x0 = pad;
        if (app().m_showDepth)
            x0 += tile + pad;
        const LONG y = sh - pad - tile;
        const LONG gap = 8;
        LONG tw = tile;
        const LONG need = n * tw + (n - 1) * gap;
        if (x0 + need > sw - pad && n > 0)
        {
            const LONG avail = sw - pad - x0 - (n - 1) * gap;
            if (avail > 64)
                tw = avail / n;
        }
        const float slice0 = static_cast<float>(m_shadows.debugSliceOffset());
        for (int i = 0; i < n; ++i)
        {
            const LONG x = x0 + i * (tw + gap);
            m_debugOverlay.drawArray(
                cmd,
                app().renderer().device(),
                m_shadows.srvCpu(),
                x,
                y,
                tw,
                tile,
                slice0 + static_cast<float>(i),
                1.25f,
                true);
        }
    }

    if (app().renderer().debugState().shadowMapTiles && m_scene.localShadows().isValid())
    {
        LocalShadowSystem& local = m_scene.localShadows();
        const int n = local.slicesPerFrame();
        const LONG gap = 8;
        const LONG y = sh - pad - tile - gap - tile;
        LONG x0 = pad;
        LONG tw = tile;
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
                app().renderer().device(),
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

    cmd->RSSetViewports(1, &app().renderer().viewport());
    cmd->RSSetScissorRects(1, &app().renderer().scissor());
}
