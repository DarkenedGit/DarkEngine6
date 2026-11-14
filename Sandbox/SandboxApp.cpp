#include "SandboxApp.h"
#include "SandboxInternal.h"

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

static float sandboxSceneGroundY(void* user, float x, float z)
{
    const TerrainGrid* terrain = static_cast<const TerrainGrid*>(user);
    return terrain ? terrain->heightAtWorld(x, z) : 0.0f;
}

// SceneRenderer owns shared pipelines; keep existing call sites stable.
#define m_meshPipeline m_frame.scene().meshPipeline()
#define m_meshTransparentPipeline m_frame.scene().meshTransparentPipeline()
#define m_skinnedPipeline m_frame.scene().skinnedPipeline()
#define m_skinnedTransparentPipeline m_frame.scene().skinnedTransparentPipeline()
#define m_skinnedShadowPipeline m_frame.scene().skinnedShadowPipeline()
#define m_skinRing m_frame.scene().skinRing()
#define m_tonemap m_frame.scene().tonemap()
#define m_lighting m_frame.scene().lighting()
#define m_localLightVolumes m_frame.scene().localLightVolumes()
#define m_localLightGpu m_frame.scene().localLightGpu()
#define m_pointVolumeMesh m_frame.scene().pointVolumeMesh()
#define m_spotVolumeMesh m_frame.scene().spotVolumeMesh()
#define m_bloom m_frame.scene().bloom()
#define m_motionBlur m_frame.scene().motionBlur()
#define m_taa m_frame.scene().taa()
#define m_shadows m_frame.scene().shadows()
#define m_debugOverlay m_frame.scene().debugOverlay()
#define m_terrainPipeline m_frame.scene().terrainPipeline()
#define m_waterPipeline m_frame.scene().waterPipeline()
#define m_skyPipeline m_frame.scene().skyPipeline()


bool useAcesTonemap(const Renderer& r)
{
    return r.hasSceneBuffers() && r.debugState().aces && r.debugState().lightingActive();
}

AssetRef<Image> loadTerrainLayerMap(AssetManager& assets, const std::string& virtualPath, Dark::Color::ColorSpace space)
{
    AssetRef<Image> img = assets.loadImage(virtualPath);
    if (!img || !img->valid())
        return {};
    img->setColorSpace(space);
    return img;
}

bool tryCreateTerrainFromContent(Renderer& renderer, AssetManager& assets, const Terrain::SplatMap& splat, TerrainMaterial& material)
{
    static constexpr const char* kLayerNames[Terrain::kMaxTerrainLayers] = { "dirt", "grass", "rock", "snow" };
    static constexpr float       kTiling[Terrain::kMaxTerrainLayers]     = { 24.0f, 20.0f, 16.0f, 12.0f };

    Terrain::TerrainSurfaceDesc desc{};
    for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
    {
        desc.layers[i].tiling      = kTiling[i];
        const std::string prefix   = std::string("terrain/") + kLayerNames[i] + "/";
        desc.albedo[i]             = loadTerrainLayerMap(assets, prefix + "albedo.png", Dark::Color::ColorSpace::sRGB);
        desc.normal[i]             = loadTerrainLayerMap(assets, prefix + "normal.png", Dark::Color::ColorSpace::Linear);
        desc.orm[i]                = loadTerrainLayerMap(assets, prefix + "orm.png", Dark::Color::ColorSpace::Linear);
        if (!desc.albedo[i] || !desc.albedo[i]->valid())
            return false;
    }

    Image     splatImg;
    Texture2D splatTex;
    if (!splatImg.createFromRGBA(splat.rgba(), splat.width(), splat.height(), splat.width() * 4u)
        || !splatTex.createFromImage(renderer, splatImg, Dark::Color::TextureUsage::Data))
    {
        DE_LOG_ERROR(LogCategory::Render, "SandboxApp: splat upload failed");
        return false;
    }
    return material.create(renderer, desc, std::move(splatTex));
}

constexpr float kNightSightBlendSeconds = 0.5f;
constexpr float kInfraredBlendSeconds   = 0.5f;
constexpr float kUltravioletBlendSeconds = 0.5f;
constexpr float kEchoBlendSeconds        = 0.5f;

void copyMatrix(float dst[16], const Math::Matrix4f& m)
{
    m.store(dst);
}

void fillForwardLighting(MeshForwardBatchConstants& dst, const MeshFrameConstants& src)
{
    dst.lightDirWS[0] = src.lightDirWS[0];
    dst.lightDirWS[1] = src.lightDirWS[1];
    dst.lightDirWS[2] = src.lightDirWS[2];
    dst.ambientScale  = src.ambientScale;
    dst.lightColor[0] = src.lightColor[0];
    dst.lightColor[1] = src.lightColor[1];
    dst.lightColor[2] = src.lightColor[2];
    dst.padLight      = src.pad1;
    dst.cameraPos[0]  = src.cameraPos[0];
    dst.cameraPos[1]  = src.cameraPos[1];
    dst.cameraPos[2]  = src.cameraPos[2];
    dst.lighting      = src.lighting;
}

void fillMeshGBufferXforms(MeshGBufferConstants& cb, const Matrix4f& world, const Matrix4f& viewProj, const Matrix4f& prevViewProj, const Matrix4f& prevWorld)
{
    copyMatrix(cb.worldViewProj, world * viewProj);
    copyMatrix(cb.world, world);
    copyMatrix(cb.prevWorldViewProj, prevWorld * prevViewProj);
}

Math::Matrix4f makeWorldMatrix(const TransformComponent& xf)
{
    return worldMatrix(xf);
}

MeshData makeGrassPatch(float sizeX, float sizeZ, uint32_t seed)
{
    MeshData m;
    uint32_t h   = seed * 2654435761u + 1u;
    auto     rnd = [&]() {
        h ^= h << 13;
        h ^= h >> 17;
        h ^= h << 5;
        return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
    };
    auto tri = [&](const Vector3f& a, const Vector3f& b, const Vector3f& c) {
        Vector3f n = (b - a).Cross(c - a);
        if (n.MagnitudeSqrd() > 1.0e-12f)
            n.Normalize();
        for (const Vector3f& p : { a, b, c })
        {
            m.indices.push_back(static_cast<uint32_t>(m.positions.size()));
            m.positions.push_back(p);
            m.normals.push_back(n);
            m.uvs.emplace_back(0.0f, p.y);
        }
    };
    const int blades = static_cast<int>(sizeX * sizeZ * 16.0f);
    for (int i = 0; i < blades; ++i)
    {
        const float    x     = (rnd() - 0.5f) * sizeX;
        const float    z     = (rnd() - 0.5f) * sizeZ;
        const float    yaw   = rnd() * 6.2831853f;
        const float    width = 0.05f + 0.04f * rnd();
        const float    tall  = 0.75f + 0.45f * rnd();
        const float    lean  = (rnd() - 0.5f) * 0.4f;
        const Vector3f side{ std::cosf(yaw) * width, 0.0f, -std::sinf(yaw) * width };
        const Vector3f base{ x, 0.0f, z };
        const Vector3f tip{ x + std::sinf(yaw) * lean, tall, z + std::cosf(yaw) * lean };
        tri(base - side, base + side, tip);
        tri(base + side, base - side, tip);
    }
    return m;
}

std::string sceneFileArg()
{
    const std::string cmd = ::GetCommandLineA();
    const size_t      at  = cmd.find("-scene:");
    if (at == std::string::npos)
        return "level.json";
    const size_t start = at + 7;
    const size_t end   = cmd.find(' ', start);
    return cmd.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

AssetID loadSandboxClip(Audio::AudioSystem& audio, AssetManager& assets, const char* path, float freq, float dur, float amp)
{
    const auto clip = audio.loadOrBlip(assets, path, freq, dur, amp);
    return (clip && clip->id != NULL_ASSET) ? clip->id : NULL_ASSET;
}

void attachCameraSounds(World& world, AssetPinTable& pins, AssetManager& assets, Audio::AudioSystem& audio, Entity camera)
{
    SoundBankComponent bank;
    addSoundCue(bank, "click", loadSandboxClip(audio, assets, "audio/ui_click.wav", 1400.0f, 0.06f, 0.35f), 0.5f, false);
    addSoundCue(bank, "reset", loadSandboxClip(audio, assets, "audio/whoosh.wav", 180.0f, 0.22f, 0.35f), 0.7f, false);
    auto music = audio.loadWav(assets, "audio/ambient_loop.wav");
    if (!music)
        music = audio.createTone(assets, 110.0f, 2.0f, 0.12f);
    if (music && music->id != NULL_ASSET)
        addSoundCue(bank, "music", music->id, 0.10f, false);
    setSoundBank(world, pins, assets, camera, std::move(bank));
}

bool playSoundCueFallback(World& world, Audio::AudioSystem& audio, AssetManager& assets, Entity e, const char* primary, const char* fallback)
{
    if (playSoundCue(world, audio, assets, e, primary))
        return true;
    return fallback && playSoundCue(world, audio, assets, e, fallback);
}

void drawShadowCaster(ID3D12GraphicsCommandList* cmd, const ShadowPipeline& pipeline, const Matrix4f& lightViewProj, const Matrix4f& world, const Mesh& mesh)
{
    const Matrix4f wvp = world * lightViewProj;
    pipeline.setWvp(cmd, wvp.m_afEntry);
    mesh.draw(cmd);
}

void drawShadowCaster(ID3D12GraphicsCommandList* cmd, const ShadowSystem& shadows, int cascade, const Matrix4f& world, const Mesh& mesh)
{
    drawShadowCaster(cmd, shadows.pipeline(), shadows.cascade(cascade).viewProj, world, mesh);
}

void unpackRgba8(uint32_t rgba, float out[4])
{
    out[0] = static_cast<float>((rgba >> 24) & 0xFFu) / 255.0f;
    out[1] = static_cast<float>((rgba >> 16) & 0xFFu) / 255.0f;
    out[2] = static_cast<float>((rgba >> 8) & 0xFFu) / 255.0f;
    out[3] = static_cast<float>(rgba & 0xFFu) / 255.0f;
}

const Mesh* sandboxPrimitiveMesh(PrimitiveMesh p, const Mesh& cube, const Mesh& cross)
{
    switch (p)
    {
    case PrimitiveMesh::Cube:
        return cube.valid() ? &cube : nullptr;
    case PrimitiveMesh::Cross:
        return cross.valid() ? &cross : nullptr;
    case PrimitiveMesh::Sphere:
    case PrimitiveMesh::None:
    default:
        return nullptr;
    }
}

// Local bounds of the meshes sandboxPrimitiveMesh returns. CreateCube(1) is ±0.5.
// CreateCross(1, 0.30, 0.22) is the union of those two cuboids.
AABox3f sandboxPrimitiveBounds(PrimitiveMesh p)
{
    switch (p)
    {
    case PrimitiveMesh::Cube:
        return AABox3f(Vector3f(-0.5f, -0.5f, -0.5f), Vector3f(0.5f, 0.5f, 0.5f));
    case PrimitiveMesh::Cross:
        return AABox3f(Vector3f(-0.5f, -0.5f, -0.11f), Vector3f(0.5f, 0.5f, 0.11f));
    case PrimitiveMesh::Sphere:
    case PrimitiveMesh::None:
    default:
        return AABox3f::Empty();
    }
}

void attachSandboxCubeModel(World& world, AssetPinTable& pins, AssetManager& assets, Entity e, AssetID modelId)
{
    ModelComponent mc{};
    mc.modelAssetID = modelId;
    mc.castShadow   = true;
    setModelComponent(world, pins, assets, e, mc);
}

SandboxApp::SandboxApp(const Dark::AppConfig& cfg)
    : Application(cfg)
{
    m_demo.bind(this);
    m_frame.bind(this);
    m_hud.bind(this);
    m_play.bind(&SandboxApp::fillPlayHost, this);
    m_dialogue.bind(&SandboxApp::fillDialogueHost, this);
    m_movie.bind(&SandboxApp::fillMovieHost, this);
}

void SandboxApp::fillMovieHost(void* user, MovieHost& host)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    host.renderer = &app->renderer();
    host.audio    = &app->audio();
    host.assets   = &app->assets();
}

void SandboxApp::fillDialogueHost(void* user, DialogueHost& host)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    host.world      = &app->world();
    host.assets     = &app->assets();
    host.audio      = &app->audio();
    host.guardVoice = &app->m_guardVoice;
    host.quest      = &app->m_play.quest();
    host.player     = app->possessedBody();
    host.session    = app->m_session;
    host.look       = app->m_viewCamera.GetLook();
    host.cameraPos  = app->m_viewCamera.GetPosition();
    host.enabled    = true;
    host.uiBlocked  = app->m_showDevTools || app->m_showInventory || app->m_showProfiler || app->m_hud.m_showJournal || app->m_hud.m_lootTarget.valid();
}

void SandboxApp::fillPlayHost(void* user, PlayHost& host)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    host.world     = &app->world();
    host.renderer  = &app->renderer();
    host.scene     = &app->m_frame.scene();
    host.assets    = &app->assets();
    host.pins      = &app->pins();
    host.audio     = &app->audio();
    host.physics   = app->m_physics.valid() ? &app->m_physics : nullptr;
    host.ai        = &app->m_chase.ai();
    host.camera    = &app->m_viewCamera;
    host.input     = &app->input();
    host.terrain   = &app->m_terrain;
    host.ground    = &app->m_ground;
    host.groundBody = app->m_physicsGround;
    host.guardVoice = &app->m_guardVoice;
    host.crows     = &app->m_crowModels;
    host.foliageCollision = &app->m_foliageCollision;
    host.foliage   = app->m_foliage.empty() ? nullptr : &app->m_foliage;

    host.player       = app->possessedBody();
    host.shield       = app->m_shield;
    host.cameraEntity = app->m_camera;
    host.muzzle       = app->m_muzzle;
    host.session      = app->m_session;

    host.waterLevel   = app->m_water.params().waterLevel;
    host.sunElevation = app->m_env.sunElevation();
    host.sunDir       = app->m_env.sunDir();
    host.cloudTime    = app->m_cloudTime;

    host.groundProbeY = &app->m_play.runtime().groundProbeY;
    host.groundIgnore = &app->m_groundIgnore;
    host.camouflage   = app->m_vision.camouflage;

    const bool ui    = app->uiOpen();
    const bool imgui = app->m_hud.m_imgui.isReady();
    const bool dialogueUi = app->m_dialogue.choiceOpen() || app->m_dialogue.locksWorld();
    host.uiKeys            = (ui && imgui && app->m_hud.m_imgui.wantCaptureKeyboard()) || dialogueUi;
    host.applyMouseLook    = !ui && !dialogueUi;
    host.allowMouseButtons = !ui && !dialogueUi;
    host.uiMouse           = ui && imgui && app->m_hud.m_imgui.wantCaptureMouse();
    host.chaseReady        = app->m_chaseOk;

    const std::vector<AABox3f>& cubes = app->m_chase.cubes();
    const std::vector<Sphere3f>& spheres = app->m_chase.spheres();
    host.cubes       = cubes.empty() ? nullptr : cubes.data();
    host.cubeCount   = static_cast<int>(cubes.size());
    host.spheres     = spheres.empty() ? nullptr : spheres.data();
    host.sphereCount = static_cast<int>(spheres.size());

    host.stepPhysics   = &SandboxApp::stepPlayPhysics;
    host.onQuestNote   = &SandboxApp::onPlayQuestNote;
    host.onDefenseText = &SandboxApp::onPlayDefenseText;
    host.onLoot        = &SandboxApp::onPlayLoot;
    host.spawnUnit     = &SandboxApp::onPlaySpawnUnit;
    host.heightAt      = &SandboxApp::onPlayHeight;
    host.groundContact = &SandboxApp::onPlayGroundContact;
    host.user          = app;
    app->bindPlayFeedback();
    installPlayFeedback(host, &app->m_playFeedback);
}

void SandboxApp::stepPlayPhysics(void* user, float dt)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    stepPossessedBody(app->m_physics, app->world(), app->possessedBody(), dt);
}

void SandboxApp::bindPlayFeedback()
{
    m_playFeedback.world       = &world();
    m_playFeedback.camera      = &m_viewCamera;
    m_playFeedback.muzzle      = &m_muzzle;
    m_playFeedback.muzzleTimer = &m_muzzleTimer;
    m_playFeedback.blood       = &m_blood;
    m_playFeedback.splats      = &m_bloodSplats;
    m_playFeedback.height      = m_terrain.coarse().valid() ? &m_terrain.coarse() : nullptr;
    m_playFeedback.afterRespawn = &SandboxApp::onPlayRespawn;
    m_playFeedback.afterUser   = this;
}

void SandboxApp::onPlayRespawn(void* user)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    app->m_vision.nightSightOn  = false;
    app->m_vision.nightSight    = 0.0f;
    app->m_vision.infraredOn    = false;
    app->m_vision.infrared      = 0.0f;
    app->m_vision.ultravioletOn = false;
    app->m_vision.ultraviolet   = 0.0f;
    app->m_vision.echoOn        = false;
    app->m_vision.echo          = 0.0f;
    app->m_vision.echoHaveChirp = false;
    app->m_vision.echoCapture   = false;
}

void SandboxApp::onPlayQuestNote(void* user, const QuestNoteEvent& note)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    if (note.kind != QuestNote::NewObjective || app->m_hud.m_questBanners.size() < 4)
        app->m_hud.m_questBanners.push_back(makeQuestBanner(note));
}

void SandboxApp::onPlayDefenseText(void* user, bool parried)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    app->m_hud.m_defenseText     = parried ? "PARRY!" : "BLOCKED";
    app->m_hud.m_defenseTextLeft = 0.7f;
    app->m_hud.m_defenseParry    = parried;
}

void SandboxApp::onPlayLoot(void* user, Entity loot)
{
    if (auto* app = static_cast<SandboxApp*>(user))
        app->m_hud.m_lootTarget = loot;
}

Entity SandboxApp::onPlaySpawnUnit(void* user, Entity spawner, const SpawnerRequest& req)
{
    auto* app = static_cast<SandboxApp*>(user);
    return app ? app->spawnSpawnerUnit(spawner, req) : Entity{};
}

float SandboxApp::onPlayHeight(void* user, float x, float z)
{
    auto* app = static_cast<SandboxApp*>(user);
    return app ? app->playerGroundHeight(x, z) : 0.0f;
}

Terrain::GroundContact SandboxApp::onPlayGroundContact(void* user, float x, float z)
{
    auto* app = static_cast<SandboxApp*>(user);
    return app ? app->groundContactAt(x, z) : Terrain::GroundContact{};
}

void SandboxApp::expandSceneBuildings(SceneFileData& scene)
{
    m_buildingContainers.clear();
    m_buildingHides.clear();
    m_buildingLabels.clear();
    m_buildingDoors.clear();
    m_buildingCameras.clear();
    Dark::expandSceneBuildings(scene, m_buildingContainers, m_buildingHides, &m_buildingDoors, &m_buildingCameras);
}

bool SandboxApp::spawnBuildingExtras(BuildingFixtureSpawn& spawn)
{
    spawn.world    = &world();
    spawn.renderer = &renderer();
    spawn.assets   = &assets();
    spawn.pins     = &pins();
    spawn.ai       = &m_chase.ai();
    spawn.labels   = &m_buildingLabels;
    spawn.stampPrefix = "sandbox/level";
    return spawnBuildingFixtures(spawn, m_buildingContainers, m_buildingHides, m_buildingDoors, m_buildingCameras);
}

void SandboxApp::startQuest(const std::string& id)
{
    m_play.startQuest(id);
}

void SandboxApp::updateQuest(float dt)
{
    if (m_hud.m_questBanners.empty())
        return;
    m_hud.m_questBanners.front().timeLeft -= dt;
    if (m_hud.m_questBanners.front().timeLeft <= 0.0f)
        m_hud.m_questBanners.erase(m_hud.m_questBanners.begin());
}

int SandboxApp::giveItem(InventoryComponent& inv, const ItemStack& item)
{
    return grantInventoryItem(world(), audio(), assets(), m_camera, inv, item);
}

void SandboxApp::stockHunter(Entity hunter)
{
    m_play.stockHunter(hunter);
}

WeaponWorldQuery SandboxApp::makeWeaponQuery()
{
    return m_play.weaponQuery();
}

void SandboxApp::registerDefaultActions()
{
    ActionMap& a = input().actions();
    a.clear();

    // App commands first. The player file loads second and replaces any action it names.
    if (!applySandboxInputFile(a))
        DE_LOG_ERROR(LogCategory::Input, "Sandbox: sandbox input file was not applied");

    if (!applyPlayerInputFile(a, "sandbox"))
        DE_LOG_ERROR(LogCategory::Input, "Sandbox: player input file was not applied");

    DE_LOG_INFO(
        "Input: quit(Esc/Back) pause(F3/Start) freeze gameplay + fly cam  step(O)  reset(R/Y) speed(+/- / RB) "
        "possessed WASD/arrows/LS move, double-tap a direction to dodge, mouse+RS look, Space/A jump (tap again quickly for a higher jump), LMB/F/B attack, 1 melee  2 rifle, "
        "hold attack to charge, release to swing (tap = normal). Hold RMB/V/LT shield; hold longer then release to parry-slam. Ctrl crouch, C camouflage, G pickup / carry body, E takedown from behind, L flashlight, N night sight, H infrared, U ultraviolet, B echolocation, Shift/LB sprint, swim in water, "
        "Y/R3 walk the wiggle demo  T talk  F2 lighting  F5 quicksave  F9 quickload  M dev tools  P profiler  -forward linear albedo + sRGB encode (legacyUnormAlbedo restores old sampling)  -no-menu skip the front menu  -no-debug-levels hides jump to level");
}

void SandboxApp::populateMainMenu()
{
    m_hud.m_menu.clearEntries();
    m_hud.m_menu.setTitle("CRISPER");
    m_hud.m_menu.setAccent(UiAccent::Sandbox);
    m_hud.m_menuPage = MainMenuPage::Front;
    m_hud.m_menu.addBuiltIn("continue", "CONTINUE", "SECTOR 04 - QUARANTINE PERIMETER");
    m_hud.m_menu.addBuiltIn("new_game", "NEW GAME");
    m_hud.m_menu.addBuiltIn("settings", "SETTINGS");
    m_hud.m_menu.addBuiltIn("credits", "CREDITS");
    m_hud.m_menu.addQuit("QUIT");
    // Sandbox is the 3D host. level2d.json stays on the Sandbox2D menu.
    if (shouldShowDebugLevelJump(config()))
    {
        const auto scenes = listSceneFiles(SceneMode::Scene3D);
        for (const SceneFileInfo& s : scenes)
            m_hud.m_menu.addScene(s.fileName, s.displayName, s.path, s.fileName, MainMenuRole::Level);
    }
    const std::string sceneArg = sceneFileArg();
    bool jumped = false;
    if (sceneArg != "level.json")
    {
        jumped = m_hud.m_menu.selectById(sceneArg) || m_hud.m_menu.selectById(std::filesystem::path(sceneArg).filename().string());
        if (jumped && m_hud.m_menu.selected() && m_hud.m_menu.selected()->role != MainMenuRole::Level)
            jumped = false;
    }
    if (!jumped)
        m_hud.m_menu.selectById("continue");
}

void SandboxApp::handleRuntimeCommands(float dt)
{
    // Splash teardown already ran. One slice here, then the main loop Presents.
    if (m_bootPolling && m_bootSlice != BootSlice::Done)
    {
        if (!advanceBootSlice())
        {
            m_bootPolling = false;
            return;
        }
        if (m_bootSlice == BootSlice::Done)
        {
            m_bootPolling = false;
            if (m_sceneStarted)
            {
                playMusicCue(world(), audio(), assets(), m_camera, "music");
                m_hud.m_menu.hide();
            }
        }
    }

    if (m_hud.m_menu.visible())
    {
        window().setCursorCaptured(false);
        if (!m_hud.m_imgui.isReady())
            m_hud.m_menu.update(input(), renderer().width(), renderer().height());
        switch (m_hud.m_menu.pollResult())
        {
        case MainMenuResult::Confirm:
        {
            // The 3D world is built once. Esc brings this menu back;
            // confirming a different level does not tear the session down.
            const MainMenuEntry* picked = m_hud.m_menu.selected();
            if (picked && picked->id == "settings")
            {
                m_hud.m_menuPage = MainMenuPage::Settings;
                break;
            }
            if (picked && picked->id == "credits")
            {
                m_hud.m_menuPage = MainMenuPage::Credits;
                break;
            }
            if (!m_sceneStarted)
            {
                if (m_bootPolling)
                    break;
                std::filesystem::path path = defaultScenePath(sceneFileArg());
                if (picked)
                {
                    if (picked->id == "new_game")
                        path = defaultScenePath("level.json");
                    else if (picked->kind == MainMenuKind::Scene && !picked->path.empty())
                        path = picked->path;
                    else if (picked->kind == MainMenuKind::BuiltIn && picked->id != "continue")
                        path = defaultScenePath("level.json");
                }
                if (!startScene(path))
                {
                    m_hud.m_menu.hide();
                    return;
                }
                // The load is still crossing frames. Music waits until the scene starts.
                if (!m_sceneStarted)
                    break;
                // Splash already finished without a camera, so the usual cue never played.
                playMusicCue(world(), audio(), assets(), m_camera, "music");
            }
            else if (picked && picked->id == "new_game")
            {
                DE_LOG_INFO("Sandbox: already in '{}'; staying in this session", m_loadedScenePath.filename().string());
                break;
            }
            else if (picked)
            {
                const bool otherScene = picked->kind == MainMenuKind::Scene && !picked->path.empty()
                    && picked->path.filename() != m_loadedScenePath.filename();
                if (otherScene)
                    DE_LOG_INFO("Sandbox: already in '{}'; staying in this scene", m_loadedScenePath.filename().string());
            }
            m_hud.m_menuPage = MainMenuPage::Front;
            m_hud.m_menu.hide();
            break;
        }
        case MainMenuResult::Quit:
            DE_LOG_INFO("Command: quit");
            requestQuit();
            return;
        default:
            break;
        }
        return;
    }

    handleNetHotkeys();
    applyNetRole();

    const bool uiKeys = uiOpen() && m_hud.m_imgui.isReady() && m_hud.m_imgui.wantCaptureKeyboard();
    // Playing swaps after the charge step. Paused play never reaches that step, so it still swaps here.
    if (!uiKeys && m_gameplayPaused)
        m_play.switchWeapons();
    // Dev tools and inventory become the keyboard-nav target, so WantCaptureKeyboard
    // is set and would swallow the same key that should close them. A text field still owns the key.
    const bool typing = m_hud.m_imgui.isReady() && m_hud.m_imgui.wantTextInput();
    if (input().actionPressed("profiler") && !typing && (!uiKeys || m_showProfiler))
    {
        m_showProfiler = !m_showProfiler;
        playSoundCue(world(), audio(), assets(), m_camera, "click");
        DE_LOG_INFO("Sandbox: profiler = {}", m_showProfiler);
    }
    if (input().actionPressed("dev_tools") && !typing && (!uiKeys || m_showDevTools))
    {
        m_showDevTools = !m_showDevTools;
        playSoundCue(world(), audio(), assets(), m_camera, "click");
        DE_LOG_INFO("Sandbox: dev tools = {}", m_showDevTools);
    }
    if (input().actionPressed("inventory") && !typing && (!uiKeys || m_showInventory))
    {
        m_showInventory = !m_showInventory;
        playSoundCue(world(), audio(), assets(), m_camera, "click");
    }
    if (input().actionPressed("journal") && !typing && (!uiKeys || m_hud.m_showJournal))
    {
        m_hud.m_showJournal = !m_hud.m_showJournal;
        playSoundCue(world(), audio(), assets(), m_camera, "click");
    }
    if (input().actionPressed("upgrades") && !typing && (!uiKeys || m_hud.m_showUpgrades))
    {
        m_hud.m_showUpgrades = !m_hud.m_showUpgrades;
        playSoundCue(world(), audio(), assets(), m_camera, "click");
    }
    if (!uiKeys && input().actionPressed("camouflage"))
    {
        m_vision.camouflage = !m_vision.camouflage;
        DE_LOG_INFO("Sandbox: camouflage = {}", m_vision.camouflage);
    }
    {
        Health* nightHp = localHealth();
        const bool nightAlive = nightHp && nightHp->alive();
        if (!uiKeys && nightAlive && input().actionPressed("night_sight"))
        {
            m_vision.nightSightOn = !m_vision.nightSightOn;
            DE_LOG_INFO("Sandbox: night sight = {}", m_vision.nightSightOn);
        }
        if (!uiKeys && nightAlive && input().actionPressed("infrared"))
        {
            m_vision.infraredOn = !m_vision.infraredOn;
            DE_LOG_INFO("Sandbox: infrared = {}", m_vision.infraredOn);
            if (m_vision.infraredOn)
            {
                m_vision.ultravioletOn = false;
                m_vision.ultraviolet   = 0.0f;
                m_vision.echoOn        = false;
                m_vision.echo          = 0.0f;
                m_vision.echoHaveChirp = false;
                m_vision.echoCapture   = false;
            }
        }
        if (!uiKeys && nightAlive && input().actionPressed("ultraviolet"))
        {
            m_vision.ultravioletOn = !m_vision.ultravioletOn;
            DE_LOG_INFO("Sandbox: ultraviolet = {}", m_vision.ultravioletOn);
            if (m_vision.ultravioletOn)
            {
                m_vision.infraredOn    = false;
                m_vision.infrared      = 0.0f;
                m_vision.echoOn        = false;
                m_vision.echo          = 0.0f;
                m_vision.echoHaveChirp = false;
                m_vision.echoCapture   = false;
            }
        }
        if (!uiKeys && nightAlive && input().actionPressed("echolocation"))
        {
            m_vision.echoOn = !m_vision.echoOn;
            DE_LOG_INFO("Sandbox: echolocation = {}", m_vision.echoOn);
            if (m_vision.echoOn)
            {
                m_vision.infraredOn    = false;
                m_vision.infrared      = 0.0f;
                m_vision.ultravioletOn = false;
                m_vision.ultraviolet   = 0.0f;
                m_vision.echoHaveChirp = false;
                m_vision.echoCapture   = true;
                m_vision.echoChirpAge  = 0.0f;
            }
        }
        if (!nightAlive)
        {
            m_vision.nightSightOn  = false;
            m_vision.nightSight    = 0.0f;
            m_vision.infraredOn    = false;
            m_vision.infrared      = 0.0f;
            m_vision.ultravioletOn = false;
            m_vision.ultraviolet   = 0.0f;
            m_vision.echoOn        = false;
            m_vision.echo          = 0.0f;
            m_vision.echoHaveChirp = false;
            m_vision.echoCapture   = false;
        }
        else
        {
            const float target = m_vision.nightSightOn ? 1.0f : 0.0f;
            const float step   = dt > 0.0f ? dt / kNightSightBlendSeconds : 0.0f;
            if (m_vision.nightSight < target)
                m_vision.nightSight = Math::Min(target, m_vision.nightSight + step);
            else
                m_vision.nightSight = Math::Max(target, m_vision.nightSight - step);

            const float heatTarget = m_vision.infraredOn ? 1.0f : 0.0f;
            const float heatStep   = dt > 0.0f ? dt / kInfraredBlendSeconds : 0.0f;
            if (m_vision.infrared < heatTarget)
                m_vision.infrared = Math::Min(heatTarget, m_vision.infrared + heatStep);
            else
                m_vision.infrared = Math::Max(heatTarget, m_vision.infrared - heatStep);

            const float uvTarget = m_vision.ultravioletOn ? 1.0f : 0.0f;
            const float uvStep   = dt > 0.0f ? dt / kUltravioletBlendSeconds : 0.0f;
            if (m_vision.ultraviolet < uvTarget)
                m_vision.ultraviolet = Math::Min(uvTarget, m_vision.ultraviolet + uvStep);
            else
                m_vision.ultraviolet = Math::Max(uvTarget, m_vision.ultraviolet - uvStep);

            const float echoTarget = m_vision.echoOn ? 1.0f : 0.0f;
            const float echoStep   = dt > 0.0f ? dt / kEchoBlendSeconds : 0.0f;
            if (m_vision.echo < echoTarget)
                m_vision.echo = Math::Min(echoTarget, m_vision.echo + echoStep);
            else
                m_vision.echo = Math::Max(echoTarget, m_vision.echo - echoStep);

            if (m_vision.echoOn || m_vision.echo > 0.0f)
            {
                m_vision.echoChirpAge += dt;
                if (!m_vision.echoHaveChirp || m_vision.echoChirpAge >= m_vision.echoChirp.periodSeconds)
                {
                    m_vision.echoCapture  = true;
                    m_vision.echoChirpAge = 0.0f;
                }
            }
        }
    }
    if (!uiKeys && input().actionPressed("toggle_lighting"))
    {
        DebugRenderState& dbg = renderer().debugState();
        dbg.lighting          = !dbg.lighting;
        DE_LOG_INFO("Sandbox: lighting = {}", dbg.lighting);
    }

    if (input().actionPressed("quit"))
    {
        if (m_movie.blocksWorld())
        {
            m_movie.stop();
            return;
        }
        if (m_dialogue.active())
        {
            m_dialogue.interrupt(DialogueInterrupt::Cancel);
            return;
        }
        if (config().showMainMenu && m_hud.m_menu.isReady() && network().role() == NetRole::Idle)
        {
            m_dialogue.interrupt(DialogueInterrupt::Menu);
            m_hud.m_menuPage = MainMenuPage::Front;
            m_hud.m_menu.show();
            window().setCursorCaptured(false);
            return;
        }
        DE_LOG_INFO("Command: quit");
        requestQuit();
        return;
    }

    if (!uiKeys && input().actionPressed("quick_save"))
    {
        const Save::SaveResult result = m_save.requestSave(Save::SaveKind::Quick, "Quick Save");
        DE_LOG_INFO("Sandbox: quicksave {}", Save::toString(result));
    }
    if (!uiKeys && input().actionPressed("quick_load"))
    {
        const Save::SaveResult result = m_save.requestLoadNewest(Save::SaveKind::Quick);
        DE_LOG_INFO("Sandbox: quickload {}", Save::toString(result));
    }

    if (input().actionPressed("pause") && !m_movie.blocksWorld())
    {
        const bool dialogueWasOpen = m_dialogue.active();
        if (!uiKeys || dialogueWasOpen)
        {
            if (dialogueWasOpen)
                m_dialogue.interrupt(DialogueInterrupt::Pause);
            const bool otherUi = m_showDevTools || m_showInventory || m_showProfiler || m_hud.m_showJournal || m_hud.m_lootTarget.valid();
            if (!uiKeys || !otherUi)
            {
                m_gameplayPaused = !m_gameplayPaused;
                m_stepGameplay   = false;
                playSoundCue(world(), audio(), assets(), m_camera, "click");
                DE_LOG_INFO("Sandbox: gameplay paused = {}", m_gameplayPaused);
            }
        }
    }
    if (!typing && m_gameplayPaused && input().actionPressed("step") && (!uiKeys || !m_showDevTools))
        m_stepGameplay = true;

    if (!uiKeys && input().actionPressed("reset"))
    {
        m_spinSpeed  = 0.8f;
        Vector3f pos{};
        if (m_demo.cube().valid())
        {
            if (auto* xf = world().get<TransformComponent>(m_demo.cube()))
            {
                xf->rotation = Quaternion::IDENTITY;
                pos          = xf->position;
            }
        }
        playSoundCueAt(world(), audio(), assets(), m_camera, "reset", pos);
        DE_LOG_INFO("Command: reset cube");
    }

    if (!uiKeys && input().actionPressed("speed_up"))
    {
        m_spinSpeed += 0.2f;
        if (m_spinSpeed > 5.0f)
            m_spinSpeed = 5.0f;
        DE_LOG_INFO("Command: spin speed = {:.2f}", m_spinSpeed);
    }

    if (!uiKeys && input().actionPressed("speed_down"))
    {
        m_spinSpeed -= 0.2f;
        if (m_spinSpeed < 0.0f)
            m_spinSpeed = 0.0f;
        DE_LOG_INFO("Command: spin speed = {:.2f}", m_spinSpeed);
    }

    if (m_sceneStarted && !typing && input().keyPressed(Key::F8) && m_movie.state() == MoviePlayState::Closed)
    {
        audio().stopMusic();
        audio().stopClipVoices();
        if (m_movie.open("movies/sample.mp4") == MovieStatus::Ok)
            m_movie.play();
    }
    if (m_sceneStarted && !typing && input().keyPressed(Key::F6))
    {
        if (m_movie.state() == MoviePlayState::Paused)
            m_movie.play();
        else
            m_movie.pause();
    }
    if (m_sceneStarted && m_movie.blocksWorld() && !typing && input().keyPressed(Key::F7))
        m_movie.seek(m_movie.stats().clockSeconds - 5.0);
    if (m_sceneStarted && m_movie.blocksWorld() && !typing && input().keyPressed(Key::F10))
        m_movie.seek(m_movie.stats().clockSeconds + 5.0);

    if (!m_movie.blocksWorld() && !typing && input().actionPressed("talk"))
    {
        if (m_dialogue.active())
            m_dialogue.requestSkip();
        else if (!uiKeys)
            m_dialogue.tryBegin();
    }
    if (m_dialogue.choiceOpen())
    {
        if (input().keyPressed(Key::Up))
            m_dialogue.moveChoice(-1);
        if (input().keyPressed(Key::Down))
            m_dialogue.moveChoice(1);
        if (input().keyPressed(Key::Enter))
            m_dialogue.commitChoice();
    }

    window().setCursorCaptured(window().isFocused() && !uiOpen() && !m_hud.m_menu.visible());
    if (!m_dialogue.locksWorld() && !m_movie.blocksWorld())
    {
        if (m_gameplayPaused)
            updateFlyCamera(dt);
        else
        {
            m_play.tickPawn(dt);
            m_play.tickCamera();
        }
    }

    if (m_gameplayPaused && !m_stepGameplay)
    {
        m_leafRustle.stopAll(audio());
        return;
    }

    if (auto* xf = m_demo.cube().valid() ? world().get<TransformComponent>(m_demo.cube()) : nullptr)
    {
        const NetRole role = network().role();
        if (role == NetRole::Host)
        {
            constexpr float kTurnRate = 1.8f; // rad/s
            const float     yawCmd    = input().actionAxis("yaw");
            const float     pitchCmd  = input().actionAxis("pitch");
            if (yawCmd != 0.0f || pitchCmd != 0.0f)
            {
                const Quaternion yawQ   = Quaternion::FromAxisAngle(Vector3f::Y_AXIS, yawCmd * kTurnRate * dt);
                const Quaternion pitchQ = Quaternion::FromAxisAngle(Vector3f::X_AXIS, pitchCmd * kTurnRate * dt);
                xf->rotation            = yawQ * pitchQ * xf->rotation;
                xf->rotation.Normalize();
            }
        }

        // Host (and offline Idle) still spin the cube. Clients must not.
        if (role == NetRole::Host || role == NetRole::Idle)
        {
            const Quaternion spin = Quaternion::FromAxisAngle(Vector3f::Y_AXIS, m_spinSpeed * dt);
            xf->rotation          = spin * xf->rotation;
            xf->rotation.Normalize();
        }

        xf->position.y = m_terrain.heightAtWorld(xf->position.x, xf->position.z) + 0.5f;
    }
}

void SandboxApp::updateFlyCamera(float dt)
{
    const bool uiKeys  = uiOpen() && m_hud.m_imgui.isReady() && m_hud.m_imgui.wantCaptureKeyboard();
    const bool uiMouse = uiOpen() && m_hud.m_imgui.isReady() && m_hud.m_imgui.wantCaptureMouse();
    const bool typing  = m_hud.m_imgui.isReady() && m_hud.m_imgui.wantTextInput();
    const bool pauseCard = m_gameplayPaused && !m_showDevTools && !m_showInventory && !m_showProfiler && !m_hud.m_showJournal && !m_hud.m_showUpgrades
        && !m_hud.m_lootTarget.valid() && !typing;

    if (!uiKeys || pauseCard)
    {
        const float forward = input().actionAxis("fly_forward");
        const float strafe  = input().actionAxis("fly_strafe");
        const float climb   = input().actionAxis("fly_climb");
        const bool  sprint  = input().keyDown(Key::LeftShift) || input().actionDown("sprint");
        const float speed   = (sprint ? 2.5f : 1.0f) * m_flySpeed;

        if (forward != 0.0f)
            m_viewCamera.Walk(forward * speed * dt);
        if (strafe != 0.0f)
            m_viewCamera.Strafe(strafe * speed * dt);
        if (climb != 0.0f)
            m_viewCamera.Climb(climb * speed * dt);
    }

    constexpr float kSens    = 0.0045f;
    constexpr float kPadLook = 2.1f;
    if (!uiMouse)
    {
        if (!uiOpen() || input().mouseDown(MouseButton::Right))
        {
            m_viewCamera.RotateY(static_cast<float>(input().mouseDeltaX()) * kSens);
            m_viewCamera.Pitch(static_cast<float>(input().mouseDeltaY()) * kSens);
        }
    }

    const float lookYaw   = input().actionAxis("look_yaw");
    const float lookPitch = input().actionAxis("look_pitch");
    if (lookYaw != 0.0f)
        m_viewCamera.RotateY(lookYaw * kPadLook * dt);
    if (lookPitch != 0.0f)
        m_viewCamera.Pitch(lookPitch * kPadLook * dt);

    if (auto* xf = world().get<TransformComponent>(m_camera))
        xf->position = m_viewCamera.GetPosition();
}

void SandboxApp::devNetHost()
{
    m_netBrowsing    = false;
    m_browseLogCount = ~0u;
    if (network().host(kNetDefaultPort))
        DE_LOG_INFO(LogCategory::Networking, "Sandbox: hosting on port {}", kNetDefaultPort);
}

void SandboxApp::devNetJoin(const Address& addr)
{
    m_netBrowsing    = false;
    m_browseLogCount = ~0u;
    if (network().join(addr))
    {
        const uint32_t ip = addr.ipv4;
        DE_LOG_INFO(LogCategory::Networking, "Sandbox: joining {}.{}.{}.{}:{}", (ip >> 24) & 255u, (ip >> 16) & 255u, (ip >> 8) & 255u, ip & 255u, addr.port);
    }
}

void SandboxApp::devNetDisconnect()
{
    m_netBrowsing    = false;
    m_browseLogCount = ~0u;
    network().disconnect();
    DE_LOG_INFO(LogCategory::Networking, "Sandbox: disconnect");
}

void SandboxApp::devNetBrowse()
{
    if (network().role() != NetRole::Idle)
        DE_LOG_WARN(LogCategory::Networking, "Sandbox: browse requires Idle (disconnect first)");
    else if (network().browse())
    {
        m_netBrowsing    = true;
        m_browseLogCount = ~0u;
        DE_LOG_INFO(LogCategory::Networking, "Sandbox: browsing LAN :{} (same-PC two binds of :{} is unreliable; join by IP)", kNetBeaconPort, kNetBeaconPort);
    }
    else
        DE_LOG_WARN(LogCategory::Networking, "Sandbox: browse bind failed; typed IP / CLI still work");
}

void SandboxApp::devToggleListen()
{
    if (debug().isListening())
    {
        debug().shutdown();
        DE_LOG_INFO(LogCategory::Debug, "Sandbox: Visual Debugger listen stopped");
    }
    else if (debug().listen(kDebugDefaultPort))
        DE_LOG_INFO(LogCategory::Debug, "Sandbox: Visual Debugger listening TCP {}", debug().boundAddress().port);
    else
        DE_LOG_ERROR(LogCategory::Debug, "Sandbox: Visual Debugger listen failed");
}

void SandboxApp::handleNetHotkeys()
{
    if (m_netBrowsing && network().role() == NetRole::Idle)
    {
        const uint32_t n = network().sessionCount();
        if (n != m_browseLogCount)
        {
            m_browseLogCount = n;
            DE_LOG_INFO(LogCategory::Networking, "Sandbox: {} LAN session(s)", n);
            for (uint32_t i = 0; i < n; ++i)
            {
                NetSessionInfo s{};
                if (!network().sessionAt(i, s))
                    continue;
                const uint32_t ip = s.address.ipv4;
                DE_LOG_INFO(LogCategory::Networking, "Sandbox: session '{}' {}.{}.{}.{}:{} peers {} mode {}",
                            s.name[0] ? s.name : "(unnamed)",
                            (ip >> 24) & 255u, (ip >> 16) & 255u, (ip >> 8) & 255u, ip & 255u,
                            s.address.port, s.peerCount, s.sceneMode);
            }
        }
    }
    else if (network().role() != NetRole::Idle)
        m_netBrowsing = false;
}

void SandboxApp::applyNetRole()
{
    const NetRole role = network().role();
    if (role != m_netRole)
    {
        DE_LOG_INFO(
            LogCategory::Networking,
            "Sandbox: role {} peers {} rtt {:.1f}ms pkts in/out {}/{}",
            netRoleName(role),
            network().peerCount(),
            network().rttMs(network().localClientId()),
            network().packetsIn(),
            network().packetsOut());
        m_netRole = role;
    }

    // Idle-tagged replicas (netId=0) must not sit beside the host's spawned cube/pawns.
    if (role == NetRole::Client)
    {
        std::vector<Entity> stale;
        world().each<NetworkedComponent>([&](Entity e, NetworkedComponent& nc) {
            if (nc.netId == NULL_NET_ID)
                stale.push_back(e);
        });
        for (Entity e : stale)
            network().unregisterEntity(world(), e);
        m_demo.cube() = {};
    }

    if (role == NetRole::Host && !network().localPawn().valid())
        spawnOwnedPawn(ClientId::Host, -2.0f);

    if (role == NetRole::Idle)
        ensureLocalCube();
}

void SandboxApp::updatePawnMotion(float dt)
{
    const Entity pawn = network().localPawn();
    if (!pawn.valid())
        return;
    TransformComponent* xf = world().get<TransformComponent>(pawn);
    if (!xf)
        return;

    const float ax = input().actionAxis("pawn_x");
    const float az = input().actionAxis("pawn_z");
    if (ax != 0.0f || az != 0.0f)
    {
        Vector3f delta{ ax, 0.0f, az };
        const float mag = delta.Magnitude();
        if (mag > 1.0f)
            delta *= (1.0f / mag);
        xf->position += delta * (kNetPawnMaxSpeed * dt);
    }

    xf->position.y = m_terrain.heightAtWorld(xf->position.x, xf->position.z) + 0.5f;
}

Entity SandboxApp::possessedBody()
{
    const Entity pawn = network().localPawn();
    if (pawn.valid())
        return pawn;
    return m_chase.walker();
}

bool SandboxApp::camouflageHides(Entity e)
{
    if (!m_vision.camouflage || !e.valid())
        return false;
    const Entity body = possessedBody();
    return body.valid() && e.id() == body.id();
}

bool SandboxApp::skipShadowModel(void* user, Entity e)
{
    auto* ctx = static_cast<SandboxShadowUser*>(user);
    if (!ctx || !ctx->app)
        return false;
    return ctx->app->camouflageHides(e);
}

void SandboxApp::drawPrimitiveShadowCasters(void* user, ID3D12GraphicsCommandList* cmd, const Matrix4f& lightViewProj, const Frustum3f& frustum, bool recordThisView)
{
    auto* ctx = static_cast<SandboxShadowUser*>(user);
    if (!ctx || !ctx->app)
        return;
    if (!capturingShadowCasters() && !cmd)
        return;
    SandboxApp& app = *ctx->app;
    app.world().each<MeshComponent>([&](Entity e, MeshComponent& mc) {
        if (app.world().has<ModelComponent>(e))
            return;
        if (!mc.castShadow)
            return;
        if (const HealthComponent* hp = app.world().get<HealthComponent>(e); hp && !hp->health.alive())
            return;
        const Mesh* gpuMesh = sandboxPrimitiveMesh(mc.primitive, app.m_demo.cubeMesh(), app.m_crossMesh);
        if (!gpuMesh)
            return;
        const TransformComponent* xf = app.world().get<TransformComponent>(e);
        if (!xf)
            return;
        const Matrix4f worldMat = makeWorldMatrix(*xf);
        const AABox3f local = sandboxPrimitiveBounds(mc.primitive);
        if (local.IsValid() && !frustum.Intersects(local.Transformed(worldMat)))
            return;
        if (recordThisView && ctx->acceptedMesh && ctx->acceptedMesh->contains(e.id()))
            return;
        if (capturingShadowCasters())
        {
            pushShadowCaster(gpuMesh, worldMat);
            return;
        }
        drawShadowCaster(cmd, app.m_shadows.pipeline(), lightViewProj, worldMat, *gpuMesh);
    });
}

void SandboxApp::attachReplicaCombat(Entity e)
{
    if (!e.valid() || !world().alive(e))
        return;
    ensureHumanoidBody(world(), e, 200.0f, Vector3f{ 0.35f, 0.7f, 0.35f });
    const EntityMaster playerMaster = loadEntityMasterType("player");
    const std::string  playerGltf = playerMaster.gltf.empty() ? std::string("models/human.gltf") : playerMaster.gltf;
    attachAnimatedCharacter(e, playerGltf.c_str());
}

void SandboxApp::attachLocalPlayer(Entity e)
{
    attachReplicaCombat(e);
    m_play.stockPlayer(e);
    if (!world().has<HudTagComponent>(e))
    {
        HudTagComponent hud{};
        hud.kind = HudKind::HealthBar;
        world().emplace<HudTagComponent>(e, hud);
    }
    const CharacterHost host{ world(), renderer(), assets(), pins(), audio(), m_ground };
    ensureLocalPlayerRig(host, e, &SandboxApp::onWeaponHitThunk, this);
    ensurePlayerProgress(e);
    ensurePlayerStealth(e);
}

void SandboxApp::ensurePlayerStealth(Entity e)
{
    if (!e.valid() || !world().alive(e) || world().has<PlayerStealthComponent>(e))
        return;
    PlayerStealthComponent& stealth = world().emplace<PlayerStealthComponent>(e);
    applyScenePrey(stealth.settings, m_sceneStealth);
}

PlayerStealthComponent* SandboxApp::localStealth()
{
    const Entity body = possessedBody();
    return body.valid() ? world().get<PlayerStealthComponent>(body) : nullptr;
}

bool SandboxApp::attachAnimatedCharacter(Entity e, const char* gltfPath)
{
    if (!e.valid() || !world().alive(e) || !gltfPath || gltfPath[0] == '\0')
        return false;
    if (world().has<AnimGraphComponent>(e) && world().has<ModelComponent>(e))
        return true;

    const CharacterHost host{ world(), renderer(), assets(), pins(), audio(), m_ground };
    const ContentModelResult result = attachContentModel(host, e, gltfPath, ModelAttach::AnimatedOnly);
    return result.attached && result.skinned;
}


Health* SandboxApp::localHealth()
{
    const Entity body = possessedBody();
    HealthComponent* hc = body.valid() ? world().get<HealthComponent>(body) : nullptr;
    return hc ? &hc->health : nullptr;
}

HitReaction* SandboxApp::localHit()
{
    const Entity body = possessedBody();
    HitReactionComponent* hr = body.valid() ? world().get<HitReactionComponent>(body) : nullptr;
    return hr ? &hr->hit : nullptr;
}

PlayerMotor* SandboxApp::localMotor()
{
    const Entity body = possessedBody();
    PlayerMotorComponent* pm = body.valid() ? world().get<PlayerMotorComponent>(body) : nullptr;
    return pm ? &pm->motor : nullptr;
}

WeaponLoadout* SandboxApp::localWeapons()
{
    const Entity body = possessedBody();
    WeaponLoadoutComponent* wlc = body.valid() ? world().get<WeaponLoadoutComponent>(body) : nullptr;
    return (wlc && wlc->loadout) ? wlc->loadout.get() : nullptr;
}
















bool SandboxApp::questTarget(Vector3f& out)
{
    return m_play.quest().objectiveTarget(world(), possessedBody(), out);
}







TonemapSettings SandboxApp::playerPostFx()
{
    TonemapSettings s{};
    s.nearZ      = m_viewCamera.GetNearZ();
    s.farZ       = m_viewCamera.GetFarZ();
    s.focusRange = 12.0f;

    const Entity body = possessedBody();
    if (const TransformComponent* xf = body.valid() ? world().get<TransformComponent>(body) : nullptr)
    {
        const Vector3f focusPt{ xf->position.x, xf->position.y + 1.15f, xf->position.z };
        s.focusZ = (focusPt - m_viewCamera.GetPosition()).Magnitude();
    }

    Health* hpFx = localHealth();
    if (hpFx && !hpFx->alive())
    {
        const float t    = Clamp(m_play.runtime().deadTimer / kPlayDeathSeconds, 0.0f, 1.0f);
        s.blur           = SmoothStep(0.0f, 0.55f, t);
        s.uniformBlur    = SmoothStep(0.0f, 0.70f, t);
        s.fade           = SmoothStep(0.30f, 1.00f, t);
        s.nightSight     = 0.0f;
        return s;
    }

    const float u    = SmoothStep(0.0f, kPlaySpawnFocusSeconds, m_play.runtime().spawnAge);
    s.blur           = 1.0f - u;
    s.uniformBlur    = 1.0f - u;
    s.fade           = 0.0f;
    s.nightSight     = (m_vision.infrared > 0.0f || m_vision.ultraviolet > 0.0f || m_vision.echo > 0.0f) ? 0.0f : m_vision.nightSight;
    return s;
}

bool SandboxApp::ownsWeapon(WeaponKind kind)
{
    const Entity body = possessedBody();
    if (kind == WeaponKind::Throwable)
    {
        if (!body.valid())
            return false;
        const ThrowableBeltComponent* belt = world().get<ThrowableBeltComponent>(body);
        return belt && belt->belt.any();
    }
    const InventoryComponent* inv = body.valid() ? world().get<InventoryComponent>(body) : nullptr;
    const char*               id  = kind == WeaponKind::Melee ? "sword" : "rifle";
    return !inv || hasItem(*inv, id);
}

bool SandboxApp::bindListedPhysics(void* user, Entity e, bool wolf)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return false;
    app->bindEntityPhysics(e, wolf ? "wolf" : "human");
    return true;
}

void SandboxApp::noteListedHunter(void* user, Entity e)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app || !e.valid())
        return;
    if (PathAgentComponent* path = app->world().get<PathAgentComponent>(e))
        path->repathAt = static_cast<float>(app->m_chase.hunterCount()) * (0.5f / 3.0f);
    app->m_chase.addHunter(e);
}

Entity SandboxApp::spawnSpawnerUnit(Entity spawner, const SpawnerRequest& req)
{
    if (!m_chaseOk)
        return {};
    ListedSpawnHost host{};
    host.world     = &world();
    host.renderer  = &renderer();
    host.assets    = &assets();
    host.pins      = &pins();
    host.audio     = &audio();
    host.ground    = &m_ground;
    host.ai        = &m_chase.ai();
    host.crows     = &m_crowModels;
    host.terrain   = m_terrain.valid() ? &m_terrain : nullptr;
    host.play      = &m_play;
    host.bindPhysics = &SandboxApp::bindListedPhysics;
    host.noteHunter  = &SandboxApp::noteListedHunter;
    host.user        = this;
    return spawnListedUnit(host, spawner, req);
}

void SandboxApp::dropInventoryItem(int bagIndex)
{
    const Entity              body = possessedBody();
    InventoryComponent*       inv  = body.valid() ? world().get<InventoryComponent>(body) : nullptr;
    const TransformComponent* xf   = body.valid() ? world().get<TransformComponent>(body) : nullptr;
    if (!inv || !xf)
        return;
    const ItemStack item = takeItem(*inv, bagIndex);
    if (item.empty())
        return;

    Vector3f forward = m_viewCamera.GetLook();
    forward.y        = 0.0f;
    if (forward.MagnitudeSqrd() < 1.0e-6f)
        forward = Vector3f{ 0.0f, 0.0f, 1.0f };
    forward.Normalize();
    spawnItemPickup(world(), pins(), assets(), renderer(), m_physics, item, xf->position + forward + Vector3f{ 0.0f, 0.6f, 0.0f },
                    forward * 2.0f + Vector3f{ 0.0f, 2.0f, 0.0f });
    DE_LOG_INFO("Inventory: dropped {} x{}", item.def->name, item.count);
}




void SandboxApp::onWeaponHitThunk(void* user, const WeaponHit& hit)
{
    if (auto* app = static_cast<SandboxApp*>(user))
        app->m_play.onWeaponHit(hit);
}


void SandboxApp::syncPlayerMode()
{
    const Entity body = possessedBody();
    if (!body.valid() || !world().alive(body))
        return;
    if (!m_godMode && !m_reaperMode)
    {
        if (world().has<Combat::PlayerModeComponent>(body))
            world().remove<Combat::PlayerModeComponent>(body);
        return;
    }
    Combat::PlayerModeComponent* mode = world().get<Combat::PlayerModeComponent>(body);
    if (!mode)
        mode = &world().emplace<Combat::PlayerModeComponent>(body);
    mode->godMode    = m_godMode;
    mode->reaperMode = m_reaperMode;
}














ImpactFxHost SandboxApp::impactHost()
{
    ImpactFxHost host;
    host.world       = &world();
    host.scene       = &m_frame.scene();
    host.renderer    = &renderer();
    host.terrain     = &m_terrain;
    host.ground      = &m_ground;
    host.contactGrid = &m_terrain;
    host.targets     = &m_play.targets();
    return host;
}

bool SandboxApp::deferredDecals()
{
    return impactDecalsReady(impactHost());
}

bool SandboxApp::isStepCue(Entity e, const char* cue)
{
    return impactIsStepCue(impactHost(), e, cue);
}

void SandboxApp::spawnFootmark(const Vector3f& bodyPos, const Vector3f& facing)
{
    Dark::spawnFootmark(impactHost(), bodyPos, facing);
}

void SandboxApp::spawnHunterFootmark(Entity hunter)
{
    Dark::spawnHunterFootmark(impactHost(), hunter);
}

void SandboxApp::spawnWeaponImpact(const WeaponHit& hit)
{
    Dark::spawnWeaponImpact(impactHost(), hit);
}

void SandboxApp::spawnLivingBloodDecal(Entity victim, const Vector3f& point, const Vector3f& hitNormal)
{
    Dark::spawnLivingBloodDecal(impactHost(), victim, point, hitNormal);
}

void SandboxApp::spawnDeathBloodDecal(const Vector3f& point, const Vector3f& hitNormal)
{
    Dark::spawnDeathBloodDecal(impactHost(), point, hitNormal);
}








Entity SandboxApp::findPawn(ClientId owner)
{
    Entity found{};
    world().each<NetworkedComponent>([&](Entity e, NetworkedComponent& nc) {
        if (!found.valid() && nc.prefab == NetPrefab::PlayerPawn && nc.owner == owner)
            found = e;
    });
    return found;
}

void SandboxApp::spawnOwnedPawn(ClientId owner, float offsetX)
{
    if (findPawn(owner).valid())
        return;

    Vector3f pos{ offsetX, 0.0f, 0.0f };
    if (m_demo.cube().valid())
    {
        if (const TransformComponent* xf = world().get<TransformComponent>(m_demo.cube()))
        {
            pos.x = xf->position.x + offsetX;
            pos.z = xf->position.z;
        }
    }
    pos.y = m_terrain.heightAtWorld(pos.x, pos.z) + 0.5f;

    Entity e = world().createEntity();
    world().emplace<TagComponent>(e, "PlayerPawn");
    world().emplace<TransformComponent>(e, pos, Quaternion::IDENTITY, Vector3f{ 1, 1, 1 });
    attachSandboxCubeModel(world(), pins(), assets(), e, m_demo.cubeModelId());
    if (!network().registerEntity(world(), e, NetPrefab::PlayerPawn, owner, pawnPaletteColor(owner)))
    {
        onEntityRemoved(world(), e, &pins());
        world().destroyEntity(e);
        DE_LOG_ERROR(LogCategory::Networking, "Sandbox: failed to register pawn for client {}", static_cast<unsigned>(owner));
        return;
    }
    if (owner == network().localClientId())
        attachLocalPlayer(e);
    else
        attachReplicaCombat(e);
}

void SandboxApp::ensureLocalCube()
{
    if (m_demo.cube().valid() && world().alive(m_demo.cube()))
        return;

    const float groundY = m_terrain.heightAtWorld(0.0f, 0.0f) + 0.5f;
    m_demo.cube()              = world().createEntity();
    world().emplace<TagComponent>(m_demo.cube(), "Cube");
    world().emplace<TransformComponent>(m_demo.cube(), Vector3f{ 0.0f, groundY, 0.0f }, Quaternion::IDENTITY, Vector3f{ 1, 1, 1 });
    attachSandboxCubeModel(world(), pins(), assets(), m_demo.cube(), m_demo.cubeModelId());
    if (!network().registerEntity(world(), m_demo.cube(), NetPrefab::Cube))
    {
        onEntityRemoved(world(), m_demo.cube(), &pins());
        world().destroyEntity(m_demo.cube());
        m_demo.cube() = {};
        DE_LOG_ERROR(LogCategory::Networking, "Sandbox: failed to register local cube");
    }
}

bool SandboxApp::onNetSpawn(World& world, Entity e, NetPrefab prefab, const TransformComponent&, uint32_t, void* user)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app || !e.valid())
        return false;
    if (!world.has<ModelComponent>(e))
        attachSandboxCubeModel(world, app->pins(), app->assets(), e, app->m_demo.cubeModelId());
    if (prefab == NetPrefab::PlayerPawn)
    {
        const NetworkedComponent* nc = world.get<NetworkedComponent>(e);
        const bool local = nc && nc->owner == app->network().localClientId();
        if (local)
            app->attachLocalPlayer(e);
        else
            app->attachReplicaCombat(e);
    }
    return true;
}

void SandboxApp::onNetDespawn(World& world, Entity e, NetId, void* user)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    clearPlayerFlashlight(world, e);
    onEntityRemoved(world, e, &app->pins());
    if (app->m_demo.cube() == e)
        app->m_demo.cube() = {};
}

void SandboxApp::onNetPeer(const NetPeerInfo& info, NetPeerEvent event, void* user)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;

    if (event == NetPeerEvent::Joined && info.wantsPawn)
        app->spawnOwnedPawn(info.id, 2.0f * static_cast<float>(static_cast<uint8_t>(info.id)));
    else if (event == NetPeerEvent::Left)
    {
        const Entity pawn = app->findPawn(info.id);
        if (pawn.valid())
            app->network().unregisterEntity(app->world(), pawn);
    }
}

void SandboxApp::syncTerrainLod()
{
    const Entity body = possessedBody();
    const TransformComponent* anchor = body.valid() ? world().get<TransformComponent>(body) : nullptr;
    updatePinnedTerrainStream(m_terrain, m_viewCamera.GetPosition(), renderer(), m_terrainMaterial, !body.valid(), anchor);
    if (m_terrain.heightTexture().valid())
    {
        renderer().setHeightSrv(m_terrain.heightTexture().cpuHandle());
        m_waterPipeline.setHeightSrv(renderer().device(), m_terrain.heightTexture().cpuHandle());
    }
    if (!flushDirtyWater(m_water, renderer()))
        DE_LOG_ERROR("SandboxApp: water upload failed");
}

void SandboxApp::createPhysicsWorld()
{
    Physics::PhysicsWorldDesc desc;
    desc.enabled = true;
    desc.gravity = 24.0f;
    if (!m_physics.create(desc))
    {
        DE_LOG_ERROR(LogCategory::Collision, "SandboxApp: physics world create failed");
        return;
    }

    const HeightMap& coarse = m_terrain.coarse();
    if (!coarse.valid())
    {
        DE_LOG_WARN(LogCategory::Collision, "SandboxApp: no terrain height map; physics has no ground");
        return;
    }

    m_physicsGround = bindHeightFieldGround(m_physics, coarse, "SandboxApp");
    if (m_physicsGround == Physics::kNullPhysicsBody)
        DE_LOG_WARN(LogCategory::Collision, "SandboxApp: terrain height field failed");
}

void SandboxApp::addChaseObstaclesToPhysics()
{
    if (!m_physics.valid())
        return;

    for (const AABox3f& box : m_chase.cubes())
    {
        Physics::PhysicsBoxDesc desc;
        desc.position    = box.Center();
        desc.halfExtents = box.Extents();
        desc.dynamic     = false;
        desc.friction    = 0.6f;
        if (m_physics.createBox(desc) == Physics::kNullPhysicsBody)
            DE_LOG_WARN(LogCategory::Collision, "SandboxApp: chase obstacle body failed");
    }
}

void SandboxApp::bindEntityPhysics(Entity e, const char* entityType)
{
    if (!m_physics.valid() || !e.valid() || !world().alive(e))
        return;

    const EntityMaster master = loadEntityMasterType(entityType);
    if (master.physics.empty())
    {
        DE_LOG_WARN(LogCategory::Collision, "SandboxApp: '{}' has no physics card", entityType);
        return;
    }

    const std::filesystem::path file = resolveContentFile(master.physics);
    PhysicsComponent settings;
    if (file.empty() || !Physics::loadPhysicsSettingsFile(file, settings))
    {
        DE_LOG_WARN(LogCategory::Collision, "SandboxApp: physics card '{}' for '{}' missing or invalid", master.physics, entityType);
        return;
    }
    world().emplace<PhysicsComponent>(e, std::move(settings));

    const Model* model = nullptr;
    if (const ModelComponent* mc = world().get<ModelComponent>(e))
        model = assets().find<Model>(mc->modelAssetID);
    if (!Physics::bindPhysicsEntity(m_physics, world(), e, model))
        DE_LOG_WARN(LogCategory::Collision, "SandboxApp: physics bind failed for '{}' #{}", entityType, e.id());
}

void SandboxApp::bindCharacterPhysics()
{
    bindEntityPhysics(m_chase.walker(), "player");
    for (int i = 0; i < m_chase.hunterCount(); ++i)
    {
        const Entity e = m_chase.hunterEntity(i);
        bindEntityPhysics(e, isInteractGuard(world(), e) ? "human" : "wolf");
    }
}

float SandboxApp::playerGroundHeight(float x, float z) const
{
    return raisedGroundY(m_physics, m_groundIgnore, m_physicsGround, m_terrain.heightAtWorld(x, z), x, z, m_play.runtime().groundProbeY);
}

Terrain::GroundContact SandboxApp::groundContactAt(float x, float z) const
{
    return Terrain::groundContactAt(m_ground, &m_terrain, x, z);
}

void SandboxApp::onInit()
{
    DE_LOG_INFO("SandboxApp: init");
    loadEchoChirpSettings(m_vision.echoChirp);

    m_ground.loadFromContent();
    skillCatalog().loadFromContent();
    throwableCatalog().loadFromContent();
    m_chase.setGround(&m_ground);
    registerDefaultActions();
    audio().setMasterVolume(0.85f);

    if (!renderer().enableSceneBuffers(config().scenePath))
        DE_LOG_ERROR(LogCategory::Render, "SandboxApp: SceneBuffers enable failed; SwapChainForward");
    if (renderer().scenePath() == ScenePath::HybridDeferred)
        renderer().debugState().decalsEnabled = true;

    {
        SceneRendererDesc sceneDesc{};
        sceneDesc.createWorldEnvironment = true;
        sceneDesc.createAutoExposure     = renderer().scenePath() == ScenePath::HybridDeferred;
        sceneDesc.logTag = "SandboxApp";
        if (!m_frame.scene().init(renderer(), sceneDesc))
        {
            requestQuit();
            return;
        }
        m_foliagePipeline.create(renderer().device());
        if (!m_frame.drawList().create(renderer().device()))
            DE_LOG_ERROR(LogCategory::Render, "SandboxApp: instanced mesh pipeline failed; static draws stay immediate");
        if (!m_frame.skinnedInstances().create(renderer().device()))
            DE_LOG_ERROR(LogCategory::Render, "SandboxApp: skinned instancing unavailable; skinned draws stay immediate");
        if (!m_grassPipeline.create(renderer()))
        {
            DE_LOG_ERROR(LogCategory::Render, "SandboxApp: grass pipeline create failed");
            GrassParams grassOff = m_grassField.params();
            grassOff.enabled     = false;
            m_grassField.setParams(grassOff);
        }
        m_foliagePrototypes.create(renderer(), assets());
        if (!m_demo.skeletonPipeline().create(renderer().device(), renderer().sceneColorFormat(), false))
            DE_LOG_ERROR(LogCategory::Render, "SandboxApp: skeleton LinePipeline create failed");
        else if (!m_demo.createSkeletonLineBuffers())
            DE_LOG_ERROR(LogCategory::Render, "SandboxApp: skeleton line buffers failed");
    }

    if (!m_healthHud.create(renderer()))
        DE_LOG_ERROR("SandboxApp: health HUD failed");
    if (!m_crosshair.create(renderer()))
        DE_LOG_ERROR("SandboxApp: crosshair HUD failed");
    if (!m_particles.create(renderer(), assets()))
        DE_LOG_ERROR("SandboxApp: particle renderer failed");
    if (!m_gpuParticles.create(renderer()))
        DE_LOG_ERROR(LogCategory::Render, "SandboxApp: GPU particle system failed");
    if (!m_leafRenderer.create(renderer(), assets()))
        DE_LOG_ERROR(LogCategory::Render, "SandboxApp: leaf renderer failed");
    if (!m_bloodSplats.create(renderer()))
        DE_LOG_ERROR("SandboxApp: blood splat pool failed");
    m_blood.setDesc(playBloodBurstDesc());
    m_blood.stop(true);
    if (!pumpBootFrame())
        return;
    if (!pumpBootFrame())
        return;
    if (!pumpBootFrame())
        return;
    if (!pumpBootFrame())
        return;
    if (!pumpBootFrame())
        return;
    populateMainMenu();
    if (!m_hud.m_menu.create(renderer(), UiAccent::Sandbox))
        DE_LOG_ERROR(LogCategory::Render, "SandboxApp: main menu GPU init failed");
    else if (shouldShowMainMenu(config()))
    {
        m_hud.m_menu.show();
        DE_LOG_INFO("Sandbox: main menu ({} entries), jump-to-level {}", m_hud.m_menu.entries().size(), shouldShowDebugLevelJump(config()) ? "on" : "off");
    }
    if (!m_hud.m_menu.loadSounds(audio(), assets()))
        DE_LOG_WARN(LogCategory::Audio, "SandboxApp: main menu sounds failed");

    if (!m_hud.m_imgui.init(window(), renderer(), "sandbox_imgui.ini", false, UiAccent::Sandbox, false))
        DE_LOG_WARN("SandboxApp: ImGui init failed — Dev Tools (M) disabled");
    else
        m_hud.loadMenuBackground();
    if (!pumpBootFrame())
        return;

    // Menu keys are polled in the main loop, after onInit returns. Load now only
    // when the picker will not be shown (-no-menu, net host, or menu GPU failed).
    prepareBoot(!m_hud.m_menu.visible(), defaultScenePath(sceneFileArg()));
    if (!bootSceneLoop())
        return;
}

void SandboxApp::onSplashFinished()
{
    m_play.runtime().spawnAge = 0.0f;
    playMusicCue(world(), audio(), assets(), m_camera, "music");
}

void SandboxApp::tickLeaves(float dt)
{
    const AssetRef<Model>& treeModel = m_foliagePrototypes.model(Terrain::FoliageKind::Tree);
    FallingLeafTick        tick;
    tick.field      = &m_leafField;
    tick.rustle     = &m_leafRustle;
    tick.terrain    = &m_terrain;
    tick.foliage    = m_foliage.empty() ? nullptr : &m_foliage;
    tick.treeModel  = treeModel ? treeModel.get() : nullptr;
    tick.audio      = &audio();
    tick.assets     = &assets();
    tick.camera     = &m_viewCamera;
    tick.simSeconds = &m_leafSimSeconds;
    tick.shedScale  = m_leafShedScale;
    tick.lake       = m_water.chunksX() > 0;
    tick.lakeLevel  = m_water.params().waterLevel;
    stepFallingLeaves(tick, dt);
}

void SandboxApp::onUpdate(float dt)
{
    m_movie.tick(dt);
    tickMovieCues(world(), m_movie, possessedBody());
    if (m_movie.blocksWorld() && !m_movieWasBlocking)
    {
        audio().stopMusic();
        audio().stopClipVoices();
    }
    m_movieWasBlocking = m_movie.blocksWorld();

    m_frameDt = dt;
    const bool menuFrame = m_hud.m_menu.visible();
    if (!menuFrame)
        dt = m_play.scaleDt(dt);
    handleRuntimeCommands(dt);
    if (m_movie.blocksWorld() && !m_movieWasBlocking)
    {
        audio().stopMusic();
        audio().stopClipVoices();
    }
    m_movieWasBlocking = m_movie.blocksWorld();
    if (menuFrame || m_hud.m_menu.visible())
    {
        m_leafRustle.stopAll(audio());
        m_save.service(world());
        return;
    }
    if (!m_sceneStarted)
    {
        m_leafRustle.stopAll(audio());
        return;
    }
    if (m_dialogue.locksWorld() || m_movie.blocksWorld())
    {
        // This conversation waits. Pawns, barks, and the fly cam stay where they are.
    }
    else if (!m_gameplayPaused || m_stepGameplay)
    {
        syncPlayerMode();
        m_cloudTime += dt;
        m_env.tick(dt);
        m_water.tick(dt);
        bindPlayFeedback();
        tickPlayMuzzle(m_playFeedback, dt);
        m_play.tickWorld(dt);
        updateQuest(dt);
        if (m_hud.m_defenseTextLeft > 0.0f)
        {
            m_hud.m_defenseTextLeft -= dt;
            if (m_hud.m_defenseTextLeft < 0.0f)
                m_hud.m_defenseTextLeft = 0.0f;
        }
        m_blood.update(dt);
        m_gpuParticles.applyIntegratorFallback(world(), renderer().debugState().gpuParticles);
        tickParticleEmitters(world(), dt);
        m_gpuParticles.prepare(world(), dt);
        m_demo.updateWiggleAnim();
        tickAnimGraphs(world(), assets(), dt);
        m_play.tickHeldItems();
        tickLeaves(dt);
        m_stepGameplay = false;
    }
    else
    {
        m_leafRustle.stopAll(audio());
    }
    if (!m_movie.blocksWorld())
        m_dialogue.tick(dt);
    m_save.service(world());
    m_water.updateLod(m_viewCamera.GetPosition());
    syncTerrainLod();
    // Shared with foliage. Trees sway with no possessed body, and while gameplay is paused.
    m_windClock.tick(dt);
    m_grassWindSeconds = m_windClock.time;
    if (const Entity body = possessedBody(); body.valid())
    {
        float             x = 0.0f;
        float             z = 0.0f;
        GrassPlayerSample sample;
        if (fillGrassPlayerSample(world(), body, x, z, sample))
            m_grassField.update(m_terrain, x, z, m_grassWindSeconds, m_water.params().waterLevel, sample);
    }
    if (!m_movie.blocksWorld() && renderer().scenePath() == ScenePath::HybridDeferred)
        syncPlayerFlashlight(world(), possessedBody(), m_viewCamera.GetPosition(), m_viewCamera.GetLook(), m_viewCamera.GetRight(), m_viewCamera.GetUp(), m_play.runtime().offhand.lightOn);

    setViewAudioListener(audio(), m_viewCamera, &world(), possessedBody(), skillCatalog().enabled());
    if (!m_movie.blocksWorld())
        tickSoundEmitters(world(), audio(), assets());
    m_frame.scene().tickDecals(world(), dt);
}







void SandboxApp::onRender()
{
    m_frame.render();
}

void SandboxApp::onShutdown()
{
    // Join MovieDecode before the device and stopAll. LoadingScreen closes later.
    m_movie.close();
    jobs().shutdown();
    resetMovieCues(world());
    m_guardVoice.stop();
    network().shutdown();
    renderer().waitForGpu();
    m_foliagePipeline.destroy();
    m_grassPipeline.destroy();
    m_hud.m_menu.shutdown(renderer());
    m_frame.scene().shutdown();
    m_hud.m_imgui.shutdown(renderer());
    m_hud.m_menuBackground   = Dark::Texture2D{};
    m_hud.m_menuBackgroundId = 0;
    m_water = WaterWorld{};
    m_terrainMaterial = TerrainMaterial{};
    Physics::destroyFoliageCollision(m_physics, m_foliageCollision);
    m_physics.destroy();
    m_physicsGround = Physics::kNullPhysicsBody;
    m_terrain.clear();
    audio().stopAll();
    if (WeaponLoadout* w = localWeapons())
    {
        w->projectile().setAudio(nullptr, nullptr);
        w->clear();
    }
    m_gpuParticles.destroy(renderer());
    m_particles.destroy(renderer());
    m_leafRenderer.destroy(renderer());
    m_bloodSplats.destroy(renderer());
    DE_LOG_INFO("SandboxApp: shutdown");
}
