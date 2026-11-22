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
#include "Render/Fog.h"
#include "Render/Underwater.h"
#include "Render/PbrLighting.h"
#include "Render/ModelDraw.h"
#include "Render/FrameComposer.h"
#include "Render/FrameView.h"
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
#include "AI/WalkabilityMesh.h"
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
#include "Play/CombatHud.h"
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
        Dark::setGameNavEnabled(app().m_hud.m_menu.visible() && !app().m_hud.m_menu.loading());
        if (app().m_hud.m_imgui.isReady())
            app().m_hud.m_imgui.beginFrame();
        if (app().m_hud.m_imgui.isReady())
            app().m_hud.drawMainMenu();
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

    app().m_frame.foliagePipeline().setWind(app().m_sceneWorld.wind, app().m_sceneWorld.windClock.time, app().m_sceneWorld.windClock.prevTime);

    Dark::setGameNavEnabled(app().m_hud.m_menu.visible() || app().m_play.simPaused());
    if (app().m_hud.m_imgui.isReady())
        app().m_hud.m_imgui.beginFrame();

    GpuResourceCache& gpu = app().renderer().gpuResources();
    if (m_skinRing.isValid())
        m_skinRing.beginFrame(app().renderer().frameIndex());
    if (m_skinnedInstances.isValid())
        m_skinnedInstances.beginFrame(app().renderer().frameIndex());

    const Frustum3f cameraCull(app().m_sceneWorld.camera.GetCullViewProj());
    const bool selectLocalShadows = app().renderer().debugState().localLights
        && app().renderer().debugState().lightingActive();
    m_scene.localShadows().update(app().world(), app().m_sceneWorld.camera, cameraCull, app().renderer().frameIndex(), selectLocalShadows);

    AABox3f sceneBounds = app().m_sceneWorld.terrain.shadowBounds(app().m_sceneWorld.camera);
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
        app().m_sceneWorld.camera,
        app().m_sceneWorld.env.lightDir(),
        sceneBounds,
        app().m_sceneWorld.env.sunElevation(),
        app().m_sceneWorld.env.weather.cloudCoverage,
        app().renderer().frameIndex());

    const bool instanceStatic = m_drawList.gpuReady() && app().renderer().debugState().instancedStatic && app().renderer().debugState().fill == DebugFill::Solid;
    const bool instanceSkinned = instanceStatic && m_skinnedInstances.isValid();
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
            const Mesh* gpuMesh = sandboxPrimitiveMesh(mc.primitive, app().m_demo.cubeMesh(), app().m_demo.crossMesh());
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
        {
            const Matrix4f camVp   = m_scene.beginCameraFrame(app().m_sceneWorld.camera, app().renderer());
            const Matrix4f camPrev = m_scene.havePrevViewProj() ? m_scene.prevViewProj() : camVp;
            float drawVp[16];
            float drawPrev[16];
            copyMatrix(drawVp, camVp);
            copyMatrix(drawPrev, camPrev);
            m_drawList.buildView(Frustum3f(app().m_sceneWorld.camera.GetCullViewProj()), drawVp, drawPrev, RenderPassKind::GBuffer, ViewId{ 0 });
        }
    }

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
    shadowHost.terrain           = &app().m_sceneWorld.terrain;
    shadowHost.foliage           = &app().m_frame.foliagePipeline();
    shadowHost.foliagePrototypes = &app().m_frame.foliagePrototypes();
    shadowHost.foliageRecords    = app().m_sceneWorld.foliage.empty() ? nullptr : &app().m_sceneWorld.foliage;
    shadowHost.foliageDensity    = &app().m_sceneWorld.foliageDensity;
    shadowHost.camera            = &app().m_sceneWorld.camera;
    shadowHost.drawTerrain       = app().m_sceneWorld.showTerrain;
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

    // The frame graph drives the frame (render-graph plan Phase 4): every step is an opaque pass on
    // the main list, set here in recording order and run by frame.execute() below in feature-list
    // order, which is also the order check. A pass body runs after this setup code, so it captures
    // only function-scope state; values that cross passes are declared here and filled in order.
    uint32_t           featureCount = 0;
    const FeaturePass* featureList  = sandboxFeatures(featureCount);
    FrameComposer&     frame        = m_frameComposer;
    FrameView          view;
    // Echolocation strobes: no TAA / GTAO / SSR history from (or into) a held frame, no motion blur.
    view.holdTemporal = app().m_vision.echo > 0.0f;
    frame.begin(app().renderer(), view, featureList, featureCount, m_shadows.resource(), m_scene.localShadows().resource());
    m_scene.requestHistories(frame, app().renderer());

    frame.set(PassId::Shadows, [&](Rg::PassContext& pass) {
        ID3D12GraphicsCommandList* const cmd = pass.cmd();
        ensureDirectionalShadowModels(shadowHost);
        recordDirectionalShadows(shadowHost, cmd, shadowViewIndex);
    });

    LocalShadowSystem& localShadows = m_scene.localShadows();
    if (localShadows.faceCountThisFrame() > 0 || localShadows.isValid())
    {
        frame.set(PassId::LocalShadows, [&](Rg::PassContext& pass) {
            ID3D12GraphicsCommandList* const cmd = pass.cmd();
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
        });
    }

    const bool visionPicture = app().m_vision.infrared > 0.0f || app().m_vision.ultraviolet > 0.0f || app().m_vision.echo > 0.0f;
    const float underDraw = !visionPicture ? Math::Clamp(app().m_play.runtime().underAmount, 0.0f, 1.0f) : 0.0f;
    const bool instanceColor = instanceStatic;
    Matrix4f             viewProj;
    Matrix4f             prevViewProj;
    Frustum3f            frustum;
    DebugFill            fill        = DebugFill::Solid;
    Vector3f             camPos;
    uint32_t             meshDraws   = 0;
    float                ultraviolet = 0.0f;
    float                thermal     = 0.0f;
    LightingConstants    lc{};
    DeferredResolveInput resolve;
    const auto drawGBuffer = [&](Rg::PassContext& pass) {
        ID3D12GraphicsCommandList* const cmd = pass.cmd();
        const Matrix4f viewProjEarly = m_scene.beginCameraFrame(app().m_sceneWorld.camera, app().renderer());
        (void)viewProjEarly;
        {
            const bool uvDraw = app().m_vision.ultraviolet > 0.0f;
            const bool thermalDraw = !uvDraw && app().m_vision.infrared > 0.0f;
            m_meshPipeline.setUltraviolet(uvDraw);
            m_skinnedPipeline.setUltraviolet(uvDraw);
            m_terrainPipeline.setUltraviolet(uvDraw);
            app().m_frame.foliagePipeline().setUltraviolet(uvDraw);
            app().m_frame.grassPipeline().setUltraviolet(uvDraw);
            m_drawList.setUltraviolet(uvDraw);
            m_skinnedInstances.setUltraviolet(uvDraw);
            m_meshPipeline.setThermal(thermalDraw);
            m_skinnedPipeline.setThermal(thermalDraw);
            m_terrainPipeline.setThermal(thermalDraw);
            app().m_frame.foliagePipeline().setThermal(thermalDraw);
            app().m_frame.grassPipeline().setThermal(thermalDraw);
            m_drawList.setThermal(thermalDraw);
            m_skinnedInstances.setThermal(thermalDraw);
        }

        viewProj     = app().m_sceneWorld.camera.GetViewProj();
        prevViewProj = m_scene.havePrevViewProj() ? m_scene.prevViewProj() : viewProj;
        frustum      = Frustum3f(app().m_sceneWorld.camera.GetCullViewProj());
        fill         = app().renderer().debugState().fill;
        camPos       = app().m_sceneWorld.camera.GetPosition();

        const float skyExposure = 1.0f; // tonemap owns exposure
        const float fogScale    = (app().renderer().debugState().lightingActive() ? 1.0f : 0.0f) * (1.0f - underDraw);
        ultraviolet = app().m_vision.ultraviolet > 0.0f ? app().m_vision.ultraviolet : 0.0f;
        thermal     = !(app().m_vision.ultraviolet > 0.0f) ? app().m_vision.infrared : 0.0f;
        m_skyPipeline.upload(app().renderer().frameIndex(), app().m_sceneWorld.camera, app().m_sceneWorld.env, skyExposure, app().m_sceneWorld.water.params().waterLevel, fogScale, thermal, ultraviolet);
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

        // Opaque G-buffer, then the deferred resolve and sky as their own passes.
        {
            {
                const GpuScope gbuffer(cmd, "GBuffer", ProfileColor::GBuffer);
                if (app().m_sceneWorld.showTerrain)
                    app().m_sceneWorld.terrain.drawGBuffer(cmd, m_terrainPipeline, app().m_frame.terrainMaterial(), app().m_sceneWorld.camera, &frustum, &app().renderer().debugState(), &prevViewProj);
                if (app().m_frame.grassPipeline().isValid() && app().m_sceneWorld.grassField.params().enabled)
                {
                    const GpuScope      grassScope(cmd, "Grass", ProfileColor::Grass);
                    GrassFrameConstants grassFrame{};
                    copyMatrix(grassFrame.viewProj, viewProj);
                    copyMatrix(grassFrame.prevViewProj, prevViewProj);
                    grassFrame.heightMetres = app().m_sceneWorld.grassField.params().heightMetres;
                    grassFrame.flexibility  = app().m_sceneWorld.grassField.params().flexibility;
                    grassFrame.faceEdge     = app().m_sceneWorld.grassField.params().faceEdge;
                    grassFrame.faceYaw      = app().m_sceneWorld.grassField.params().faceYaw;
                    grassFrame.faceLean     = app().m_sceneWorld.grassField.params().faceLean;
                    grassFrame.faceSteep    = app().m_sceneWorld.grassField.params().faceSteep;
                    grassFrame.cameraPos[0]   = camPos.x;
                    grassFrame.cameraPos[1]   = camPos.y;
                    grassFrame.cameraPos[2]   = camPos.z;
                    grassFrame.cameraRight[0] = app().m_sceneWorld.camera.GetRight().x;
                    grassFrame.cameraRight[1] = app().m_sceneWorld.camera.GetRight().y;
                    grassFrame.cameraRight[2] = app().m_sceneWorld.camera.GetRight().z;
                    grassFrame.cameraLook[0]  = app().m_sceneWorld.camera.GetLook().x;
                    grassFrame.cameraLook[1]  = app().m_sceneWorld.camera.GetLook().y;
                    grassFrame.cameraLook[2]  = app().m_sceneWorld.camera.GetLook().z;
                    app().m_sceneWorld.grassField.writeFrameInteraction(grassFrame);
                    GrassLodSpan grassLods[kGrassLodCount]{};
                    for (int lod = 0; lod < kGrassLodCount; ++lod)
                    {
                        const GrassField::BladeSpan span = app().m_sceneWorld.grassField.blades(lod);
                        grassLods[lod].blades            = span.data;
                        grassLods[lod].count             = span.count;
                        grassLods[lod].staleMask         = app().m_sceneWorld.grassField.lodStaleMask(lod);
                    }
                    const uint32_t residentTiles = grassRegionBudget().residentTiles;
                    std::vector<GrassTileDraw> grassTiles(residentTiles);
                    const uint32_t grassDraws = app().m_sceneWorld.grassField.visibleTiles(frustum, grassTiles.data(), residentTiles);
                    // This slot has seen the CPU blades. Leave the other slot stale until its fence.
                    if (app().m_frame.grassPipeline().draw(cmd, app().renderer(), grassLods, app().m_sceneWorld.grassField.tileWind(), app().m_sceneWorld.grassField.tileWindCount(), grassFrame, grassTiles.data(), grassDraws))
                    {
                        const uint32_t slot = app().renderer().frameIndex() % kGrassFrameSlots;
                        for (int lod = 0; lod < kGrassLodCount; ++lod)
                            app().m_sceneWorld.grassField.clearLodStale(lod, slot);
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
                const Mesh* gpuMesh = sandboxPrimitiveMesh(mc.primitive, app().m_demo.cubeMesh(), app().m_demo.crossMesh());
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
                if (app().m_sceneWorld.terrain.valid())
                    app().m_frame.foliagePipeline().drawGBuffer(cmd, app().renderer(), app().assets(), app().m_frame.foliagePrototypes(), app().m_sceneWorld.terrain, app().m_sceneWorld.foliage.empty() ? nullptr : &app().m_sceneWorld.foliage, app().m_sceneWorld.foliageDensity, app().m_sceneWorld.camera, viewProj, prevViewProj, frustum);
            }

            m_meshPipeline.setUltraviolet(false);
            m_skinnedPipeline.setUltraviolet(false);
            m_terrainPipeline.setUltraviolet(false);
            app().m_frame.foliagePipeline().setUltraviolet(false);
            app().m_frame.grassPipeline().setUltraviolet(false);
            m_drawList.setUltraviolet(false);
            m_skinnedInstances.setUltraviolet(false);
            m_meshPipeline.setThermal(false);
            m_skinnedPipeline.setThermal(false);
            m_terrainPipeline.setThermal(false);
            app().m_frame.foliagePipeline().setThermal(false);
            app().m_frame.grassPipeline().setThermal(false);
            m_drawList.setThermal(false);
            m_skinnedInstances.setThermal(false);

            copyMatrix(lc.invViewProj, viewProj.Inverse());
            lc.cameraPos[0]     = camPos.x;
            lc.cameraPos[1]     = camPos.y;
            lc.cameraPos[2]     = camPos.z;
            lc.lightDirWS[0]    = app().m_sceneWorld.env.lightDir().x;
            lc.lightDirWS[1]    = app().m_sceneWorld.env.lightDir().y;
            lc.lightDirWS[2]    = app().m_sceneWorld.env.lightDir().z;
            lc.lighting         = app().renderer().debugState().lightingActive() ? 1.0f : 0.0f;
            lc.lightColor[0]    = app().m_sceneWorld.env.lightColor().x;
            lc.lightColor[1]    = app().m_sceneWorld.env.lightColor().y;
            lc.lightColor[2]    = app().m_sceneWorld.env.lightColor().z;
            {
                const Vector3f pbrSun = pbrSunLightColor(app().m_sceneWorld.env.lightColor());
                lc.pbrLightColor[0]   = pbrSun.x;
                lc.pbrLightColor[1]   = pbrSun.y;
                lc.pbrLightColor[2]   = pbrSun.z;
            }
            lc.emissiveGain     = 4.0f;
            lc.ambientColor[0]  = app().m_sceneWorld.env.ambientColor().x;
            lc.ambientColor[1]  = app().m_sceneWorld.env.ambientColor().y;
            lc.ambientColor[2]  = app().m_sceneWorld.env.ambientColor().z;
            FogGpu fog = makeFogGpu(&app().m_sceneWorld.env, app().m_sceneWorld.water.params().waterLevel, lc.lighting > 0.5f);
            const float air = 1.0f - underDraw;
            fog.fogDensity *= air;
            fog.heightFogDensity *= air;
            fog.volumetricFogDensity *= air;
            fillFogHeightMap(fog, &app().m_sceneWorld.terrain.coarse());
            applyFogToLighting(lc, fog);
            fillIblLightingConstants(lc, app().m_sceneWorld.ibl, app().renderer().debugState().iblEnabled, app().renderer().debugState().iblDebug, iblGpuReady(app().renderer().gpuResources(), app().m_sceneWorld.iblImageId));
            lc.iblNight = 1.0f - app().m_sceneWorld.env.dayFactor();
            lc.thermal  = app().m_vision.ultraviolet > 0.0f ? -app().m_vision.ultraviolet : app().m_vision.infrared;
            fillSsrLightingConstants(lc, app().m_sceneWorld.ssr, app().renderer().debugState().ssrEnabled, m_scene.ssr().isValid());
            resolve.viewProj     = &viewProj;
            resolve.prevViewProj = &prevViewProj;
            resolve.gtao         = &app().m_sceneWorld.ssao;
            resolve.ssr          = &app().m_sceneWorld.ssr;
            resolve.ssrEnv       = &app().m_sceneWorld.env;
            resolve.lighting     = &lc;
            {
                const FacilityState& facility = app().m_play.facility();
                m_scene.setFogOccluders(facility.blocks, facility.blockCount);
            }
        }
    };
    frame.declare(PassId::GBuffer, [&](FeatureBuilder& b) { m_scene.addGBufferPasses(b, app().renderer(), drawGBuffer); });

    // Decals, Gtao, Ssr, Lighting and LocalVolumes declare their uses. Settings are captured at build;
    // the camera and `resolve` (lc, filled by the GBuffer draws) are read at execute.
    frame.declare(PassId::Decals, [&](FeatureBuilder& b) { m_scene.addDecalsPasses(b, app().renderer(), app().m_sceneWorld.camera, resolve); });
    frame.declare(PassId::Gtao, [&](FeatureBuilder& b) { m_scene.addGtaoPasses(b, app().renderer(), app().m_sceneWorld.camera, app().m_sceneWorld.ssao); });
    frame.declare(PassId::Ssr, [&](FeatureBuilder& b) { m_scene.addSsrPasses(b, app().renderer(), app().m_sceneWorld.camera, app().m_sceneWorld.ssr, &app().m_sceneWorld.env); });
    frame.declare(PassId::Lighting, [&](FeatureBuilder& b) { m_scene.addLightingPasses(b, app().renderer(), resolve); });
    frame.declare(PassId::LocalVolumes, [&](FeatureBuilder& b) { m_scene.addLocalVolumesPasses(b, app().renderer(), app().world(), app().m_sceneWorld.camera, resolve); });
    frame.declare(PassId::Sky,
                  [&](FeatureBuilder& b)
                  {
                      m_scene.addSkyPasses(b, app().renderer(), [&](Rg::PassContext& pass, D3D12_GPU_DESCRIPTOR_HANDLE shadowTable)
                                           { m_skyPipeline.draw(pass.cmd(), app().renderer().frameIndex(), shadowTable, &m_shadows); });
                  });

    D3D12_GPU_VIRTUAL_ADDRESS waterLightsVa   = m_localLightGpu.isValid() ? m_localLightGpu.dummyGpuVa() : 0;
    uint32_t                  waterLightCount = 0;
    uint32_t                  waterIndex[kWaterLocalLightMax]{};
    const SsrSettings*        waterSsr = &app().m_sceneWorld.ssr;
    frame.declare(PassId::SsrCapture,
                  [&](FeatureBuilder& b)
                  {
                      // CPU only: water's local lights. Runs at execute (viewProj is filled by GBuffer).
                      b.opaque("SsrCapture.WaterInputs", {}, {},
                               [&](Rg::PassContext&)
                               {
                                   if (app().renderer().debugState().localLights && app().renderer().debugState().lightingActive() && m_localLightGpu.isValid())
                                   {
                                       LocalLightCullInput in{};
                                       in.frustum      = &frustum;
                                       in.cameraPos    = camPos;
                                       in.cameraLook   = app().m_sceneWorld.camera.GetLook();
                                       in.nearZ        = app().m_sceneWorld.camera.GetNearZ();
                                       in.viewportW    = app().renderer().width();
                                       in.viewportH    = app().renderer().height();
                                       in.viewProj     = &viewProj;
                                       in.localShadows = &m_scene.localShadows();
                                       in.fogBoxes     = m_scene.fogOccluders();
                                       in.fogBoxCount  = m_scene.fogOccluderCount();
                                       LocalLightDrawLists lists{};
                                       if (gatherLocalLights(app().world(), in, lists) && lists.count > 0)
                                       {
                                           m_localLightGpu.upload(app().renderer().frameIndex(), lists);
                                           waterLightCount = lists.waterCount;
                                           std::memcpy(waterIndex, lists.waterIndex, sizeof(waterIndex));
                                           waterLightsVa = m_localLightGpu.lightsGpuVa();
                                       }
                                   }
                               });
                      m_scene.addSsrCapturePasses(b, app().renderer());
                  });
    const bool waterSheet = app().m_sceneWorld.water.wetChunkCount() > 0;
    if (waterSheet || !app().m_sceneWorld.water.ribbons().empty())
    {
        const auto drawWater = [&](Rg::PassContext& pass, const WaterTables& tables)
        {
            ID3D12GraphicsCommandList* const cmd = pass.cmd();
            const GpuScope waterScope(cmd, "Water", ProfileColor::Water);
            uint32_t drawIndex = 0;
            if (waterSheet)
            {
                app().m_sceneWorld.water.draw(cmd, m_waterPipeline, app().m_sceneWorld.camera, &frustum, &app().m_sceneWorld.env, &app().renderer().debugState(), waterLightsVa, waterLightCount,
                                              waterIndex, app().renderer().frameIndex(), &tables, &m_shadows, waterSsr, drawIndex, thermal, ultraviolet, underDraw);
                drawIndex = 1;
            }
            for (const WaterWorld::WaterRibbon& stream : app().m_sceneWorld.water.ribbons())
            {
                if (drawIndex >= WaterPipeline::kMaxWaterDrawsPerFrame)
                {
                    static bool s_warned = false;
                    if (!s_warned)
                    {
                        s_warned = true;
                        DE_LOG_WARN(LogCategory::Render, "SandboxApp: water draw cap {} reached; the rest of the streams in view are not drawn (warns once)", WaterPipeline::kMaxWaterDrawsPerFrame);
                    }
                    break;
                }
                WaterParams streamParams = app().m_sceneWorld.water.params();
                streamParams.flowSpeed = stream.flowSpeed;
                if (drawStreamRibbon(cmd, m_waterPipeline, stream.mesh, stream.bounds, streamParams, app().m_sceneWorld.camera, &frustum, &app().renderer().debugState(),
                                     app().m_sceneWorld.water.time(), app().renderer().frameIndex(), drawIndex, &tables, &m_shadows, waterSsr, &app().m_sceneWorld.terrain.coarse(),
                                     &app().m_sceneWorld.env, thermal, ultraviolet))
                    ++drawIndex;
            }
        };
        frame.declare(PassId::Water, [&, drawWater](FeatureBuilder& b) { m_scene.addWaterPasses(b, app().renderer(), drawWater); });
    }

    frame.declare(PassId::Clouds,
                  [&](FeatureBuilder& b)
                  {
                      m_scene.addCloudsPasses(b, app().renderer(),
                                              [&](Rg::PassContext& pass, D3D12_GPU_DESCRIPTOR_HANDLE depthTable)
                                              {
                                                  CloudVolumeFrame cf{};
                                                  cf.sunDir       = app().m_sceneWorld.env.lightDir();
                                                  cf.sunColor     = app().m_sceneWorld.env.lightColor();
                                                  cf.ambientColor = app().m_sceneWorld.env.ambientColor();
                                                  cf.time         = app().m_sceneWorld.cloudTime;
                                                  cf.lod          = app().m_frame.cloudLod();
                                                  m_scene.drawCloudVolumes(pass.cmd(), depthTable, app().renderer(), app().world(), app().m_sceneWorld.camera, viewProj, cf);
                                              });
                      // The passes after Clouds are opaque and expect HDR bound with read-only depth.
                      b.opaque("Clouds.BindHdr", {}, {}, [&](Rg::PassContext&) { app().renderer().bindHdrDepthRead(); });
                  });

    bool lateDepthWrite = false;
    if (app().m_vision.camouflage && m_scene.camouflage().isValid())
    {
        frame.set(PassId::Camouflage, [&](Rg::PassContext& pass) {
            ID3D12GraphicsCommandList* const cmd = pass.cmd();
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
                lateDepthWrite = m_scene.camouflage().drawModel(cmd, app().renderer(), gpu, m_skinRing, *model, pose, worldMat, viewProj, camPos, app().m_sceneWorld.cloudTime, fill);
            }
        });
    }

    frame.set(PassId::Translucent, [&](Rg::PassContext& pass) {
        ID3D12GraphicsCommandList* const cmd = pass.cmd();
        const GpuScope translucent(cmd, "Translucent", ProfileColor::Translucent);
        MeshFrameConstants lit{};
        lit.lightDirWS[0] = app().m_sceneWorld.env.lightDir().x;
        lit.lightDirWS[1] = app().m_sceneWorld.env.lightDir().y;
        lit.lightDirWS[2] = app().m_sceneWorld.env.lightDir().z;
        lit.ambientScale  = 0.22f;
        {
            const Vector3f pbrSun = pbrSunLightColor(app().m_sceneWorld.env.lightColor());
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
        sortTranslucent(translucentOrder.data(), static_cast<uint32_t>(translucentOrder.size()), app().m_sceneWorld.camera.GetView());
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
    });

    frame.set(PassId::DebugOverlays, [&](Rg::PassContext& pass) {
        ID3D12GraphicsCommandList* const cmd = pass.cmd();
        if (app().m_chaseOk && (app().m_demo.demoScene() || app().m_showDevTools))
        {
            const GpuScope paths(cmd, "Path Debug", ProfileColor::DebugOverlay);
            app().m_chase.drawPaths(cmd, app().renderer(), viewProj);
        }
        m_walkDebug.draw(cmd, app().renderer(), AI::walkabilityMeshSource(&app().m_chase.ai().walkability()), viewProj);
        app().m_demo.drawSkeletonOverlay(cmd, viewProj);
        m_scene.drawDecalVolumes(cmd, app().renderer(), app().m_chase.linePipeline(), viewProj);
    });

    frame.set(PassId::Particles, [&](Rg::PassContext& pass) {
        ID3D12GraphicsCommandList* const cmd = pass.cmd();
        const GpuScope particles(cmd, "Particles", ProfileColor::Particles);
        app().m_frame.gpuParticles().simulateAndDraw(cmd, app().renderer(), app().m_sceneWorld.camera, app().world(), app().assets(), lateDepthWrite);
        app().m_frame.particles().beginFrame(app().renderer().frameIndex());
    if (app().m_play.feedback().blood.aliveCount() > 0)
        app().m_frame.particles().draw(cmd, app().m_sceneWorld.camera, app().m_play.feedback().blood, false);
    app().world().each<ParticleEmitterComponent>([&](Entity, ParticleEmitterComponent& pe) {
        if (!pe.runtime)
            return;
        const Material* sprite = nullptr;
        if (const Material* mat = app().assets().find<Material>(pe.matAssetID))
        {
            gpu.retainMaterial(app().assets(), *mat);
            sprite = mat;
        }
        app().m_frame.particles().draw(cmd, app().m_sceneWorld.camera, *pe.runtime, pe.runtime->desc().additiveBlend, sprite);
    });
    if (WeaponLoadout* wFx = app().localWeapons(); wFx && wFx->projectile().impactEmitter().aliveCount() > 0)
        app().m_frame.particles().draw(cmd, app().m_sceneWorld.camera, wFx->projectile().impactEmitter(), true);
    app().m_frame.leafRenderer().draw(cmd, app().m_sceneWorld.camera, app().m_sceneWorld.leafField);

        if (!app().deferredDecals())
            app().m_play.feedback().splats.draw(cmd, app().m_sceneWorld.camera);
    });

    if (underDraw > 0.0f && m_scene.underwater().isValid())
    {
        const auto underFrame = [&](Rg::PassContext&)
        {
            UnderwaterFrame under{};
            copyMatrix(under.invViewProj, viewProj.Inverse());
            under.cameraPos[0] = camPos.x;
            under.cameraPos[1] = camPos.y;
            under.cameraPos[2] = camPos.z;
            under.waterLevel   = app().m_sceneWorld.water.params().waterLevel;
            under.amount       = underDraw;
            return under;
        };
        // The water surface seen from below.
        const auto drawCeiling = [&](Rg::PassContext& pass, const WaterTables& tables)
        {
            app().m_sceneWorld.water.draw(pass.cmd(), m_waterPipeline, app().m_sceneWorld.camera, &frustum, &app().m_sceneWorld.env, &app().renderer().debugState(), waterLightsVa, waterLightCount,
                                          waterIndex, app().renderer().frameIndex(), &tables, &m_shadows, waterSsr, 0, thermal, ultraviolet, underDraw, true);
        };
        frame.declare(PassId::Underwater, [&, underFrame, drawCeiling](FeatureBuilder& b) { m_scene.addUnderwaterPasses(b, app().renderer(), underFrame, drawCeiling); });
    }

    // Phase 6 part 2: Post adds declared passes (SceneRenderer::addPostPasses) when execute() builds
    // the frame; the echo image stays one opaque pass ahead of them (game content, Phase 8).
    frame.declare(PassId::Post, [&](FeatureBuilder& b) {
        const bool aces = useAcesTonemap(app().renderer());
        TonemapSettings post = app().playerPostFx();
        post.mode            = aces ? 1.0f : 0.0f;
        float exposure       = app().m_sceneWorld.env.exposure();
        app().m_frame.autoExposureResult() = {};
        app().m_frame.autoExposureResult().finalExposure = exposure;
        const bool deferredMeter = m_scene.autoExposure().isValid();
        if (deferredMeter)
        {
            // Resize drops the 1×1. Zero the correction before this frame can apply a stale sample.
            if (m_scene.syncAutoExposureSize(app().renderer()))
                resetAutoExposure(app().m_frame.autoExposureState());
            // Infrared, ultraviolet, echolocation, and the underwater column swing the meter. Hold the correction while any is on.
            const bool holdExposure = app().m_vision.infrared > 0.0f || app().m_vision.ultraviolet > 0.0f || app().m_vision.echo > 0.0f || app().m_play.runtime().underAmount > 0.0f;
            const float measured = (app().m_frame.autoExposure().mode == ExposureMode::Auto && !holdExposure)
                ? m_scene.autoExposure().readMeasuredLuma(app().renderer().frameIndex())
                : 0.0f;
            app().m_frame.autoExposureResult() = adaptExposure(app().m_frame.autoExposureState(), measured, exposure, app().m_frameDt, app().m_frame.autoExposure());
            exposure             = app().m_frame.autoExposureResult().finalExposure;
        }
        post.exposure = exposure;
        const bool echoImage = app().m_vision.echo > 0.0f && m_scene.echo().isValid();
        if (echoImage)
            b.opaque("Post.Echo", { RenderResource::Hdr, RenderResource::Depth }, { RenderResource::Hdr }, [&](Rg::PassContext& pass) {
                ID3D12GraphicsCommandList* const cmd = pass.cmd();
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
            });
        const bool holdExposure = app().m_vision.infrared > 0.0f || app().m_vision.ultraviolet > 0.0f || app().m_vision.echo > 0.0f || app().m_play.runtime().underAmount > 0.0f;
        m_scene.addPostPasses(b, app().renderer(), viewProj, post, deferredMeter && app().m_frame.autoExposure().mode == ExposureMode::Auto && !holdExposure);
    });

    const bool movieBlocks = app().m_movie.blocksWorld();
    const bool ssaoTile = app().renderer().debugState().ssaoDebug == 1;
    const bool ssrTile  = app().renderer().debugState().ssrDebug != 0;
    if (!movieBlocks && m_debugOverlay.isValid()
        && (app().m_showShadowMaps || app().renderer().debugState().shadowMapTiles || app().m_showDepth || app().m_showGBuffer || app().m_showVelocity || ssaoTile || ssrTile))
        frame.set(PassId::Overlay, [&](Rg::PassContext& pass) { drawDebugOverlays(pass.cmd()); });
    // ImGui, the movie and the HUD stay after the graph until Phase 7; endFrame still presents.
    (void)frame.execute("Sandbox");
    // After execute: the Gtao/Ssr passes read last frame's view-proj while the graph records.
    m_scene.endCameraFrame(app().m_sceneWorld.camera, viewProj);

    Health* hudHp = app().localHealth();
    const Entity hudBody = app().possessedBody();
    const HudTagComponent* hudTag = hudBody.valid() ? app().world().get<HudTagComponent>(hudBody) : nullptr;
    const bool showHealth = !movieBlocks && !app().m_hud.m_menu.visible() && hudHp && hudTag && hudTag->kind == HudKind::HealthBar;
    const bool showCrosshair = !movieBlocks && !app().m_hud.m_menu.visible() && hudHp && hudHp->alive() && !app().m_play.simPaused() && !app().m_dialogue.locksWorld();

    app().renderer().stats().drawCalls = app().m_sceneWorld.terrain.lastDrawCalls() + app().m_sceneWorld.water.lastDrawCalls() + meshDraws + 1;
    app().renderer().stats().triangles =
        app().m_sceneWorld.terrain.lastTriangles() + app().m_sceneWorld.water.lastTriangles() + meshDraws * (app().m_demo.cubeMesh().indexCount() / 3);

    app().m_movie.draw(app().renderer());
    if (app().m_hud.m_imgui.isReady() && app().m_hud.m_menu.visible())
    {
        app().m_hud.drawMainMenu();
        app().m_hud.m_imgui.render(app().renderer());
    }
    else if (app().m_hud.m_imgui.isReady() && !app().m_hud.m_menu.visible() && app().frameCaptureActive())
    {
        // Frame capture: the baseline PNG is the 3D frame with no HUD, pause card or panels.
        app().m_hud.m_imgui.render(app().renderer());
    }
    else if (app().m_hud.m_imgui.isReady() && !app().m_hud.m_menu.visible())
    {
        app().m_hud.drawNpcInfoOverlay();
        app().m_hud.drawGuardAwareness();
        app().m_hud.drawGuardSpeech();
        app().m_hud.drawDialogue();
        app().m_hud.drawArmorHud();
        if (showHealth)
            drawHealthBar(hudHp->ratio());
        if (showCrosshair)
        {
            bool throwableSlot = false;
            if (hudBody.valid())
            {
                if (const ThrowableBeltComponent* carried = app().world().get<ThrowableBeltComponent>(hudBody))
                    throwableSlot = carried->belt.any();
            }
            const WeaponKind hudKind = app().localWeapons() ? app().localWeapons()->activeKind() : WeaponKind::Melee;
            drawCrosshair(hudKind, throwableSlot);
        }
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
        if (app().m_play.simPaused())
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
    frameJoinRender(app().jobs(), app().renderer().frameIndex());
    app().renderer().endFrame();
}

void SandboxFrame::drawDebugOverlays(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd || !m_debugOverlay.isValid())
        return;
    const bool ssaoTile = app().renderer().debugState().ssaoDebug == 1;
    const bool ssrTile  = app().renderer().debugState().ssrDebug != 0;
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

    if (app().m_showGBuffer)
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
    if ((app().m_showGBuffer || app().m_showVelocity))
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
