#include "SandboxApp.h"

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
#include "Scene/SceneSpawn.h"
#include "Gameplay/PowerCheck.h"
#include "Play/PhysicsGround.h"
#include "Play/PlayLook.h"
#include "Play/PlayMotor.h"
#include "Play/ShoulderCamera.h"
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

const char* netRoleName(NetRole role)
{
    switch (role)
    {
    case NetRole::Idle:
        return "Idle";
    case NetRole::Joining:
        return "Joining";
    case NetRole::Host:
        return "Host";
    case NetRole::Client:
        return "Client";
    default:
        return "?";
    }
}

uint32_t pawnPaletteColor(ClientId id)
{
    static constexpr uint32_t kPalette[8] = {
        0x3DA6F2FFu, 0xE85D4CFFu, 0x5BD96CFFu, 0xF2C14EFFu, 0xC86BFFFFu, 0xF28C3CFFu, 0x4CD4E8FFu, 0xE8E8E8FFu,
    };
    const unsigned i = static_cast<unsigned>(id);
    return kPalette[i < 8u ? i : 0u];
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
    , m_offhand(m_play.runtime().offhand)
    , m_chargeSettings(m_play.runtime().chargeSettings)
    , m_attackCharge(m_play.runtime().attackCharge)
    , m_blockCharge(m_play.runtime().blockCharge)
    , m_chargeWindup(m_play.runtime().chargeWindup)
    , m_fireQuick(m_play.runtime().fireQuick)
    , m_fireCharged(m_play.runtime().fireCharged)
    , m_throwFlights(m_play.runtime().throws)
    , m_hitStop(m_play.runtime().hitStop)
    , m_shake(m_play.runtime().shake)
    , m_shakeTime(m_play.runtime().shakeTime)
    , m_swingCharged(m_play.runtime().swingCharged)
    , m_swingLock(m_play.runtime().swingLock)
    , m_comboWindow(m_play.runtime().comboWindow)
    , m_comboStep(m_play.runtime().comboStep)
    , m_comboCount(m_play.runtime().comboCount)
    , m_faceYaw(m_play.runtime().faceYaw)
    , m_faceLeft(m_play.runtime().faceLeft)
    , m_blockRecoil(m_play.runtime().blockRecoil)
    , m_quest(m_play.quest())
    , m_shieldBump(m_play.runtime().shieldBump)
    , m_weaponTargets(m_play.targets())
    , m_groundProbeY(m_play.runtime().groundProbeY)
    , m_lookYaw(m_play.runtime().lookYaw)
    , m_lookPitch(m_play.runtime().lookPitch)
    , m_lowerBodyYaw(m_play.runtime().lowerBodyYaw)
    , m_playerWet(m_play.runtime().wet)
    , m_playerSubmerged(m_play.runtime().submerged)
    , m_swimDiveEdge(m_play.runtime().swimDiveEdge)
    , m_swimStandEdge(m_play.runtime().swimStandEdge)
    , m_underAmount(m_play.runtime().underAmount)
    , m_footstepAcc(m_play.runtime().footstepAcc)
    , m_playerSpawn(m_play.runtime().spawn)
    , m_havePlayerSpawn(m_play.runtime().haveSpawn)
    , m_playerDeadTimer(m_play.runtime().deadTimer)
    , m_spawnAge(m_play.runtime().spawnAge)
    , m_hurtSoundTimer(m_play.runtime().hurtTimer)
    , m_jumpAttackBuffer(m_play.runtime().jumpAttackBuffer)
    , m_crouchLatch(m_play.runtime().crouchLatch)
{
    m_play.bind(&SandboxApp::fillPlayHost, this);
}

void SandboxApp::fillPlayHost(void* user, PlayHost& host)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    host.world     = &app->world();
    host.renderer  = &app->renderer();
    host.scene     = &app->m_scene;
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

    host.groundProbeY = &app->m_groundProbeY;
    host.groundIgnore = &app->m_groundIgnore;
    host.camouflage   = app->m_camouflage;

    const bool ui    = app->uiOpen();
    const bool imgui = app->m_imgui.isReady();
    const bool dialogueUi = app->m_dialogue.choiceOpen() || app->m_dialogue.locksWorld();
    host.uiKeys            = (ui && imgui && app->m_imgui.wantCaptureKeyboard()) || dialogueUi;
    host.applyMouseLook    = !ui && !dialogueUi;
    host.allowMouseButtons = !ui && !dialogueUi;
    host.uiMouse           = ui && imgui && app->m_imgui.wantCaptureMouse();
    host.chaseReady        = app->m_chaseOk;

    const std::vector<AABox3f>& cubes = app->m_chase.cubes();
    host.cubes      = cubes.empty() ? nullptr : cubes.data();
    host.cubeCount  = static_cast<int>(cubes.size());
    host.spheres    = nullptr;
    host.sphereCount = 0;

    host.stepPhysics   = &SandboxApp::stepPlayPhysics;
    host.onMuzzle      = &SandboxApp::onPlayMuzzle;
    host.onBloodBurst  = &SandboxApp::onPlayBloodBurst;
    host.onBloodSplat  = &SandboxApp::onPlayBloodSplat;
    host.onRespawn     = &SandboxApp::onPlayRespawn;
    host.onQuestNote   = &SandboxApp::onPlayQuestNote;
    host.onDefenseText = &SandboxApp::onPlayDefenseText;
    host.onLoot        = &SandboxApp::onPlayLoot;
    host.spawnUnit     = &SandboxApp::onPlaySpawnUnit;
    host.heightAt      = &SandboxApp::onPlayHeight;
    host.groundContact = &SandboxApp::onPlayGroundContact;
    host.user          = app;
}

void SandboxApp::stepPlayPhysics(void* user, float dt)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app || !app->m_physics.valid())
        return;
    app->m_physics.pushPoses(app->world(), false);
    app->m_physics.step(dt, app->world());
    app->m_physics.writeDynamicPoses(app->world());
}

void SandboxApp::onPlayMuzzle(void* user)
{
    if (auto* app = static_cast<SandboxApp*>(user))
        app->pulseMuzzle();
}

void SandboxApp::onPlayBloodBurst(void* user, const Vector3f& pos)
{
    if (auto* app = static_cast<SandboxApp*>(user))
        app->spawnHunterBlood(pos);
}

void SandboxApp::onPlayBloodSplat(void* user, float x, float z)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    app->m_bloodSplats.spawn(x, z, app->m_terrain.coarse());
}

void SandboxApp::onPlayRespawn(void* user)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    app->m_nightSightOn  = false;
    app->m_nightSight    = 0.0f;
    app->m_infraredOn    = false;
    app->m_infrared      = 0.0f;
    app->m_ultravioletOn = false;
    app->m_ultraviolet   = 0.0f;
    app->m_echoOn        = false;
    app->m_echo          = 0.0f;
    app->m_echoHaveChirp = false;
    app->m_echoCapture   = false;
}

void SandboxApp::onPlayQuestNote(void* user, const QuestNoteEvent& note)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    const auto color = [](uint32_t r, uint32_t g, uint32_t b) { return 0xFF000000u | (b << 16) | (g << 8) | r; };
    QuestBanner banner;
    banner.text = note.text;
    switch (note.kind)
    {
    case QuestNote::Started:
        banner.label = "NEW QUEST";
        banner.color = color(240, 205, 110);
        break;
    case QuestNote::NewObjective:
        banner.label = "NEW OBJECTIVE";
        banner.color = color(235, 235, 235);
        break;
    case QuestNote::ObjectiveDone:
        banner.label = "OBJECTIVE COMPLETE";
        banner.color = color(120, 230, 140);
        break;
    case QuestNote::QuestDone:
        banner.label = "QUEST COMPLETE";
        banner.color = color(240, 205, 110);
        break;
    }
    if (note.kind != QuestNote::NewObjective || app->m_questBanners.size() < 4)
        app->m_questBanners.push_back(std::move(banner));
}

void SandboxApp::onPlayDefenseText(void* user, bool parried)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    app->m_defenseText     = parried ? "PARRY!" : "BLOCKED";
    app->m_defenseTextLeft = 0.7f;
}

void SandboxApp::onPlayLoot(void* user, Entity loot)
{
    if (auto* app = static_cast<SandboxApp*>(user))
        app->m_lootTarget = loot;
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

void SandboxApp::spawnBuildingExtras()
{
    BuildingFixtureSpawn spawn;
    spawn.world    = &world();
    spawn.renderer = &renderer();
    spawn.assets   = &assets();
    spawn.pins     = &pins();
    spawn.ai       = &m_chase.ai();
    spawn.labels   = &m_buildingLabels;
    spawnBuildingFixtures(spawn, m_buildingContainers, m_buildingHides, m_buildingDoors, m_buildingCameras);
}

void SandboxApp::startQuest(const std::string& id)
{
    m_play.startQuest(id);
}

void SandboxApp::updateQuest(float dt)
{
    if (m_questBanners.empty())
        return;
    m_questBanners.front().timeLeft -= dt;
    if (m_questBanners.front().timeLeft <= 0.0f)
        m_questBanners.erase(m_questBanners.begin());
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
        "Y/R3 walk the wiggle demo  T talk  F2 lighting  F5 quicksave  F9 quickload  M dev tools  P profiler  -forward linear albedo + sRGB encode (legacyUnormAlbedo restores old sampling)  -no-menu skip scene picker");
}

void SandboxApp::populateMainMenu()
{
    m_menu.clearEntries();
    m_menu.setTitle(config().hostName ? config().hostName : "Sandbox");
    m_menu.setAccent(UiAccent::Sandbox);
    // Sandbox is the 3D host. level2d.json stays on the Sandbox2D menu.
    const auto scenes = listSceneFiles(SceneMode::Scene3D);
    if (scenes.empty())
        m_menu.addBuiltIn("sandbox", "Play", "Default sandbox");
    else
    {
        for (const SceneFileInfo& s : scenes)
            m_menu.addScene(s.fileName, s.displayName, s.path, s.fileName);
    }
    m_menu.addQuit();
    if (!m_menu.selectById(sceneFileArg()))
        m_menu.selectById(std::filesystem::path(sceneFileArg()).filename().string());
}

void SandboxApp::handleRuntimeCommands(float dt)
{
    if (m_menu.visible())
    {
        window().setCursorCaptured(false);
        m_menu.update(input(), renderer().width(), renderer().height());
        switch (m_menu.pollResult())
        {
        case MainMenuResult::Confirm:
            // The 3D world is built once. Esc brings this menu back as a pause;
            // confirming a different row does not tear the level down.
            if (!m_sceneStarted)
            {
                std::filesystem::path path = defaultScenePath(sceneFileArg());
                if (const MainMenuEntry* e = m_menu.selected())
                {
                    if (e->kind == MainMenuKind::Scene && !e->path.empty())
                        path = e->path;
                    else if (e->kind == MainMenuKind::BuiltIn)
                        path = defaultScenePath("level.json");
                }
                if (!startScene(path))
                {
                    m_menu.hide();
                    return;
                }
                // Splash already finished without a camera, so the usual cue never played.
                playMusicCue(world(), audio(), assets(), m_camera, "music");
            }
            else if (const MainMenuEntry* picked = m_menu.selected())
            {
                const bool otherScene = picked->kind == MainMenuKind::Scene && !picked->path.empty()
                    && picked->path.filename() != m_loadedScenePath.filename();
                if (otherScene)
                    DE_LOG_INFO("Sandbox: already in '{}'; staying in this scene", m_loadedScenePath.filename().string());
            }
            m_menu.hide();
            break;
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

    const bool uiKeys = uiOpen() && m_imgui.isReady() && m_imgui.wantCaptureKeyboard();
    // Playing swaps after the charge step. Paused play never reaches that step, so it still swaps here.
    if (!uiKeys && m_gameplayPaused)
        m_play.switchWeapons();
    // Dev tools and inventory become the keyboard-nav target, so WantCaptureKeyboard
    // is set and would swallow the same key that should close them. A text field still owns the key.
    const bool typing = m_imgui.isReady() && m_imgui.wantTextInput();
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
    if (input().actionPressed("journal") && !typing && (!uiKeys || m_showJournal))
    {
        m_showJournal = !m_showJournal;
        playSoundCue(world(), audio(), assets(), m_camera, "click");
    }
    if (input().actionPressed("upgrades") && !typing && (!uiKeys || m_showUpgrades))
    {
        m_showUpgrades = !m_showUpgrades;
        playSoundCue(world(), audio(), assets(), m_camera, "click");
    }
    if (!uiKeys && input().actionPressed("camouflage"))
    {
        m_camouflage = !m_camouflage;
        DE_LOG_INFO("Sandbox: camouflage = {}", m_camouflage);
    }
    {
        Health* nightHp = localHealth();
        const bool nightAlive = nightHp && nightHp->alive();
        if (!uiKeys && nightAlive && input().actionPressed("night_sight"))
        {
            m_nightSightOn = !m_nightSightOn;
            DE_LOG_INFO("Sandbox: night sight = {}", m_nightSightOn);
        }
        if (!uiKeys && nightAlive && input().actionPressed("infrared"))
        {
            m_infraredOn = !m_infraredOn;
            DE_LOG_INFO("Sandbox: infrared = {}", m_infraredOn);
            if (m_infraredOn)
            {
                m_ultravioletOn = false;
                m_ultraviolet   = 0.0f;
                m_echoOn        = false;
                m_echo          = 0.0f;
                m_echoHaveChirp = false;
                m_echoCapture   = false;
            }
        }
        if (!uiKeys && nightAlive && input().actionPressed("ultraviolet"))
        {
            m_ultravioletOn = !m_ultravioletOn;
            DE_LOG_INFO("Sandbox: ultraviolet = {}", m_ultravioletOn);
            if (m_ultravioletOn)
            {
                m_infraredOn    = false;
                m_infrared      = 0.0f;
                m_echoOn        = false;
                m_echo          = 0.0f;
                m_echoHaveChirp = false;
                m_echoCapture   = false;
            }
        }
        if (!uiKeys && nightAlive && input().actionPressed("echolocation"))
        {
            m_echoOn = !m_echoOn;
            DE_LOG_INFO("Sandbox: echolocation = {}", m_echoOn);
            if (m_echoOn)
            {
                m_infraredOn    = false;
                m_infrared      = 0.0f;
                m_ultravioletOn = false;
                m_ultraviolet   = 0.0f;
                m_echoHaveChirp = false;
                m_echoCapture   = true;
                m_echoChirpAge  = 0.0f;
            }
        }
        if (!nightAlive)
        {
            m_nightSightOn  = false;
            m_nightSight    = 0.0f;
            m_infraredOn    = false;
            m_infrared      = 0.0f;
            m_ultravioletOn = false;
            m_ultraviolet   = 0.0f;
            m_echoOn        = false;
            m_echo          = 0.0f;
            m_echoHaveChirp = false;
            m_echoCapture   = false;
        }
        else
        {
            const float target = m_nightSightOn ? 1.0f : 0.0f;
            const float step   = dt > 0.0f ? dt / kNightSightBlendSeconds : 0.0f;
            if (m_nightSight < target)
                m_nightSight = Math::Min(target, m_nightSight + step);
            else
                m_nightSight = Math::Max(target, m_nightSight - step);

            const float heatTarget = m_infraredOn ? 1.0f : 0.0f;
            const float heatStep   = dt > 0.0f ? dt / kInfraredBlendSeconds : 0.0f;
            if (m_infrared < heatTarget)
                m_infrared = Math::Min(heatTarget, m_infrared + heatStep);
            else
                m_infrared = Math::Max(heatTarget, m_infrared - heatStep);

            const float uvTarget = m_ultravioletOn ? 1.0f : 0.0f;
            const float uvStep   = dt > 0.0f ? dt / kUltravioletBlendSeconds : 0.0f;
            if (m_ultraviolet < uvTarget)
                m_ultraviolet = Math::Min(uvTarget, m_ultraviolet + uvStep);
            else
                m_ultraviolet = Math::Max(uvTarget, m_ultraviolet - uvStep);

            const float echoTarget = m_echoOn ? 1.0f : 0.0f;
            const float echoStep   = dt > 0.0f ? dt / kEchoBlendSeconds : 0.0f;
            if (m_echo < echoTarget)
                m_echo = Math::Min(echoTarget, m_echo + echoStep);
            else
                m_echo = Math::Max(echoTarget, m_echo - echoStep);

            if (m_echoOn || m_echo > 0.0f)
            {
                m_echoChirpAge += dt;
                if (!m_echoHaveChirp || m_echoChirpAge >= m_echoChirp.periodSeconds)
                {
                    m_echoCapture  = true;
                    m_echoChirpAge = 0.0f;
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
        if (m_dialogue.active())
        {
            m_dialogue.interrupt(DialogueInterrupt::Cancel);
            return;
        }
        if (config().showMainMenu && m_menu.isReady() && network().role() == NetRole::Idle)
        {
            m_dialogue.interrupt(DialogueInterrupt::Menu);
            m_menu.show();
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

    if (input().actionPressed("pause"))
    {
        const bool dialogueWasOpen = m_dialogue.active();
        if (!uiKeys || dialogueWasOpen)
        {
            if (dialogueWasOpen)
                m_dialogue.interrupt(DialogueInterrupt::Pause);
            const bool otherUi = m_showDevTools || m_showInventory || m_showProfiler || m_showJournal || m_lootTarget.valid();
            if (!uiKeys || !otherUi)
            {
                m_gameplayPaused = !m_gameplayPaused;
                m_stepGameplay   = false;
                playSoundCue(world(), audio(), assets(), m_camera, "click");
                DE_LOG_INFO("Sandbox: gameplay paused = {}", m_gameplayPaused);
            }
        }
    }
    if (!uiKeys && m_gameplayPaused && input().actionPressed("step"))
        m_stepGameplay = true;

    if (!uiKeys && input().actionPressed("reset"))
    {
        m_spinSpeed  = 0.8f;
        Vector3f pos{};
        if (m_cube.valid())
        {
            if (auto* xf = world().get<TransformComponent>(m_cube))
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

    if (!typing && input().actionPressed("talk"))
    {
        if (m_dialogue.active())
            m_dialogue.requestSkip();
        else if (!uiKeys)
            tryBeginTalk();
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

    window().setCursorCaptured(window().isFocused() && !uiOpen() && !m_menu.visible());
    if (!m_dialogue.locksWorld())
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

    if (auto* xf = m_cube.valid() ? world().get<TransformComponent>(m_cube) : nullptr)
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
    const bool uiKeys  = uiOpen() && m_imgui.isReady() && m_imgui.wantCaptureKeyboard();
    const bool uiMouse = uiOpen() && m_imgui.isReady() && m_imgui.wantCaptureMouse();

    if (!uiKeys)
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
        m_cube = {};
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
    if (!m_camouflage || !e.valid())
        return false;
    const Entity body = possessedBody();
    return body.valid() && e.id() == body.id();
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
    return m_quest.objectiveTarget(world(), possessedBody(), out);
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
        const float t    = Clamp(m_playerDeadTimer / kPlayDeathSeconds, 0.0f, 1.0f);
        s.blur           = SmoothStep(0.0f, 0.55f, t);
        s.uniformBlur    = SmoothStep(0.0f, 0.70f, t);
        s.fade           = SmoothStep(0.30f, 1.00f, t);
        s.nightSight     = 0.0f;
        return s;
    }

    const float u    = SmoothStep(0.0f, kPlaySpawnFocusSeconds, m_spawnAge);
    s.blur           = 1.0f - u;
    s.uniformBlur    = 1.0f - u;
    s.fade           = 0.0f;
    s.nightSight     = (m_infrared > 0.0f || m_ultraviolet > 0.0f || m_echo > 0.0f) ? 0.0f : m_nightSight;
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

Entity SandboxApp::spawnSpawnerUnit(Entity spawner, const SpawnerRequest& req)
{
    if (!m_chaseOk)
        return {};
    if (req.unit == SpawnerUnit::Crow)
    {
        const TransformComponent* home = world().get<TransformComponent>(spawner);
        const float homeX = home ? home->position.x : req.x;
        const float homeZ = home ? home->position.z : req.z;
        const float groundY = m_terrain.valid() ? m_terrain.heightAtWorld(req.x, req.z) : 0.0f;
        return Dark::spawnCrow(world(), renderer(), assets(), pins(), m_crowModels, m_terrain.valid() ? &m_terrain : nullptr, spawner, req, groundY, homeX, homeZ);
    }
    const bool wolf = req.unit == SpawnerUnit::Wolf;
    TransformComponent xf{};
    xf.position = Vector3f{ req.x, m_terrain.heightAtWorld(req.x, req.z) + 0.5f, req.z };
    xf.scale    = Vector3f{ 1.0f, 1.0f, 1.0f };
    const Entity hunter = m_chase.spawnListedHunter(world(), pins(), assets(), xf);
    if (!hunter.valid())
    {
        DE_LOG_WARN(LogCategory::AI, "Sandbox: spawner failed to create a {}", wolf ? "wolf" : "hunter");
        return {};
    }
    const CharacterHost host{ world(), renderer(), assets(), pins(), audio(), m_ground };
    attachNpcVisual(host, hunter, wolf ? NpcKind::Wolf : NpcKind::Hunter, ModelAttach::AnimatedOnly);
    bindEntityPhysics(hunter, wolf ? "wolf" : "human");
    if (!wolf)
    {
        stockHunter(hunter);
        bindNpcDialogue(hunter, {});
    }
    SpawnedUnitComponent mark{};
    mark.spawner = spawner;
    mark.slot    = req.slot;
    world().emplace<SpawnedUnitComponent>(hunter, mark);
    return hunter;
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






void SandboxApp::pulseMuzzle()
{
    if (!m_muzzle.valid())
        return;
    if (TransformComponent* xf = world().get<TransformComponent>(m_muzzle))
        xf->position = m_viewCamera.GetPosition() + m_viewCamera.GetLook() * 0.8f;
    if (LocalLightComponent* light = world().get<LocalLightComponent>(m_muzzle))
    {
        light->enabled   = true;
        light->intensity = 37699.0f; // 12000*π after Fd/π
        light->range     = 6.0f;
    }
    m_muzzleTimer = 0.05f;
}

bool SandboxApp::createSandboxModels()
{
    MeshData cubeData;
    if (!CreateCube(cubeData, 1.0f))
        return false;
    auto cubeMat = std::make_shared<Material>();
    if (!cubeMat->createFromAlbedoPath(assets(), "textures/dark_engine_cube.png", /*fallback*/ 64, 166, 242, 255))
        return false;
    cubeMat = assets().internMaterial(cubeMat, "runtime:/sandbox/cube-mat");
    AssetRef<Model> cubeModel = internAndUploadProceduralModel(renderer(), assets(), std::move(cubeData), cubeMat, "runtime:/sandbox/cube");
    if (!cubeModel)
        return false;
    m_cubeModelId = cubeModel->id;

    MeshData packData;
    if (!CreateCross(packData, 1.0f, 0.30f, 0.22f))
        return false;
    AssetRef<Material> packMat = internSolidMaterial(assets(), 214, 28, 36, 255, "runtime:/sandbox/pack-mat");
    AssetRef<Model> packModel  = internAndUploadProceduralModel(renderer(), assets(), std::move(packData), packMat, "runtime:/sandbox/pack");
    if (!packModel)
        return false;
    m_packModelId = packModel->id;

    MeshData lanternData;
    if (!CreateCube(lanternData, 1.0f))
        return false;
    auto lanternMat = std::make_shared<Material>();
    if (!lanternMat->createSolid(assets(), 220, 150, 60, 255))
        return false;
    lanternMat->setEmissive(1.0f);
    lanternMat = assets().internMaterial(lanternMat, "runtime:/sandbox/lantern-mat");
    if (!lanternMat || lanternMat->id == NULL_ASSET)
        return false;
    AssetRef<Model> lanternModel = internAndUploadProceduralModel(renderer(), assets(), std::move(lanternData), lanternMat, "runtime:/sandbox/lantern");
    if (!lanternModel)
        return false;
    m_lanternModelId = lanternModel->id;

    MeshData tracerData;
    if (!CreateSphere(tracerData, 0.5f, 8, 12))
    {
        DE_LOG_ERROR("SandboxApp: projectile tracer mesh failed");
        return true;
    }
    AssetRef<Material> tracerMat = internSolidMaterial(assets(), 255, 196, 48, 255, "runtime:/sandbox/tracer-mat");
    AssetRef<Model> tracerModel  = internAndUploadProceduralModel(renderer(), assets(), std::move(tracerData), tracerMat, "runtime:/sandbox/tracer");
    if (tracerModel)
        m_tracerModelId = tracerModel->id;
    else
        DE_LOG_ERROR("SandboxApp: projectile tracer model failed");
    return true;
}

void SandboxApp::spawnGltfDemo()
{
    auto spawn = [&](const char* virtualPath, const char* tag, float x, float z, float scale) {
        AssetRef<Model> model = loadAndUploadModel(renderer(), assets(), virtualPath);
        if (!model || !model->valid())
        {
            DE_LOG_WARN("SandboxApp: glTF '{}' not loaded", virtualPath);
            return;
        }
        const float groundY = m_terrain.heightAtWorld(x, z);
        const float y       = groundY + scale * 0.5f;
        Entity e = world().createEntity();
        world().emplace<TagComponent>(e, tag);
        world().emplace<TransformComponent>(e, Vector3f{ x, y, z }, Quaternion::IDENTITY, Vector3f{ scale, scale, scale });
        ModelComponent mc;
        mc.modelAssetID = model->id;
        mc.castShadow   = model->hasOpaque();
        world().emplace<ModelComponent>(e, mc);
        DE_LOG_INFO("SandboxApp: spawned {} at ({:.1f},{:.1f},{:.1f}) ground {:.1f}", tag, x, y, z, groundY);
    };

    // Sit on the terrain next to the player cube at the origin (default camera looks here).
    const EntityMaster cubeMaster = loadEntityMasterType("unit_cube");
    const EntityMaster glassMaster = loadEntityMasterType("unit_glass");
    const std::string  cubeGltf = cubeMaster.gltf.empty() ? std::string("models/unit_cube.gltf") : cubeMaster.gltf;
    const std::string  glassGltf = glassMaster.gltf.empty() ? std::string("models/unit_glass.gltf") : glassMaster.gltf;
    spawn(cubeGltf.c_str(), "GltfCube", 3.0f, 3.0f, 2.0f);
    spawn(glassGltf.c_str(), "GltfGlass", 5.5f, 3.0f, 2.0f);
    spawnAnimatedDemo();
}

void SandboxApp::onWiggleNotify(void*, const AnimNotify& n)
{
    if (!n.name || std::strcmp(n.name, "footstep") != 0)
        return;
    DE_LOG_INFO("SandboxApp: wiggle footstep t={:.2f}", n.time);
}

void SandboxApp::spawnAnimatedDemo()
{
    const EntityMaster wiggleMaster = loadEntityMasterType("wiggle");
    const std::string  wiggleGltf = wiggleMaster.gltf.empty() ? std::string("models/wiggle.gltf") : wiggleMaster.gltf;
    const char*        kGltf = wiggleGltf.c_str();
    AssetRef<Model> model = loadAndUploadModel(renderer(), assets(), kGltf);
    if (!model || !model->valid())
    {
        DE_LOG_WARN("SandboxApp: animated glTF '{}' not loaded", kGltf);
        return;
    }
    AssetRef<AnimGraphDef> graph;
    if (!wiggleMaster.anim.empty())
        graph = assets().loadAnimGraph(wiggleMaster.anim);
    if (!graph)
        graph = assets().tryLoadAnimGraphForModel(kGltf);
    if (!graph)
        DE_LOG_WARN("SandboxApp: '{}' has no anim graph sidecar; clips still play if present", kGltf);

    constexpr float x = 8.0f;
    constexpr float z = 3.0f;
    constexpr float scale = 2.0f;
    const float groundY = m_terrain.heightAtWorld(x, z);
    Entity e = world().createEntity();
    world().emplace<TagComponent>(e, "Wiggle");
    world().emplace<TransformComponent>(e, Vector3f{ x, groundY, z }, Quaternion::IDENTITY, Vector3f{ scale, scale, scale });
    ModelComponent mc;
    mc.modelAssetID = model->id;
    mc.castShadow   = model->hasOpaque();
    world().emplace<ModelComponent>(e, mc);

    AnimGraphComponent ag;
    ag.model   = model;
    ag.animSet = model->animationSet();
    ag.graphDef = graph;
    if (graph && model->skeleton())
        ag.graph.bind(graph.get(), model->skeleton());
    else if (ag.animSet && model->skeleton())
    {
        ag.graph.player().bind(model->skeleton(), ag.animSet.get());
        if (!ag.graph.player().play("Idle", 0.0f))
            ag.graph.player().playIndex(0, 0.0f);
    }
    ag.graph.setApplyRootMotion(false);
    ag.graph.player().addListener(&SandboxApp::onWiggleNotify, this);
    world().emplace<AnimGraphComponent>(e, std::move(ag));
    m_wiggle = e;
    DE_LOG_INFO("SandboxApp: spawned Wiggle at ({:.1f},{:.1f},{:.1f}) ground {:.1f} graph={}", x, groundY, z, groundY, graph ? "yes" : "no");
}

void SandboxApp::updateWiggleAnim()
{
    if (!m_wiggle.valid())
        return;
    AnimGraphComponent* ag = world().get<AnimGraphComponent>(m_wiggle);
    if (!ag || !ag->graphDef)
        return;

    float speed = 0.0f;
    const bool uiKeys = uiOpen() && m_imgui.isReady() && m_imgui.wantCaptureKeyboard();
    if (!uiKeys && input().actionDown("anim_walk"))
        speed = 1.0f;
    else
    {
        const Vector3f v = localMotor() ? localMotor()->velocity() : Vector3f{};
        speed = Vector3f(v.x, 0.0f, v.z).Magnitude();
    }
    ag->graph.setFloat("speed", speed);
}

bool SandboxApp::createSkeletonLineBuffers()
{
    ID3D12Device* device = renderer().device();
    if (!device)
        return false;
    constexpr uint64_t kMaxVerts = 1024;
    const uint64_t vbBytes = sizeof(Vector3f) * kMaxVerts;
    const uint64_t ibBytes = sizeof(uint32_t) * kMaxVerts;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Height           = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.SampleDesc       = { 1, 0 };
    desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (int i = 0; i < 2; ++i)
    {
        desc.Width = vbBytes;
        if (FAILED(device->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_skelLineVb[i]))))
            return false;
        desc.Width = ibBytes;
        if (FAILED(device->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_skelLineIb[i]))))
            return false;
        m_skelLineVbv[i].BufferLocation = m_skelLineVb[i]->GetGPUVirtualAddress();
        m_skelLineVbv[i].StrideInBytes  = sizeof(Vector3f);
        m_skelLineVbv[i].SizeInBytes    = static_cast<UINT>(vbBytes);
        m_skelLineIbv[i].BufferLocation = m_skelLineIb[i]->GetGPUVirtualAddress();
        m_skelLineIbv[i].Format         = DXGI_FORMAT_R32_UINT;
        m_skelLineIbv[i].SizeInBytes    = static_cast<UINT>(ibBytes);
    }
    return true;
}

void SandboxApp::drawSkeletonOverlay(ID3D12GraphicsCommandList* cmd, const Matrix4f& viewProj)
{
    if (!m_showSkeleton || !cmd || !m_skelLinePipeline.isValid() || !m_skelLineVb[0])
        return;

    const GpuScope skeleton(cmd, "Skeleton", ProfileColor::Skeleton);
    SkeletonDebugLines lines;
    world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
        if (!mc.visible)
            return;
        const TransformComponent* xf = world().get<TransformComponent>(e);
        const auto model = assets().getAs<Model>(mc.modelAssetID);
        if (!xf || !model || !model->skinned() || !model->skeleton())
            return;
        const AnimPose* pose = skinnedPose(*model, world().get<AnimGraphComponent>(e));
        if (!pose || pose->boneCount == 0)
            return;
        SkeletonDebugLines one;
        collectSkeletonDebugLines(*model->skeleton(), *pose, makeWorldMatrix(*xf), 0.25f, one);
        lines.bones.insert(lines.bones.end(), one.bones.begin(), one.bones.end());
        lines.axisX.insert(lines.axisX.end(), one.axisX.begin(), one.axisX.end());
        lines.axisY.insert(lines.axisY.end(), one.axisY.begin(), one.axisY.end());
        lines.axisZ.insert(lines.axisZ.end(), one.axisZ.begin(), one.axisZ.end());
    });

    struct Batch
    {
        const std::vector<Vector3f>* verts;
        float r, g, b;
    };
    const Batch batches[] = {
        { &lines.bones, 1.00f, 0.45f, 0.95f },
        { &lines.axisX, 1.00f, 0.25f, 0.20f },
        { &lines.axisY, 0.25f, 1.00f, 0.30f },
        { &lines.axisZ, 0.30f, 0.55f, 1.00f },
    };

    std::vector<Vector3f> verts;
    std::vector<uint32_t> idx;
    uint32_t rangeStart[4]{};
    uint32_t rangeCount[4]{};
    verts.reserve(256);
    idx.reserve(256);
    for (int b = 0; b < 4; ++b)
    {
        rangeStart[b] = static_cast<uint32_t>(idx.size());
        const auto& src = *batches[b].verts;
        for (size_t i = 0; i + 1 < src.size(); i += 2)
        {
            const uint32_t i0 = static_cast<uint32_t>(verts.size());
            verts.push_back(src[i]);
            verts.push_back(src[i + 1]);
            idx.push_back(i0);
            idx.push_back(i0 + 1);
        }
        rangeCount[b] = static_cast<uint32_t>(idx.size()) - rangeStart[b];
    }
    constexpr size_t kMaxVerts = 1024;
    if (verts.empty() || idx.empty() || verts.size() > kMaxVerts)
        return;

    const uint32_t fi = renderer().frameIndex() % 2;
    void* vp = nullptr;
    void* ip = nullptr;
    if (FAILED(m_skelLineVb[fi]->Map(0, nullptr, &vp)) || FAILED(m_skelLineIb[fi]->Map(0, nullptr, &ip)))
        return;
    std::memcpy(vp, verts.data(), verts.size() * sizeof(Vector3f));
    std::memcpy(ip, idx.data(), idx.size() * sizeof(uint32_t));
    m_skelLineVb[fi]->Unmap(0, nullptr);
    m_skelLineIb[fi]->Unmap(0, nullptr);
    m_skelLineVbv[fi].SizeInBytes = static_cast<UINT>(verts.size() * sizeof(Vector3f));
    m_skelLineIbv[fi].SizeInBytes = static_cast<UINT>(idx.size() * sizeof(uint32_t));

    m_skelLinePipeline.bind(cmd);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    cmd->IASetVertexBuffers(0, 1, &m_skelLineVbv[fi]);
    cmd->IASetIndexBuffer(&m_skelLineIbv[fi]);
    LineFrameConstants lc{};
    copyMatrix(lc.worldViewProj, viewProj);
    lc.color[3] = 1.0f;
    for (int b = 0; b < 4; ++b)
    {
        if (rangeCount[b] == 0)
            continue;
        lc.color[0] = batches[b].r;
        lc.color[1] = batches[b].g;
        lc.color[2] = batches[b].b;
        m_skelLinePipeline.setConstants(cmd, lc);
        cmd->DrawIndexedInstanced(rangeCount[b], 1, rangeStart[b], 0, 0);
    }
}

void SandboxApp::spawnHybridLocalLights()
{
    if (renderer().scenePath() != ScenePath::HybridDeferred)
        return;

    m_muzzle = world().createEntity();
    world().emplace<TagComponent>(m_muzzle, "Muzzle");
    world().emplace<TransformComponent>(m_muzzle, Vector3f{}, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
    auto& muzzle     = world().emplace<LocalLightComponent>(m_muzzle);
    muzzle.type      = LocalLightType::Point;
    muzzle.color     = Vector3f{ 1.0f, 0.82f, 0.45f };
    muzzle.intensity = 37699.0f; // 12000*π after Fd/π
    muzzle.range     = 6.0f;
    muzzle.enabled   = false;
    m_muzzleTimer    = 0.0f;

    // ±1.2 from pack/tree XZ so 0.22 cubes sit outside trunks (r=0.5) and health crosses.
    const Vector3f spots[] = {
        { 6.2f, 0.0f, 5.0f },
        { -8.2f, 0.0f, 7.0f },
        { 8.0f, 0.0f, -9.2f },
        { -4.0f, 0.0f, -5.8f },
        { 13.2f, 0.0f, 8.0f },
        { -11.2f, 0.0f, 10.0f },
        { 14.0f, 0.0f, -7.2f },
        { 5.2f, 0.0f, -18.0f },
    };
    if (!m_demoScene)
    {
        m_offhand.lightOn = false;
        DE_LOG_INFO("SandboxApp: flashlight off, no demo lanterns in this scene");
        return;
    }
    const float waterY = m_water.params().waterLevel;
    int         spawned = 0;
    bool        firstLantern = true;
    for (const Vector3f& s : spots)
    {
        const float gy = m_terrain.heightAtWorld(s.x, s.z);
        if (gy < waterY - 0.2f)
            continue;
        const Vector3f pos{ s.x, gy + 1.55f, s.z };

        Entity fixture = world().createEntity();
        world().emplace<TagComponent>(fixture, "Lantern");
        world().emplace<TransformComponent>(fixture, pos, Quaternion::IDENTITY, Vector3f{ 0.22f, 0.22f, 0.22f });
        if (m_lanternModelId != NULL_ASSET)
        {
            ModelComponent mc{};
            mc.modelAssetID = m_lanternModelId;
            mc.castShadow   = true;
            setModelComponent(world(), pins(), assets(), fixture, mc);
        }

        Entity lightE = world().createEntity();
        world().emplace<TagComponent>(lightE, "LanternLight");
        world().emplace<TransformComponent>(lightE, pos, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
        auto& light        = world().emplace<LocalLightComponent>(lightE);
        light.type         = LocalLightType::Point;
        light.color        = Vector3f{ 1.0f, 0.72f, 0.35f };
        light.intensity    = 1257.0f; // 400*π after Fd/π
        light.range        = 6.0f;
        light.emissiveMesh = fixture;
        if (firstLantern)
        {
            light.castShadow = true;
            firstLantern     = false;
        }
        ++spawned;
    }
    DE_LOG_INFO("SandboxApp: muzzle ready, {} demo lanterns", spawned);
}

void SandboxApp::spawnHunterBlood(const Vector3f& pos)
{
    m_blood.setTransform(Vector3f{ pos.x, pos.y + 0.45f, pos.z });
    m_blood.emitBurst(12);
}

ImpactFxHost SandboxApp::impactHost()
{
    ImpactFxHost host;
    host.world       = &world();
    host.scene       = &m_scene;
    host.renderer    = &renderer();
    host.terrain     = &m_terrain;
    host.ground      = &m_ground;
    host.contactGrid = &m_terrain;
    host.targets     = &m_weaponTargets;
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

void SandboxApp::placeHealthPacks()
{
    std::vector<Entity> old;
    world().each<HealthPackComponent>([&](Entity e, HealthPackComponent&) { old.push_back(e); });
    for (Entity e : old)
    {
        onEntityRemoved(world(), e, &pins());
        world().destroyEntity(e);
    }

    const Vector3f spots[] = {
        { 5.0f, 0.0f, 5.0f },
        { -7.0f, 0.0f, 7.0f },
        { 8.0f, 0.0f, -8.0f },
        { -4.0f, 0.0f, -7.0f },
    };
    const float   waterY = m_water.params().waterLevel;
    int count = 0;
    for (const Vector3f& s : spots)
    {
        const float gy = m_terrain.heightAtWorld(s.x, s.z);
        if (gy < waterY - 0.2f)
            continue;
        const Vector3f pos{ s.x, gy + 0.95f, s.z };
        Entity         e = world().createEntity();
        world().emplace<TagComponent>(e, "HealthPack");
        TransformComponent xf{};
        xf.position = pos;
        xf.scale    = Vector3f{ 0.9f, 0.9f, 0.9f };
        world().emplace<TransformComponent>(e, xf);
        if (m_packModelId != NULL_ASSET)
        {
            ModelComponent mc{};
            mc.modelAssetID = m_packModelId;
            mc.castShadow   = true;
            setModelComponent(world(), pins(), assets(), e, mc);
        }
        HealthPackComponent pack{};
        pack.restPos = pos;
        pack.active  = true;
        world().emplace<HealthPackComponent>(e, pack);
        stampProceduralId(world(), e, std::format("sandbox/healthpack/{}", count), "healthpack");
        ++count;
    }
    DE_LOG_INFO("SandboxApp: {} health packs", count);
}


void SandboxApp::drawProjectiles(ID3D12GraphicsCommandList* cmd, const Matrix4f& viewProj, MeshFrameConstants& cb, bool instanced, const std::vector<int>* rejectedShots)
{
    WeaponLoadout* w = localWeapons();
    const auto model = assets().getAs<Model>(m_tracerModelId);
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

    GpuResourceCache& gpu = renderer().gpuResources();
    if (!instanced && !gpu.ensureModel(model))
        return;

    const GpuScope projectiles(cmd, "Projectiles", ProfileColor::Projectiles);
    const float     radius = w->projectile().desc().radius;
    const float     scale  = radius * 2.0f;
    const DebugFill fill   = renderer().debugState().fill;
    if (instanced)
    {
        MeshForwardBatchConstants lighting{};
        fillForwardLighting(lighting, cb);
        m_drawList.recordForward(cmd, gpu, ViewId{ 0 }, StaticPopulation::Projectiles, lighting, m_shadows.receiverCbvGpu());
        if (!rejectedShots || rejectedShots->empty() || !gpu.ensureModel(model))
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

void SandboxApp::drawProjectilesGBuffer(ID3D12GraphicsCommandList* cmd, const Matrix4f& viewProj, const Matrix4f& prevViewProj, bool instanced, const std::vector<int>* rejectedShots)
{
    WeaponLoadout* w = localWeapons();
    const auto model = assets().getAs<Model>(m_tracerModelId);
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

    GpuResourceCache& gpu = renderer().gpuResources();
    if (!instanced && !gpu.ensureModel(model))
        return;

    const GpuScope projectiles(cmd, "Projectiles", ProfileColor::Projectiles);
    if (instanced)
    {
        m_drawList.recordOpaque(cmd, gpu, ViewId{ 0 }, StaticPopulation::Projectiles);
        if (!rejectedShots || rejectedShots->empty() || !gpu.ensureModel(model))
            return;
        const float     radius = w->projectile().desc().radius;
        const float     scale  = radius * 2.0f;
        const DebugFill fill   = renderer().debugState().fill;
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
    const DebugFill fill   = renderer().debugState().fill;
    for (const LiveProjectile& s : w->projectile().live())
    {
        if (!s.alive)
            continue;
        const Matrix4f world = Matrix4f::ScaleMatrixXYZ(scale, scale, scale) * Matrix4f::TranslationMatrix(s.position.x, s.position.y, s.position.z);
        drawModelOpaqueGBuffer(cmd, gpu, m_meshPipeline, *model, world, viewProj, prevViewProj, fill);
    }
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
    if (m_cube.valid())
    {
        if (const TransformComponent* xf = world().get<TransformComponent>(m_cube))
        {
            pos.x = xf->position.x + offsetX;
            pos.z = xf->position.z;
        }
    }
    pos.y = m_terrain.heightAtWorld(pos.x, pos.z) + 0.5f;

    Entity e = world().createEntity();
    world().emplace<TagComponent>(e, "PlayerPawn");
    world().emplace<TransformComponent>(e, pos, Quaternion::IDENTITY, Vector3f{ 1, 1, 1 });
    attachSandboxCubeModel(world(), pins(), assets(), e, m_cubeModelId);
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
    if (m_cube.valid() && world().alive(m_cube))
        return;

    const float groundY = m_terrain.heightAtWorld(0.0f, 0.0f) + 0.5f;
    m_cube              = world().createEntity();
    world().emplace<TagComponent>(m_cube, "Cube");
    world().emplace<TransformComponent>(m_cube, Vector3f{ 0.0f, groundY, 0.0f }, Quaternion::IDENTITY, Vector3f{ 1, 1, 1 });
    attachSandboxCubeModel(world(), pins(), assets(), m_cube, m_cubeModelId);
    if (!network().registerEntity(world(), m_cube, NetPrefab::Cube))
    {
        onEntityRemoved(world(), m_cube, &pins());
        world().destroyEntity(m_cube);
        m_cube = {};
        DE_LOG_ERROR(LogCategory::Networking, "Sandbox: failed to register local cube");
    }
}

bool SandboxApp::onNetSpawn(World& world, Entity e, NetPrefab prefab, const TransformComponent&, uint32_t, void* user)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app || !e.valid())
        return false;
    if (!world.has<ModelComponent>(e))
        attachSandboxCubeModel(world, app->pins(), app->assets(), e, app->m_cubeModelId);
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
    if (app->m_cube == e)
        app->m_cube = {};
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

    AssetRef<Model> model;
    if (const ModelComponent* mc = world().get<ModelComponent>(e))
        model = assets().getAs<Model>(mc->modelAssetID);
    if (!Physics::bindPhysicsEntity(m_physics, world(), e, model.get()))
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
    return raisedGroundY(m_physics, m_groundIgnore, m_physicsGround, m_terrain.heightAtWorld(x, z), x, z, m_groundProbeY);
}

Terrain::GroundContact SandboxApp::groundContactAt(float x, float z) const
{
    return Terrain::groundContactAt(m_ground, &m_terrain, x, z);
}

void SandboxApp::onInit()
{
    DE_LOG_INFO("SandboxApp: init");
    loadEchoChirpSettings(m_echoChirp);

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
        if (!m_scene.init(renderer(), sceneDesc))
        {
            requestQuit();
            return;
        }
        m_foliagePipeline.create(renderer().device());
        if (!m_drawList.create(renderer().device()))
            DE_LOG_ERROR(LogCategory::Render, "SandboxApp: instanced mesh pipeline failed; static draws stay immediate");
        if (!m_skinnedInstances.create(renderer().device()))
            DE_LOG_ERROR(LogCategory::Render, "SandboxApp: skinned instancing unavailable; skinned draws stay immediate");
        if (!m_grassPipeline.create(renderer()))
        {
            DE_LOG_ERROR(LogCategory::Render, "SandboxApp: grass pipeline create failed");
            GrassParams grassOff = m_grassField.params();
            grassOff.enabled     = false;
            m_grassField.setParams(grassOff);
        }
        m_foliagePrototypes.create(renderer(), assets());
        if (!m_skelLinePipeline.create(renderer().device(), renderer().sceneColorFormat(), false))
            DE_LOG_ERROR(LogCategory::Render, "SandboxApp: skeleton LinePipeline create failed");
        else if (!createSkeletonLineBuffers())
            DE_LOG_ERROR(LogCategory::Render, "SandboxApp: skeleton line buffers failed");
    }

    if (!m_healthHud.create(renderer()))
        DE_LOG_ERROR("SandboxApp: health HUD failed");
    if (!m_crosshair.create(renderer()))
        DE_LOG_ERROR("SandboxApp: crosshair HUD failed");
    if (!m_particles.create(renderer(), assets()))
        DE_LOG_ERROR("SandboxApp: particle renderer failed");
    if (!m_leafRenderer.create(renderer(), assets()))
        DE_LOG_ERROR(LogCategory::Render, "SandboxApp: leaf renderer failed");
    if (!m_bloodSplats.create(renderer()))
        DE_LOG_ERROR("SandboxApp: blood splat pool failed");
    {
        ParticleEmitterDesc blood{};
        blood.name          = "Blood";
        blood.maxParticles  = 128;
        blood.emissionRate  = 0.0f;
        blood.duration      = 0.0f;
        blood.looping       = false;
        blood.lifetime      = { 0.22f, 0.50f };
        blood.startSpeed    = { 1.8f, 4.2f };
        blood.startSize     = { 0.07f, 0.14f };
        blood.endSize       = { 0.02f, 0.05f };
        blood.startColor[0] = 0.72f;
        blood.startColor[1] = 0.04f;
        blood.startColor[2] = 0.06f;
        blood.startColor[3] = 0.95f;
        blood.endColor[0]   = 0.28f;
        blood.endColor[1]   = 0.00f;
        blood.endColor[2]   = 0.01f;
        blood.endColor[3]   = 0.00f;
        blood.gravity       = Vector3f{ 0.0f, -11.0f, 0.0f };
        blood.direction     = Vector3f{ 0.0f, 1.0f, 0.0f };
        blood.spreadDegrees = 75.0f;
        blood.shape         = ParticleEmitterDesc::Shape::Sphere;
        blood.shapeSize     = Vector3f{ 0.12f, 0.0f, 0.0f };
        blood.additiveBlend = false;
        m_blood.setDesc(blood);
        m_blood.stop(true);
    }
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
    if (!m_menu.create(renderer(), UiAccent::Sandbox))
        DE_LOG_ERROR(LogCategory::Render, "SandboxApp: main menu GPU init failed");
    else if (shouldShowMainMenu(config()))
    {
        m_menu.show();
        DE_LOG_INFO("Sandbox: main menu ({} entries)", m_menu.entries().size());
    }

    if (!m_imgui.init(window(), renderer(), "sandbox_imgui.ini", false, UiAccent::Sandbox))
        DE_LOG_WARN("SandboxApp: ImGui init failed — Dev Tools (M) disabled");
    if (!pumpBootFrame())
        return;

    if (!loadAndBakeIbl(renderer(), assets(), m_ibl, m_iblImageId))
        DE_LOG_INFO(LogCategory::Render, "SandboxApp: IBL off — using ambient");

    // Menu keys are polled in the main loop, after onInit returns. Load now only
    // when the picker will not be shown (-no-menu, net host, or menu GPU failed).
    if (!m_menu.visible())
    {
        if (!startScene(defaultScenePath(sceneFileArg())))
            return;
    }
}

bool SandboxApp::startScene(const std::filesystem::path& scenePath)
{
    if (m_sceneStarted)
        return true;

    std::vector<SceneObjectData> scenePawns;
    std::vector<SceneObjectData> sceneBoxes;
    std::vector<SceneObjectData> sceneLights;
    std::vector<SceneObjectData> sceneSpawners;
    {
        auto sidecar = [](const std::filesystem::path& scenePath, const std::string& file) {
            if (file.empty())
                return std::filesystem::path{};
            const std::filesystem::path p(file);
            if (p.is_absolute())
                return p;
            const std::filesystem::path parent = scenePath.has_parent_path() ? scenePath.parent_path() : std::filesystem::path{};
            return parent / p;
        };

        bool loadedScene = false;
        bool worldEngine = false;
        Terrain::SplatMap matSplat;
        Terrain::WorldEngineMaps worldMaps;
        SceneFileData sceneData{};
        {
            std::string err;
            if (!loadSceneFromJson(scenePath, sceneData, &err))
                DE_LOG_ERROR(LogCategory::Render, "SandboxApp: scene load failed ({}) — {}", scenePath.string(), err);
            else
                DE_LOG_INFO(LogCategory::Render, "SandboxApp: scene {}", scenePath.string());
            m_sceneStealth = sceneData.stealth;
            applySceneAwareness(m_chase.ai(), m_sceneStealth);
            m_sceneQuest = sceneData.quest;
            applySceneAtmosphere(m_env, sceneData);
            {
                const ExposureSceneDesc exposure = sceneData.exposure.present ? sceneData.exposure : ExposureSceneDesc{};
                m_autoExposure.mode        = exposure.autoExposure ? ExposureMode::Auto : ExposureMode::Manual;
                m_autoExposure.evBias      = exposure.evBias;
                m_autoExposure.maxEv       = exposure.maxEv;
                m_autoExposure.adaptBright = exposure.adaptBright;
                m_autoExposure.adaptDark   = exposure.adaptDark;
                resetAutoExposure(m_autoExposureState);
                DE_LOG_INFO(LogCategory::Render, "SandboxApp: exposure auto={}", exposure.autoExposure);
            }
            if (sceneData.mode != SceneMode::Scene2D && sceneData.hasTerrain)
            {
                if (!sceneData.terrain.source.empty())
                {
                    const std::filesystem::path dir = Terrain::resolveWorldEngineDirectory(sceneData.terrain.source);
                    Terrain::WorldEngineLoadDesc importDesc{};
                    importDesc.worldSizeMeters   = sceneData.terrain.worldSize > 1.0f ? sceneData.terrain.worldSize : 1024.0f;
                    importDesc.heightRangeMeters = sceneData.terrain.importHeight > 0.0f ? sceneData.terrain.importHeight : 480.0f;
                    const int chunkCells = sceneData.terrain.chunkCells > 0 ? sceneData.terrain.chunkCells : 64;
                    if (!dir.empty()
                        && Terrain::loadWorldEngineDirectory(dir, importDesc, worldMaps)
                        && m_terrain.createFromHeightMap(std::move(worldMaps.height), chunkCells))
                    {
                        matSplat = worldMaps.splat;
                        if (!m_terrain.setWorkingSplat(std::move(worldMaps.splat)))
                            DE_LOG_WARN(LogCategory::Render, "SandboxApp: World Engine splat was not kept");
                        loadedScene = true;
                        worldEngine = true;
                        DE_LOG_INFO(LogCategory::Render, "SandboxApp: loaded World Engine terrain '{}'", sceneData.terrain.source);
                    }
                    else
                        DE_LOG_ERROR(LogCategory::Render, "SandboxApp: World Engine folder '{}' failed", sceneData.terrain.source);
                }
                else if (sceneData.terrain.hasGrid)
                {
                    TerrainGridDesc gridDesc{};
                    gridDesc.tilesX       = sceneData.terrain.grid.tilesX;
                    gridDesc.tilesZ       = sceneData.terrain.grid.tilesZ;
                    gridDesc.tileCells    = sceneData.terrain.grid.tileCells;
                    gridDesc.chunkCells   = sceneData.terrain.chunkCells > 0 ? sceneData.terrain.chunkCells : 64;
                    gridDesc.cellSize     = sceneData.terrain.grid.cellSize;
                    gridDesc.heightScale  = sceneData.terrain.grid.heightScale;
                    gridDesc.origin       = sceneData.terrain.grid.origin;
                    gridDesc.coarseFile   = sidecar(scenePath, sceneData.terrain.grid.coarseFile);
                    gridDesc.tileDir      = sidecar(scenePath, sceneData.terrain.grid.tileDir);
                    gridDesc.residentRing = sceneData.terrain.grid.residentRing;
                    if (m_terrain.create(gridDesc))
                    {
                        loadedScene        = true;
                        m_haveTerrainSea   = true;
                        m_terrainSeaLevel  = sceneData.terrain.grid.seaLevel;
                        DE_LOG_INFO(LogCategory::Render, "SandboxApp: streaming scene terrain {}x{} tiles", gridDesc.tilesX, gridDesc.tilesZ);
                    }
                    else
                        DE_LOG_ERROR(LogCategory::Render, "SandboxApp: scene terrain.grid failed — FBM fallback");
                }
                else if (!sceneData.terrain.heightFile.empty())
                {
                    HeightMap height;
                    if (height.loadBinary(sidecar(scenePath, sceneData.terrain.heightFile))
                        && m_terrain.createFromHeightMap(std::move(height), sceneData.terrain.chunkCells > 0 ? sceneData.terrain.chunkCells : 16))
                    {
                        loadedScene = true;
                        DE_LOG_INFO(LogCategory::Render, "SandboxApp: loaded legacy heightFile as 1-tile Grid");
                    }
                }
            }
        }

        m_foliageDensity.dirtTreesPerM2    = sceneData.terrain.foliage.dirtTreesPerM2;
        m_foliageDensity.dirtFlowersPerM2  = sceneData.terrain.foliage.dirtFlowersPerM2;
        m_foliageDensity.grassTreesPerM2   = sceneData.terrain.foliage.grassTreesPerM2;
        m_foliageDensity.grassFlowersPerM2 = sceneData.terrain.foliage.grassFlowersPerM2;
        m_foliageDensity.rockPerM2         = sceneData.terrain.foliage.rockPerM2;
        m_foliageDensity.grassPerM2        = sceneData.terrain.foliage.grassPerM2;
        m_foliageDensity.seed              = sceneData.terrain.foliage.seed;
        m_foliageDensity.treeModel         = sceneData.terrain.foliage.treeModel;
        m_foliageDensity.flowerModel       = sceneData.terrain.foliage.flowerModel;
        m_foliageDensity.rockModel         = sceneData.terrain.foliage.rockModel;
        m_foliageDensity.grassModel        = sceneData.terrain.foliage.grassModel;
        m_leafShedScale                    = sceneData.terrain.foliage.shedScale;

        m_wind = sceneData.terrain.wind.settings;
        clampWindSettings(m_wind);

        const GrassSceneDesc& grassScene = sceneData.terrain.grass;
        GrassParams           grassParams;
        grassParams.enabled          = grassScene.enabled;
        grassParams.heightMetres     = grassScene.heightMetres;
        grassParams.flexibility      = grassScene.flexibility;
        grassParams.densityScale     = grassScene.densityScale;
        grassParams.windTipMetres    = grassScene.windTipMetres;
        grassParams.faceEdge         = grassScene.faceEdge;
        grassParams.faceYaw          = grassScene.faceYaw;
        grassParams.faceLean         = grassScene.faceLean;
        grassParams.faceSteep        = grassScene.faceSteep;
        applyWindToGrass(m_wind, grassParams);
        if (!m_grassPipeline.isValid())
            grassParams.enabled = false;
        m_grassField.setParams(grassParams);

        if (!loadedScene)
        {
            HeightMap base;
            HeightMap detail;
            if (!base.createFbm(129, 129, 1337u, 6, 3.5f, 1.0f, 2.1f, 0.48f, 2.0f, 22.0f)
                || !detail.createFbm(129, 129, 9001u, 3, 18.0f, 0.12f, 2.0f, 0.5f, 2.0f, 22.0f)
                || !base.addLayer(detail, 1.0f))
            {
                DE_LOG_FATAL("SandboxApp: height map create failed");
                requestQuit();
                return false;
            }
            const float extent = 128.0f * 2.0f;
            base.setOrigin(Vector3f{ -0.5f * extent, 0.0f, -0.5f * extent });
            if (!matSplat.generateFromHeight(base))
            {
                DE_LOG_FATAL("SandboxApp: splat generate failed");
                requestQuit();
                return false;
            }
            if (!m_terrain.createFromHeightMap(std::move(base), 16))
            {
                DE_LOG_FATAL("SandboxApp: terrain create failed");
                requestQuit();
                return false;
            }
            m_terrain.setWorkingSplat(SplatMap(matSplat));
        }

        const AABox3f terrainBox = m_terrain.bounds();
        const WaterSceneDesc& waterScene = sceneData.water;
        float waterLevel = Lerp(terrainBox.Min.y, terrainBox.Max.y, waterScene.levelFraction);
        if (waterScene.hasLevel)
            waterLevel = waterScene.level;
        else if (m_haveTerrainSea)
            waterLevel = m_terrainSeaLevel;

        if (worldEngine)
        {
            const HeightMap* height = m_terrain.editableWorking();
            const SplatMap*  splat  = m_terrain.editableWorkingSplat();
            FoliageSpawnIn   spawnIn;
            spawnIn.height    = height;
            spawnIn.splat     = splat;
            spawnIn.density   = m_foliageDensity;
            spawnIn.tilesX    = m_terrain.tilesX();
            spawnIn.tilesZ    = m_terrain.tilesZ();
            const int cells   = m_terrain.tileCells();
            spawnIn.tileCells = cells > 0 ? static_cast<uint32_t>(cells) : 0u;
            spawnIn.cellSize  = m_terrain.cellSize();
            spawnIn.origin    = m_terrain.origin();
            spawnIn.seaLevel  = height ? height->origin().y : 0.0f;
            FoliageWaterVolume lake;
            lake.entireMap = true;
            lake.surfaceY  = waterLevel;
            spawnIn.water.push_back(lake);
            FoliageSpawnOut spawned;
            if (height && splat && spawnFoliage(spawnIn, spawned))
                m_foliage.swap(spawned.records);
            else
                DE_LOG_WARN(LogCategory::Render, "SandboxApp: splat foliage spawn failed");
        }

        if (!matSplat.valid() && !matSplat.create(2, 2))
        {
            DE_LOG_FATAL("SandboxApp: dummy splat failed");
            requestQuit();
            return false;
        }
        if (worldEngine)
        {
            if (!Terrain::uploadWorldEngineMaterial(renderer(), assets(), matSplat, worldMaps, m_terrainMaterial, nullptr))
            {
                DE_LOG_FATAL("SandboxApp: World Engine material failed");
                requestQuit();
                return false;
            }
            DE_LOG_INFO(LogCategory::Render, "Terrain: World Engine albedo and roughness");
        }
        else if (tryCreateTerrainFromContent(renderer(), assets(), matSplat, m_terrainMaterial))
            DE_LOG_INFO(LogCategory::Render, "Terrain: using content layer set");
        else
        {
            DE_LOG_INFO(LogCategory::Render, "Terrain: checkers fallback");
            if (!m_terrainMaterial.createDefault(renderer(), matSplat))
            {
                DE_LOG_FATAL("SandboxApp: terrain material create failed");
                requestQuit();
                return false;
            }
        }
        m_terrainMaterial.setLayerSamplingRaw(renderer().device(), renderer().debugState().legacyUnormAlbedo);
        if (!pumpBootFrame())
            return false;
        m_terrain.updateStreaming(Vector3f{ 0.0f, 50.0f, -80.0f }, &renderer(), &m_terrainMaterial);
        if (!m_terrain.uploadCoarseHeightTexture(renderer()))
        {
            DE_LOG_FATAL("SandboxApp: coarse height texture upload failed");
            requestQuit();
            return false;
        }
        if (m_terrain.heightTexture().valid())
        {
            renderer().setHeightSrv(m_terrain.heightTexture().cpuHandle());
            m_waterPipeline.setHeightSrv(renderer().device(), m_terrain.heightTexture().cpuHandle());
        }
        if (!pumpBootFrame())
            return false;

        auto spawnCloudEntity = [&](const Vector3f& pos, const Vector3f& scale, const CloudVolumeDesc& desc) {
            Entity e = world().createEntity();
            TransformComponent xf{};
            xf.position = pos;
            xf.scale    = scale;
            world().emplace<TransformComponent>(e, xf);
            CloudVolumeComponent cloud{};
            cloud.desc = desc;
            world().emplace<CloudVolumeComponent>(e, cloud);
        };
        uint32_t spawnedClouds = 0;
        expandSceneBuildings(sceneData);
        for (const SceneObjectData& o : sceneData.objects)
        {
            if (o.type == SceneObjectType::Hunter || o.type == SceneObjectType::Wolf)
                scenePawns.push_back(o);
            if (o.type == SceneObjectType::Spawner)
                sceneSpawners.push_back(o);
            if (o.type == SceneObjectType::Cube)
                sceneBoxes.push_back(o);
            if (o.type == SceneObjectType::PointLight || o.type == SceneObjectType::SpotLight)
                sceneLights.push_back(o);
            if (o.type == SceneObjectType::Player)
                m_chase.setPlayerStart(o.position);
            if (o.type != SceneObjectType::CloudVolume)
                continue;
            CloudVolumeDesc desc{};
            if (o.hasCloud)
                cloudDescFromSceneData(o, desc);
            spawnCloudEntity(o.position, o.scale, desc);
            ++spawnedClouds;
        }
        if (spawnedClouds == 0)
        {
            CloudVolumeDesc desc{};
            spawnCloudEntity(Vector3f(0.0f, 36.0f, 0.0f), Vector3f(140.0f, 32.0f, 140.0f), desc);
            ++spawnedClouds;
        }
        DE_LOG_INFO(LogCategory::Render, "SandboxApp: {} cloud volume(s)", spawnedClouds);

        WaterDesc waterDesc;
        waterDesc.chunkCells = waterScene.chunkCells > 0 ? waterScene.chunkCells : 16;
        waterDesc.waterLevel = waterLevel;
        const int lodCount = waterScene.lodDistanceCount < 1 ? 1 : (waterScene.lodDistanceCount > Terrain::kMaxLodLevels ? Terrain::kMaxLodLevels : waterScene.lodDistanceCount);
        waterDesc.lodDistanceCount = lodCount;
        for (int i = 0; i < lodCount; ++i)
            waterDesc.lodDistances[i] = waterScene.lodDistances[i];
        waterDesc.params = sceneWaterParams(waterScene, waterLevel);
        if (!m_water.create(m_terrain.coarse(), waterDesc))
        {
            DE_LOG_FATAL("SandboxApp: water create failed");
            requestQuit();
            return false;
        }
        m_water.updateLod(Vector3f{ 0.0f, 50.0f, -80.0f });
        if (!m_water.createGpu(renderer()))
        {
            DE_LOG_FATAL("SandboxApp: water GPU upload failed");
            requestQuit();
            return false;
        }
        if (!pumpBootFrame())
            return false;
        DE_LOG_INFO("SandboxApp: water level {:.2f}, {} wet chunks", waterLevel, m_water.wetChunkCount());

        // Streams are visual only. cellWet and swimming a creek stay on the still lake.
        for (const SceneObjectData& o : sceneData.objects)
        {
            if (o.type != SceneObjectType::Stream || !o.hasStream)
                continue;
            if (m_streams.size() >= 16)
                break;
            StreamDesc desc;
            desc.width        = o.streamWidth;
            desc.flowSpeed    = o.streamFlowSpeed;
            desc.bedClearance = 0.45f;
            desc.pointsXZ     = o.streamPoints;
            MeshData cpu;
            SandboxStream slot;
            std::string err;
            if (!buildStreamRibbon(m_terrain.coarse(), desc, waterLevel, cpu, &slot.bounds, &err))
                continue;
            if (!Mesh::tryCreate(renderer(), cpu, slot.mesh))
            {
                DE_LOG_ERROR(LogCategory::Render, "SandboxApp: stream upload failed");
                continue;
            }
            slot.flowSpeed = o.streamFlowSpeed;
            m_streams.push_back(std::move(slot));
        }
    }

    MeshData cubeData;
    CreateCube(cubeData, 1.0f);
    m_cubeMesh = Mesh::Create(renderer(), cubeData);
    if (!m_cubeMesh.valid())
    {
        DE_LOG_FATAL("SandboxApp: cube mesh upload failed");
        requestQuit();
        return false;
    }
    MeshData crossData;
    if (!CreateCross(crossData, 1.0f, 0.30f, 0.22f) || !Mesh::tryCreate(renderer(), crossData, m_crossMesh))
    {
        DE_LOG_FATAL("SandboxApp: health pack mesh failed");
        requestQuit();
        return false;
    }

    if (!createSandboxModels())
    {
        DE_LOG_FATAL("SandboxApp: procedural models failed");
        requestQuit();
        return false;
    }

    if (!pumpBootFrame())
        return false;

    renderer().gpuResources().setShadowSrv(m_shadows.srvCpu());
    renderer().setShadowSrv(m_shadows.srvCpu());
    m_skyPipeline.setShadowSrv(renderer().device(), m_shadows.srvCpu());
    m_waterPipeline.setShadowSrv(renderer().device(), m_shadows.srvCpu());
    m_scene.localShadows().setDepthPipeline(&m_shadows.pipeline());
    {
        const LocalShadowSystem::CpuSrvs localSrvs = m_scene.localShadows().cpuSrvs();
        renderer().setLocalShadowSrvs(localSrvs.arraySrv, localSrvs.recordsSrv);
    }
    // The glTF demo and the npc overlay belong to level.json, whichever way it was opened.
    m_demoScene = scenePath.filename() == "level.json";
    if (!m_demoScene)
        m_npcInfo.enabled = false;
    if (m_demoScene)
        spawnGltfDemo();

    const float aspect = (renderer().height() > 0) ? static_cast<float>(renderer().width()) / static_cast<float>(renderer().height()) : 1.0f;
    m_viewCamera.SetLens(/*fovY*/ 1.04719755f /*60deg*/, aspect, 0.05f, 1.0e5f);
    m_viewCamera.LookAt(Vector3f(0.0f, 48.0f, -86.0f), Vector3f(0.0f, 8.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f));

    m_camera = world().createEntity();
    world().emplace<TagComponent>(m_camera, "Main Camera");
    world().emplace<TransformComponent>(m_camera, m_viewCamera.GetPosition(), Quaternion::IDENTITY, Vector3f{ 1, 1, 1 });
    world().emplace<CameraComponent>(m_camera, /* fovDeg */ 60.0f, /* near */ 0.5f, /* far */ 1.0e5f, /* primary */ true);
    world().emplace<AudioListenerComponent>(m_camera);
    attachCameraSounds(world(), pins(), assets(), audio(), m_camera);

    const float groundY = m_terrain.heightAtWorld(0.0f, 0.0f) + 0.5f;
    m_cube = world().createEntity();
    world().emplace<TagComponent>(m_cube, "Cube");
    world().emplace<TransformComponent>(m_cube, Vector3f{ 0.0f, groundY, 0.0f }, Quaternion::IDENTITY, Vector3f{ 1, 1, 1 });
    attachSandboxCubeModel(world(), pins(), assets(), m_cube, m_cubeModelId);

    network().setWantsPawn(true);
    network().setSceneMode(0);
    network().setPlayerName("Sandbox");
    network().setSpawnCallback(&SandboxApp::onNetSpawn, this);
    network().setDespawnCallback(&SandboxApp::onNetDespawn, this);
    network().setPeerCallback(&SandboxApp::onNetPeer, this);
    if (!network().registerEntity(world(), m_cube, NetPrefab::Cube))
    {
        onEntityRemoved(world(), m_cube, &pins());
        world().destroyEntity(m_cube);
        m_cube = {};
        DE_LOG_ERROR(LogCategory::Networking, "Sandbox: failed to register init cube");
    }

    DE_LOG_INFO(
        "SandboxApp: cube mesh {} verts / {} indices, aspect {:.3f}, cube model id={}, terrain {}x{} tiles",
        m_cubeMesh.vertexCount(),
        m_cubeMesh.indexCount(),
        aspect,
        m_cubeModelId,
        m_terrain.tilesX(),
        m_terrain.tilesZ());
    DE_LOG_INFO(LogCategory::Networking, "Sandbox net: Sandbox.exe -host   and   Sandbox.exe -join 127.0.0.1");
    DE_LOG_INFO(LogCategory::Networking, "Sandbox net: M opens Dev Tools (host / join / browse / debugger)");

    createPhysicsWorld();

    m_quest.clear();
    m_questBanners.clear();
    {
        SceneSpawnHost host{};
        host.world       = &world();
        host.physics     = m_physics.valid() ? &m_physics : nullptr;
        host.pins        = &pins();
        host.assets      = &assets();
        host.renderer    = &renderer();
        host.stampPrefix = "sandbox/level";
        instantiateSceneObjects(sceneBoxes.data(), static_cast<int>(sceneBoxes.size()), host);
        spawnBuildingExtras();
        world().each<ObstacleVolumeComponent>([&](Entity, ObstacleVolumeComponent& block) { m_chase.addObstacle(block.box); });
        std::vector<AABox3f> conceal;
        gatherConcealVolumes(world(), conceal);
        m_chase.ai().setConcealVolumes(std::move(conceal));
    }
    m_chaseOk = m_chase.init(renderer(), m_terrain, m_water, world(), pins(), assets());
    if (!m_sceneQuest.empty())
        startQuest(m_sceneQuest);
    m_play.clearFacility();
    if (m_play.facility().defs.empty())
    {
        std::string upgradeError;
        if (!loadUpgrades(resolveContentFile("upgrades.json"), m_play.facility().defs, &upgradeError))
            DE_LOG_WARN("Upgrades: {}", upgradeError);
    }
    if (m_chaseOk && m_chase.ai().awarenessEnabled())
    {
        GuardBarks  barks;
        std::string barkError;
        if (loadGuardBarks(resolveContentFile("ai/barks.json"), barks, &barkError))
            m_guardVoice.start(std::move(barks));
        else
            DE_LOG_WARN(LogCategory::AI, "Guard barks: {}", barkError);
    }
    uint32_t patchSeed = 1;
    for (const SceneObjectData& o : sceneBoxes)
    {
        if (!o.conceal)
            continue;
        const uint8_t     r   = static_cast<uint8_t>(Math::Clamp(o.color[0], 0.0f, 1.0f) * 255.0f);
        const uint8_t     g   = static_cast<uint8_t>(Math::Clamp(o.color[1], 0.0f, 1.0f) * 255.0f);
        const uint8_t     b   = static_cast<uint8_t>(Math::Clamp(o.color[2], 0.0f, 1.0f) * 255.0f);
        const std::string key = std::format("runtime:/scene/grass-{}", patchSeed);
        const Entity e = world().createEntity();
        world().emplace<TransformComponent>(e, Vector3f{ o.position.x, m_terrain.heightAtWorld(o.position.x, o.position.z), o.position.z }, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
        AssetRef<Model> patch = internAndUploadProceduralModel(renderer(), assets(), makeGrassPatch(o.scale.x, o.scale.z, patchSeed++),
                                                               internSolidMaterial(assets(), r, g, b, 255, "runtime:/scene/grass-mat"), key);
        if (!patch)
        {
            world().destroyEntity(e);
            continue;
        }
        ModelComponent mc{};
        mc.modelAssetID = patch->id;
        mc.castShadow   = true;
        setModelComponent(world(), pins(), assets(), e, mc);
    }
    for (const SceneObjectData& o : sceneLights)
    {
        const Entity e = world().createEntity();
        world().emplace<TagComponent>(e, "SceneLight");
        world().emplace<TransformComponent>(e, o.position, o.rotation, Vector3f{ 1.0f, 1.0f, 1.0f });
        auto& light        = world().emplace<LocalLightComponent>(e);
        light.type         = o.type == SceneObjectType::SpotLight ? LocalLightType::Spot : LocalLightType::Point;
        light.color        = Vector3f{ o.color[0], o.color[1], o.color[2] };
        light.intensity    = o.lightIntensity;
        light.range        = o.lightRange;
        light.innerConeDeg = o.lightInnerDeg;
        light.outerConeDeg = o.lightOuterDeg;
        light.enabled      = o.lightEnabled;
        light.castShadow   = o.lightCastShadow;
        if (!o.circuit.empty())
        {
            CircuitMemberComponent member{};
            member.circuit = o.circuit;
            world().emplace<CircuitMemberComponent>(e, member);
        }
    }
    for (const SceneObjectData& o : sceneBoxes)
    {
        if (o.conceal || !o.zone.empty() || !o.pickup.empty())
            continue;
        const uint8_t r = static_cast<uint8_t>(Math::Clamp(o.color[0], 0.0f, 1.0f) * 255.0f);
        const uint8_t g = static_cast<uint8_t>(Math::Clamp(o.color[1], 0.0f, 1.0f) * 255.0f);
        const uint8_t b = static_cast<uint8_t>(Math::Clamp(o.color[2], 0.0f, 1.0f) * 255.0f);
        const std::string key = std::format("runtime:/scene/box-{:02x}{:02x}{:02x}", r, g, b);
        MeshData          mesh;
        if (!CreateCube(mesh, 1.0f))
            continue;
        AssetRef<Model> box = internAndUploadProceduralModel(renderer(), assets(), std::move(mesh), internSolidMaterial(assets(), r, g, b, 255, key + "-mat"), key);
        if (!box)
            continue;
        const Entity e = world().createEntity();
        world().emplace<TransformComponent>(e, o.position, o.rotation, o.scale);
        ModelComponent mc{};
        mc.modelAssetID = box->id;
        mc.castShadow   = true;
        setModelComponent(world(), pins(), assets(), e, mc);
    }
    if (!m_chaseOk)
        DE_LOG_ERROR(LogCategory::AI, "SandboxApp: path chase init failed");
    else
    {
        addChaseObstaclesToPhysics();
        m_chase.ai().setHunterHitReactionSettings(defaultHunterHitReaction());
    }

    if (m_chase.walker().valid())
        attachLocalPlayer(m_chase.walker());
    m_shield = spawnPlayerShield(world(), pins(), assets(), renderer());
    if (m_chaseOk)
    {
        int hunterStamp = 0;
        int wolfStamp   = 0;
        for (const SceneObjectData& o : scenePawns)
        {
            const bool wolf = o.type == SceneObjectType::Wolf;
            TransformComponent xf{};
            xf.position = o.position;
            xf.rotation = o.rotation;
            const bool zeroScale = o.scale.x == 0.0f && o.scale.y == 0.0f && o.scale.z == 0.0f;
            xf.scale = zeroScale ? Vector3f{ 1.0f, 1.0f, 1.0f } : o.scale;
            xf.position.y = m_terrain.heightAtWorld(xf.position.x, xf.position.z) + 0.5f;
            if (!m_chase.ai().walkability().walkableWorld(xf.position.x, xf.position.z))
            {
                DE_LOG_WARN(LogCategory::AI, "SandboxApp: scene {} at ({:.1f}, {:.1f}) is not walkable",
                    wolf ? "wolf" : "hunter", xf.position.x, xf.position.z);
            }

            const Entity hunter = m_chase.spawnListedHunter(world(), pins(), assets(), xf);
            if (!hunter.valid())
                continue;
            const CharacterHost host{ world(), renderer(), assets(), pins(), audio(), m_ground };
            attachNpcVisual(host, hunter, wolf ? NpcKind::Wolf : NpcKind::Hunter, ModelAttach::AnimatedOnly);
            const int stamp = wolf ? wolfStamp++ : hunterStamp++;
            stampProceduralId(world(), hunter, std::format("sandbox/level/{}/{}", wolf ? "wolf" : "hunter", stamp), wolf ? "wolf" : "hunter");
            if (!wolf)
            {
                bindNpcDialogue(hunter, o.dialogue);
                const Entity torch = world().createEntity();
                world().emplace<TransformComponent>(torch, xf.position, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
                AttachedLightComponent att{};
                att.owner = hunter;
                auto& beam        = world().emplace<LocalLightComponent>(torch);
                beam.type         = LocalLightType::Spot;
                beam.color        = Vector3f{ 1.0f, 0.95f, 0.85f };
                beam.intensity    = att.intensity;
                beam.range        = att.range;
                beam.innerConeDeg = att.innerConeDeg;
                beam.outerConeDeg = att.outerConeDeg;
                beam.enabled      = false;
                world().emplace<AttachedLightComponent>(torch, att);
                stampProceduralId(world(), torch, std::format("sandbox/level/torch/{}", stamp), "torch");
                PatrolComponent patrol;
                for (const Vector2f& p : o.patrol)
                    patrol.points.push_back(Vector3f{ p.x, m_terrain.heightAtWorld(p.x, p.y), p.y });
                world().emplace<PatrolComponent>(hunter, std::move(patrol));
                if (AiAgentComponent* agent = world().get<AiAgentComponent>(hunter); agent && !o.squad.empty())
                    agent->squad = static_cast<uint32_t>(std::hash<std::string>{}(o.squad) | 1u);
            }
        }
        DE_LOG_INFO(LogCategory::AI, "SandboxApp: {} scene pawn(s)", m_chase.hunterCount());
    }
    if (!sceneSpawners.empty())
    {
        AssetRef<Model> marker;
        MeshData markerMesh;
        if (CreateCube(markerMesh, 1.0f))
        {
            marker = internAndUploadProceduralModel(renderer(), assets(), std::move(markerMesh),
                internSolidMaterial(assets(), 255, 158, 38, 255, "runtime:/scene/spawner-mat"), "runtime:/scene/spawner");
        }
        int spawnerStamp = 0;
        for (const SceneObjectData& o : sceneSpawners)
        {
            const bool zeroScale = o.scale.x == 0.0f && o.scale.y == 0.0f && o.scale.z == 0.0f;
            const Vector3f scale = zeroScale ? Vector3f{ 0.8f, 0.8f, 0.8f } : o.scale;
            TransformComponent xf{};
            xf.rotation = o.rotation;
            xf.scale    = scale;
            xf.position = o.position;
            xf.position.y = m_terrain.heightAtWorld(o.position.x, o.position.z) + 0.5f * std::fabs(scale.y);
            const Entity e = world().createEntity();
            world().emplace<TagComponent>(e, "Spawner");
            world().emplace<TransformComponent>(e, xf);
            SpawnerComponent spawner{};
            if (o.hasSpawner)
                spawner.desc = o.spawner;
            clampSpawnerDesc(spawner.desc);
            world().emplace<SpawnerComponent>(e, spawner);
            if (marker)
            {
                ModelComponent mc{};
                mc.modelAssetID = marker->id;
                mc.castShadow   = true;
                setModelComponent(world(), pins(), assets(), e, mc);
            }
            stampProceduralId(world(), e, std::format("sandbox/level/spawner/{}", spawnerStamp++), "spawner");
        }
        DE_LOG_INFO(LogCategory::AI, "SandboxApp: {} spawner(s)", sceneSpawners.size());
    }
    if (m_chaseOk)
    {
        bindCharacterPhysics();
        for (int i = 0; i < m_chase.hunterCount(); ++i)
        {
            const Entity e = m_chase.hunterEntity(i);
            if (isInteractGuard(world(), e))
                stockHunter(e);
        }
    }

    placeHealthPacks();
    if (const TransformComponent* start = m_chase.walker().valid() ? world().get<TransformComponent>(m_chase.walker()) : nullptr)
        stockMissingThrowablePickups(world(), pins(), assets(), renderer(), m_physics, start->position, worldHasThrowablePickup(world(), "rock"),
                                     worldHasThrowablePickup(world(), "bottle"));
    spawnHybridLocalLights();
    ensureSessionEntity();
    installSaveHost();
    m_save.captureBaseline(world());
    m_loadedScenePath = scenePath;
    m_sceneStarted    = true;
    return true;
}

void SandboxApp::onSplashFinished()
{
    m_spawnAge = 0.0f;
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

void SandboxApp::bindNpcDialogue(Entity npc, const std::string& scenePath)
{
    if (!npc.valid())
        return;
    std::string path = scenePath;
    if (path.empty())
        path = loadEntityMasterType("hunter").dialogue;
    if (path.empty())
        return;
    DialogueBindingComponent binding;
    binding.graph = std::move(path);
    if (DialogueBindingComponent* existing = world().get<DialogueBindingComponent>(npc))
        existing->graph = std::move(binding.graph);
    else
        world().emplace<DialogueBindingComponent>(npc, std::move(binding));
    if (!world().get<DialogueMemoryComponent>(npc))
        world().emplace<DialogueMemoryComponent>(npc);
}

Entity SandboxApp::pickTalkTarget()
{
    if (carriedBody(world()).valid())
        return {};
    const Entity player = possessedBody();
    const TransformComponent* playerXf = player.valid() ? world().get<TransformComponent>(player) : nullptr;
    if (!playerXf)
        return {};
    Vector3f look = m_viewCamera.GetLook();
    if (look.MagnitudeSqrd() < 1.0e-6f)
        return {};
    look.Normalize();
    const Vector3f camPos = m_viewCamera.GetPosition();

    Entity best{};
    float  bestDot = 0.94f;
    world().each<DialogueBindingComponent>([&](Entity npc, DialogueBindingComponent& binding) {
        if (binding.graph.empty() || npc == player)
            return;
        if (const AiAgentComponent* agent = world().get<AiAgentComponent>(npc); agent && agent->carried)
            return;
        const TransformComponent* npcXf = world().get<TransformComponent>(npc);
        if (!npcXf)
            return;
        const Vector3f flat{ npcXf->position.x - playerXf->position.x, 0.0f, npcXf->position.z - playerXf->position.z };
        float range = 2.5f;
        if (const AssetRef<DialogueGraphDef> graph = assets().tryLoadDialogueGraph(binding.graph))
            range = graph->talkRange;
        if (flat.MagnitudeSqrd() > range * range)
            return;
        Vector3f to = npcXf->position - camPos;
        const float len2 = to.MagnitudeSqrd();
        if (len2 < 1.0e-6f)
            return;
        to *= 1.0f / std::sqrt(len2);
        const float dot = look.Dot(to);
        if (dot > bestDot)
        {
            bestDot = dot;
            best    = npc;
        }
    });
    return best;
}

void SandboxApp::tryBeginTalk()
{
    if (m_dialogue.active())
        return;
    const Entity npc = pickTalkTarget();
    if (!npc.valid())
        return;
    const DialogueBindingComponent* binding = world().get<DialogueBindingComponent>(npc);
    if (!binding || binding->graph.empty())
        return;
    AssetRef<DialogueGraphDef> loaded = assets().tryLoadDialogueGraph(binding->graph);
    if (!loaded)
        return;
    m_talkGraph = std::move(loaded);
    m_dialogue.setGraph(m_talkGraph.get());
    m_dialogueSpeech.setServices(&assets(), &audio(), &world());
    m_dialogueSpeech.startSynth();
    m_dialogue.setAudio(m_dialogueSpeech.hook());
    m_dialogue.setFacts(m_session.valid() ? world().get<DialogueFactsComponent>(m_session) : nullptr);
    if (!m_dialogue.tryStart(world(), possessedBody(), npc, &m_quest))
        return;
    m_guardVoice.setSuppressed(npc, true, &audio());
    m_talkNpc = npc;
}

void SandboxApp::onUpdate(float dt)
{
    m_frameDt = dt;
    const bool menuFrame = m_menu.visible();
    if (!menuFrame)
        dt = m_play.scaleDt(dt);
    handleRuntimeCommands(dt);
    if (menuFrame || m_menu.visible())
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
    if (m_dialogue.locksWorld())
    {
        // This conversation waits. Pawns, barks, and the fly cam stay where they are.
    }
    else if (!m_gameplayPaused || m_stepGameplay)
    {
        syncPlayerMode();
        m_cloudTime += dt;
        m_env.tick(dt);
        m_water.tick(dt);
        if (m_muzzleTimer > 0.0f)
        {
            m_muzzleTimer -= dt;
            if (m_muzzleTimer <= 0.0f)
            {
                if (LocalLightComponent* light = m_muzzle.valid() ? world().get<LocalLightComponent>(m_muzzle) : nullptr)
                    light->enabled = false;
            }
            else if (TransformComponent* mxf = m_muzzle.valid() ? world().get<TransformComponent>(m_muzzle) : nullptr)
                mxf->position = m_viewCamera.GetPosition() + m_viewCamera.GetLook() * 0.8f;
        }
        m_play.tickWorld(dt);
        updateQuest(dt);
        m_blood.update(dt);
        tickParticleEmitters(world(), dt);
        updateWiggleAnim();
        tickAnimGraphs(world(), assets(), dt);
        m_play.tickHeldItems();
        tickLeaves(dt);
        m_stepGameplay = false;
    }
    else
    {
        m_leafRustle.stopAll(audio());
    }
    m_dialogueSpeech.setServices(&assets(), &audio(), &world());
    m_dialogue.setFacts(m_session.valid() ? world().get<DialogueFactsComponent>(m_session) : nullptr);
    m_dialogue.setUiBlocked(m_showDevTools || m_showInventory || m_showProfiler || m_showJournal || m_lootTarget.valid());
    m_dialogue.tick(world(), dt);
    if (m_talkNpc.valid() && !m_dialogue.active())
    {
        m_guardVoice.setSuppressed(m_talkNpc, false, &audio());
        m_talkNpc = {};
    }
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
    if (renderer().scenePath() == ScenePath::HybridDeferred)
        syncPlayerFlashlight(world(), possessedBody(), m_viewCamera.GetPosition(), m_viewCamera.GetLook(), m_viewCamera.GetRight(), m_viewCamera.GetUp(), m_offhand.lightOn);

    setViewAudioListener(audio(), m_viewCamera, &world(), possessedBody(), skillCatalog().enabled());
    tickSoundEmitters(world(), audio(), assets());
    m_scene.tickDecals(world(), dt);
}

void SandboxApp::onRender()
{
    if (!renderer().beginFrame())
    {
        requestQuit();
        return;
    }

    // The scene is still unbuilt while the start menu is waiting for a pick.
    // shadowBounds is safe on an empty grid; the water and post path is not.
    if (!m_sceneStarted)
    {
        if (m_imgui.isReady())
            m_imgui.beginFrame();
        m_menu.draw(renderer());
        if (m_imgui.isReady())
            m_imgui.render(renderer());
        renderer().endFrame();
        return;
    }

    m_foliagePipeline.setWind(m_wind, m_windClock.time, m_windClock.prevTime);

    if (m_imgui.isReady())
        m_imgui.beginFrame();

    auto* cmd = renderer().commandList();
    GpuResourceCache& gpu = renderer().gpuResources();
    if (m_skinRing.isValid())
        m_skinRing.beginFrame(renderer().frameIndex());
    if (m_skinnedInstances.isValid())
        m_skinnedInstances.beginFrame(renderer().frameIndex());

    PassId   playedPasses[24]{};
    uint32_t playedCount = 0;
    auto markPass = [&](PassId id) {
        if (playedCount < 24)
            playedPasses[playedCount++] = id;
    };

    const Frustum3f cameraCull(m_viewCamera.GetCullViewProj());
    const bool selectLocalShadows = renderer().scenePath() == ScenePath::HybridDeferred
        && renderer().debugState().localLights
        && renderer().debugState().lightingActive();
    m_scene.localShadows().update(world(), m_viewCamera, cameraCull, renderer().frameIndex(), selectLocalShadows);

    AABox3f sceneBounds = m_terrain.shadowBounds(m_viewCamera);
    world().each<MeshComponent>([&](Entity e, MeshComponent& mc) {
        if (mc.primitive == PrimitiveMesh::None)
            return;
        if (const TransformComponent* xf = world().get<TransformComponent>(e))
            sceneBounds.ExpandToInclude(xf->position);
    });
    world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
        if (!mc.visible)
            return;
        const TransformComponent* xf = world().get<TransformComponent>(e);
        if (!xf)
            return;
        const auto model = assets().getAs<Model>(mc.modelAssetID);
        if (!model || !model->bounds().IsValid())
            return;
        sceneBounds.ExpandToInclude(model->bounds().Transformed(makeWorldMatrix(*xf)));
    });
    if (m_chaseOk)
        m_chase.expandBounds(sceneBounds);
    m_shadows.update(
        m_viewCamera,
        m_env.lightDir(),
        sceneBounds,
        m_env.sunElevation(),
        m_env.weather.cloudCoverage,
        renderer().frameIndex());

    const bool instanceStatic = m_drawList.gpuReady() && renderer().debugState().instancedStatic && renderer().debugState().fill == DebugFill::Solid;
    const bool instanceSkinned = instanceStatic && m_skinnedInstances.isValid()
        && (renderer().scenePath() == ScenePath::HybridDeferred || m_skinnedInstances.forwardReady());
    std::unordered_set<EntityID> acceptedMesh;
    std::unordered_set<EntityID> acceptedModel;
    std::unordered_set<EntityID> acceptedSkinned;
    std::vector<int> rejectedShots;
    uint8_t shadowViewIndex = 1;
    if (instanceStatic)
    {
        m_drawList.beginFrame(renderer().frameIndex());
        m_drawList.setIndirect(renderer().debugState().instancedIndirect);
        world().each<MeshComponent>([&](Entity e, MeshComponent& mc) {
            if (world().has<ModelComponent>(e))
                return;
            if (const HealthComponent* hp = world().get<HealthComponent>(e); hp && !hp->health.alive())
                return;
            const Mesh* gpuMesh = sandboxPrimitiveMesh(mc.primitive, m_cubeMesh, m_crossMesh);
            if (!gpuMesh)
                return;
            const TransformComponent* xf = world().get<TransformComponent>(e);
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
            if (auto mat = assets().getAs<Material>(mc.matAssetID))
            {
                gpu.ensureMaterial(mat);
                item.material = mc.matAssetID;
                MeshGBufferConstants surface{};
                applyMaterialSurface(*mat, surface);
                item.color[0]   = surface.color[0];
                item.color[1]   = surface.color[1];
                item.color[2]   = surface.color[2];
                alphaModeMask   = surface.alphaModeMask;
            }
            if (const NetworkedComponent* nc = world().get<NetworkedComponent>(e))
                unpackRgba8(nc->colorRgba8, item.color);
            item.color[3]   = mc.emissive;
            item.castShadow = mc.castShadow;
            item.alphaMask  = alphaModeMask > 0.5f;
            if (m_drawList.submit(item))
                acceptedMesh.insert(e.id());
        });
        world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            if (!mc.visible || camouflageHides(e))
                return;
            const TransformComponent* xf = world().get<TransformComponent>(e);
            const auto model = assets().getAs<Model>(mc.modelAssetID);
            if (!xf || xf->scale.MagnitudeSqrd() < 1.0e-12f || !model || !model->valid() || !model->hasOpaque() || !gpu.ensureModel(model))
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
        if (WeaponLoadout* w = localWeapons())
        {
            const auto tracer = assets().getAs<Model>(m_tracerModelId);
            if (tracer && tracer->hasOpaque() && gpu.ensureModel(tracer))
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
    auto drawShadowCasters = [&](const Matrix4f& lightViewProj, const Frustum3f& casterFrustum, const Sphere3f* foliageSphere, bool beginFoliage) {
        m_terrain.drawDepth(cmd, &casterFrustum);
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
        world().each<MeshComponent>([&](Entity e, MeshComponent& mc) {
            if (world().has<ModelComponent>(e))
                return;
            if (!mc.castShadow)
                return;
            if (const HealthComponent* hp = world().get<HealthComponent>(e); hp && !hp->health.alive())
                return;
            const Mesh* gpuMesh = sandboxPrimitiveMesh(mc.primitive, m_cubeMesh, m_crossMesh);
            if (!gpuMesh)
                return;
            const TransformComponent* xf = world().get<TransformComponent>(e);
            if (!xf)
                return;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            const AABox3f local = sandboxPrimitiveBounds(mc.primitive);
            if (local.IsValid() && !casterFrustum.Intersects(local.Transformed(worldMat)))
                return;
            if (recordThisView && acceptedMesh.count(e.id()) != 0)
                return;
            drawShadowCaster(cmd, m_shadows.pipeline(), lightViewProj, worldMat, *gpuMesh);
        });
        world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            if (!mc.visible || camouflageHides(e))
                return;
            if (!mc.castShadow)
                return;
            const TransformComponent* xf = world().get<TransformComponent>(e);
            const auto model = assets().getAs<Model>(mc.modelAssetID);
            if (!xf || xf->scale.MagnitudeSqrd() < 1.0e-12f || !model || !model->valid())
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
        if (m_terrain.valid())
        {
            if (beginFoliage)
                m_foliagePipeline.beginDepthView(renderer(), assets(), m_foliagePrototypes, m_terrain, m_foliage.empty() ? nullptr : &m_foliage, m_foliageDensity, m_viewCamera, casterFrustum, foliageSphere);
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
            const Matrix4f camVp   = m_scene.beginCameraFrame(m_viewCamera, renderer());
            const Matrix4f camPrev = m_scene.havePrevViewProj() ? m_scene.prevViewProj() : camVp;
            float drawVp[16];
            float drawPrev[16];
            copyMatrix(drawVp, camVp);
            copyMatrix(drawPrev, camPrev);
            m_drawList.buildView(Frustum3f(m_viewCamera.GetCullViewProj()), drawVp, drawPrev, RenderPassKind::GBuffer, ViewId{ 0 });
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
        // An idle frame still has to leave this half of the array as a shader resource.
        localShadows.endCapture(cmd);
    }

    const bool deferred = renderer().scenePath() == ScenePath::HybridDeferred;
    const bool visionPicture = m_infrared > 0.0f || m_ultraviolet > 0.0f || m_echo > 0.0f;
    const float underDraw = (deferred && !visionPicture) ? Math::Clamp(m_underAmount, 0.0f, 1.0f) : 0.0f;
    const bool instanceColor = instanceStatic && deferred;
    const bool instanceForward = instanceStatic && !deferred && m_drawList.forwardReady();
    const Matrix4f viewProjEarly = m_scene.beginCameraFrame(m_viewCamera, renderer());
    (void)viewProjEarly;
    if (deferred)
    {
        markPass(PassId::GBuffer);
        renderer().bindGBuffer();
        renderer().clearGBuffer();
        const bool uvDraw = m_ultraviolet > 0.0f;
        const bool thermalDraw = !uvDraw && m_infrared > 0.0f;
        m_meshPipeline.setUltraviolet(uvDraw);
        m_skinnedPipeline.setUltraviolet(uvDraw);
        m_terrainPipeline.setUltraviolet(uvDraw);
        m_foliagePipeline.setUltraviolet(uvDraw);
        m_grassPipeline.setUltraviolet(uvDraw);
        m_drawList.setUltraviolet(uvDraw);
        m_skinnedInstances.setUltraviolet(uvDraw);
        m_meshPipeline.setThermal(thermalDraw);
        m_skinnedPipeline.setThermal(thermalDraw);
        m_terrainPipeline.setThermal(thermalDraw);
        m_foliagePipeline.setThermal(thermalDraw);
        m_grassPipeline.setThermal(thermalDraw);
        m_drawList.setThermal(thermalDraw);
        m_skinnedInstances.setThermal(thermalDraw);
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

    const Matrix4f  viewProj = m_viewCamera.GetViewProj();
    const Matrix4f  prevViewProj = m_scene.havePrevViewProj() ? m_scene.prevViewProj() : viewProj;
    const Frustum3f frustum(m_viewCamera.GetCullViewProj());
    const DebugFill fill     = renderer().debugState().fill;
    const Vector3f  camPos   = m_viewCamera.GetPosition();
    uint32_t        meshDraws = 0;

    const float skyExposure = deferred ? 1.0f : m_env.exposure();
    const float fogScale    = (renderer().debugState().lightingActive() ? 1.0f : 0.0f) * (1.0f - underDraw);
    const float ultraviolet = (deferred && m_ultraviolet > 0.0f) ? m_ultraviolet : 0.0f;
    const float thermal     = (deferred && !(m_ultraviolet > 0.0f)) ? m_infrared : 0.0f;
    m_skyPipeline.upload(renderer().frameIndex(), m_viewCamera, m_env, skyExposure, m_water.params().waterLevel, fogScale, thermal, ultraviolet);
    if (!deferred)
    {
        markPass(PassId::Sky);
        m_skyPipeline.draw(cmd, renderer().frameIndex(), &m_shadows);
    }

    AssetRef<Material> material;
    if (m_cube.valid())
    {
        if (auto* meshComp = world().get<MeshComponent>(m_cube))
            material = assets().getAs<Material>(meshComp->matAssetID);
        else if (const ModelComponent* mo = world().get<ModelComponent>(m_cube))
        {
            if (const auto model = assets().getAs<Model>(mo->modelAssetID))
            {
                if (const Model::Part* part = model->partAt(0))
                    material = part->material;
            }
        }
    }

    if (deferred)
    {
        {
            const GpuScope gbuffer(cmd, "GBuffer", ProfileColor::GBuffer);
            m_terrain.drawGBuffer(cmd, m_terrainPipeline, m_terrainMaterial, m_viewCamera, &frustum, &renderer().debugState(), &prevViewProj);
            if (m_grassPipeline.isValid() && m_grassField.params().enabled)
            {
                const GpuScope      grassScope(cmd, "Grass", ProfileColor::Grass);
                GrassFrameConstants grassFrame{};
                copyMatrix(grassFrame.viewProj, viewProj);
                copyMatrix(grassFrame.prevViewProj, prevViewProj);
                grassFrame.heightMetres = m_grassField.params().heightMetres;
                grassFrame.flexibility  = m_grassField.params().flexibility;
                grassFrame.faceEdge     = m_grassField.params().faceEdge;
                grassFrame.faceYaw      = m_grassField.params().faceYaw;
                grassFrame.faceLean     = m_grassField.params().faceLean;
                grassFrame.faceSteep    = m_grassField.params().faceSteep;
                grassFrame.cameraPos[0]   = camPos.x;
                grassFrame.cameraPos[1]   = camPos.y;
                grassFrame.cameraPos[2]   = camPos.z;
                grassFrame.cameraRight[0] = m_viewCamera.GetRight().x;
                grassFrame.cameraRight[1] = m_viewCamera.GetRight().y;
                grassFrame.cameraRight[2] = m_viewCamera.GetRight().z;
                grassFrame.cameraLook[0]  = m_viewCamera.GetLook().x;
                grassFrame.cameraLook[1]  = m_viewCamera.GetLook().y;
                grassFrame.cameraLook[2]  = m_viewCamera.GetLook().z;
                m_grassField.writeFrameInteraction(grassFrame);
                GrassLodSpan grassLods[kGrassLodCount]{};
                for (int lod = 0; lod < kGrassLodCount; ++lod)
                {
                    const GrassField::BladeSpan span = m_grassField.blades(lod);
                    grassLods[lod].blades            = span.data;
                    grassLods[lod].count             = span.count;
                    grassLods[lod].staleMask         = m_grassField.lodStaleMask(lod);
                }
                const uint32_t residentTiles = grassRegionBudget().residentTiles;
                std::vector<GrassTileDraw> grassTiles(residentTiles);
                const uint32_t grassDraws = m_grassField.visibleTiles(frustum, grassTiles.data(), residentTiles);
                // This slot has seen the CPU blades. Leave the other slot stale until its fence.
                if (m_grassPipeline.draw(cmd, renderer(), grassLods, m_grassField.tileWind(), m_grassField.tileWindCount(), grassFrame, grassTiles.data(), grassDraws))
                {
                    const uint32_t slot = renderer().frameIndex() % kGrassFrameSlots;
                    for (int lod = 0; lod < kGrassLodCount; ++lod)
                        m_grassField.clearLodStale(lod, slot);
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
            renderer().gpuResources().bindMaterial(cmd, *material, MeshPipeline::kRootAlbedoSrv);
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
        world().each<MeshComponent>([&](Entity e, MeshComponent& mc) {
            if (world().has<ModelComponent>(e))
                return;
            if (const HealthComponent* hp = world().get<HealthComponent>(e); hp && !hp->health.alive())
                return;
            const Mesh* gpuMesh = sandboxPrimitiveMesh(mc.primitive, m_cubeMesh, m_crossMesh);
            if (!gpuMesh)
                return;
            const TransformComponent* xf = world().get<TransformComponent>(e);
            if (!xf)
                return;
            if (auto mat = assets().getAs<Material>(mc.matAssetID))
            {
                gpu.ensureMaterial(mat);
                gpu.bindMaterial(cmd, *mat, MeshPipeline::kRootAlbedoSrv);
                applyMaterialSurface(*mat, gcb);
            }
            const Matrix4f worldMat  = makeWorldMatrix(*xf);
            if (instanceColor && acceptedMesh.count(e.id()) != 0)
            {
                m_prevWorldByEntity[e.id()] = worldMat;
                return;
            }
            const Matrix4f prevWorld = m_prevWorldByEntity.count(e.id()) ? m_prevWorldByEntity[e.id()] : worldMat;
            fillMeshGBufferXforms(gcb, worldMat, viewProj, prevViewProj, prevWorld);
            if (const NetworkedComponent* nc = world().get<NetworkedComponent>(e))
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
                world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            if (!mc.visible || camouflageHides(e))
                return;
            const TransformComponent* xf = world().get<TransformComponent>(e);
            const auto model = assets().getAs<Model>(mc.modelAssetID);
            if (!xf || xf->scale.MagnitudeSqrd() < 1.0e-12f || !model || !model->hasOpaque() || !gpu.ensureModel(model))
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
            if (m_terrain.valid())
                m_foliagePipeline.drawGBuffer(cmd, renderer(), assets(), m_foliagePrototypes, m_terrain, m_foliage.empty() ? nullptr : &m_foliage, m_foliageDensity, m_viewCamera, viewProj, prevViewProj, frustum);
        }

        m_meshPipeline.setUltraviolet(false);
        m_skinnedPipeline.setUltraviolet(false);
        m_terrainPipeline.setUltraviolet(false);
        m_foliagePipeline.setUltraviolet(false);
        m_grassPipeline.setUltraviolet(false);
        m_drawList.setUltraviolet(false);
        m_skinnedInstances.setUltraviolet(false);
        m_meshPipeline.setThermal(false);
        m_skinnedPipeline.setThermal(false);
        m_terrainPipeline.setThermal(false);
        m_foliagePipeline.setThermal(false);
        m_grassPipeline.setThermal(false);
        m_drawList.setThermal(false);
        m_skinnedInstances.setThermal(false);

        LightingConstants lc{};
        copyMatrix(lc.invViewProj, viewProj.Inverse());
        lc.cameraPos[0]     = camPos.x;
        lc.cameraPos[1]     = camPos.y;
        lc.cameraPos[2]     = camPos.z;
        lc.lightDirWS[0]    = m_env.lightDir().x;
        lc.lightDirWS[1]    = m_env.lightDir().y;
        lc.lightDirWS[2]    = m_env.lightDir().z;
        lc.lighting         = renderer().debugState().lightingActive() ? 1.0f : 0.0f;
        lc.lightColor[0]    = m_env.lightColor().x;
        lc.lightColor[1]    = m_env.lightColor().y;
        lc.lightColor[2]    = m_env.lightColor().z;
        {
            const Vector3f pbrSun = pbrSunLightColor(m_env.lightColor());
            lc.pbrLightColor[0]   = pbrSun.x;
            lc.pbrLightColor[1]   = pbrSun.y;
            lc.pbrLightColor[2]   = pbrSun.z;
        }
        lc.emissiveGain     = 4.0f;
        lc.ambientColor[0]  = m_env.ambientColor().x;
        lc.ambientColor[1]  = m_env.ambientColor().y;
        lc.ambientColor[2]  = m_env.ambientColor().z;
        FogGpu fog = makeFogGpu(&m_env, m_water.params().waterLevel, lc.lighting > 0.5f);
        const float air = 1.0f - underDraw;
        fog.fogDensity *= air;
        fog.heightFogDensity *= air;
        fog.volumetricFogDensity *= air;
        fillFogHeightMap(fog, &m_terrain.coarse());
        applyFogToLighting(lc, fog);
        fillIblLightingConstants(lc, m_ibl, renderer().debugState().iblEnabled, renderer().debugState().iblDebug, iblGpuReady(renderer().gpuResources(), m_iblImageId));
        lc.iblNight = 1.0f - m_env.dayFactor();
        lc.thermal  = m_ultraviolet > 0.0f ? -m_ultraviolet : m_infrared;
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
        resolve.ssrEnv       = &m_env;
        resolve.lighting     = &lc;
        m_scene.resolveDeferred(cmd, renderer(), world(), m_viewCamera, resolve);

        renderer().bindHdr(true);
        markPass(PassId::Sky);
        m_skyPipeline.draw(cmd, renderer().frameIndex(), &m_shadows);
    }
    else
    {
        if (!m_loggedForwardGrassSkip && m_grassPipeline.isValid() && m_grassField.params().enabled)
        {
            m_loggedForwardGrassSkip = true;
            DE_LOG_INFO(LogCategory::Render, "SandboxApp: forward frame skips grass");
        }
        const GpuScope forward(cmd, "Forward Opaque", ProfileColor::ForwardOpaque);
        m_terrain.draw(
            cmd, m_terrainPipeline, m_terrainMaterial, m_viewCamera, &frustum, &m_env, &m_shadows,
            &renderer().debugState());

        m_meshPipeline.bind(cmd, fill);
        if (material && material->isValid())
            renderer().gpuResources().bindMaterial(cmd, *material, MeshPipeline::kRootAlbedoSrv);
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
        cb.lightDirWS[0] = m_env.lightDir().x;
        cb.lightDirWS[1] = m_env.lightDir().y;
        cb.lightDirWS[2] = m_env.lightDir().z;
        cb.ambientScale  = 0.22f;
        {
            const Vector3f pbrSun = pbrSunLightColor(m_env.lightColor());
            cb.lightColor[0]      = pbrSun.x;
            cb.lightColor[1]      = pbrSun.y;
            cb.lightColor[2]      = pbrSun.z;
        }
        cb.cameraPos[0]  = camPos.x;
        cb.cameraPos[1]  = camPos.y;
        cb.cameraPos[2]  = camPos.z;
        cb.lighting      = renderer().debugState().lightingActive() ? 1.0f : 0.0f;
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
                    renderer().gpuResources().bindMaterial(cmd, *material, MeshPipeline::kRootAlbedoSrv);
                m_shadows.bindReceiverCbv(cmd, MeshPipeline::kRootShadowCbv);
            }
            world().each<MeshComponent>([&](Entity e, MeshComponent& mc) {
            if (world().has<ModelComponent>(e))
                return;
            if (const HealthComponent* hp = world().get<HealthComponent>(e); hp && !hp->health.alive())
                return;
            const Mesh* gpuMesh = sandboxPrimitiveMesh(mc.primitive, m_cubeMesh, m_crossMesh);
            if (!gpuMesh)
                return;
            const TransformComponent* xf = world().get<TransformComponent>(e);
            if (!xf)
                return;
            if (instanceForward && acceptedMesh.count(e.id()) != 0)
                return;
            if (auto mat = assets().getAs<Material>(mc.matAssetID))
            {
                gpu.ensureMaterial(mat);
                gpu.bindMaterial(cmd, *mat, MeshPipeline::kRootAlbedoSrv);
                applyMaterialSurface(*mat, cb);
            }
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            copyMatrix(cb.worldViewProj, worldMat * viewProj);
            copyMatrix(cb.world, worldMat);
            if (const NetworkedComponent* nc = world().get<NetworkedComponent>(e))
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
            world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            if (!mc.visible || camouflageHides(e))
                return;
            const TransformComponent* xf = world().get<TransformComponent>(e);
            const auto model = assets().getAs<Model>(mc.modelAssetID);
            if (!xf || xf->scale.MagnitudeSqrd() < 1.0e-12f || !model || !model->hasOpaque() || !gpu.ensureModel(model))
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
                if (accepted && skinnedListed)
                    return;
                if (accepted)
                    drawSkinnedModelForwardSkinnedOnly(cmd, gpu, m_skinnedPipeline, m_meshPipeline, m_shadows, m_skinRing, *model, *pose, worldMat, viewProj, cb, fill);
                else if (skinnedListed)
                    drawModelForward(cmd, gpu, m_meshPipeline, m_shadows, *model, false, worldMat, viewProj, cb, fill);
                else
                    drawSkinnedModelForward(cmd, gpu, m_skinnedPipeline, m_meshPipeline, m_shadows, m_skinRing, *model, false, *pose, worldMat, viewProj, cb, fill);
            }
            else if (!(instanceForward && acceptedModel.count(e.id()) != 0))
                drawModelForward(cmd, gpu, m_meshPipeline, m_shadows, *model, false, worldMat, viewProj, cb, fill);
        });
        }
    }

    D3D12_GPU_VIRTUAL_ADDRESS waterLightsVa   = m_localLightGpu.isValid() ? m_localLightGpu.dummyGpuVa() : 0;
    uint32_t                  waterLightCount = 0;
    uint32_t                  waterIndex[kWaterLocalLightMax]{};
    if (renderer().debugState().localLights && renderer().debugState().lightingActive() && m_localLightGpu.isValid())
    {
        LocalLightCullInput in{};
        in.frustum    = &frustum;
        in.cameraPos  = camPos;
        in.cameraLook = m_viewCamera.GetLook();
        in.nearZ      = m_viewCamera.GetNearZ();
        in.viewportW  = renderer().width();
        in.viewportH  = renderer().height();
        in.viewProj      = &viewProj;
        in.localShadows  = &m_scene.localShadows();
        LocalLightDrawLists lists{};
        if (gatherLocalLights(world(), in, lists) && lists.count > 0)
        {
            m_localLightGpu.upload(renderer().frameIndex(), lists);
            waterLightCount = lists.waterCount;
            std::memcpy(waterIndex, lists.waterIndex, sizeof(waterIndex));
            waterLightsVa = m_localLightGpu.lightsGpuVa();
        }
    }

    ID3D12DescriptorHeap*         heightHeap = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE   heightGpu{};
    if (m_terrain.heightTexture().valid())
    {
        heightHeap = m_terrain.heightTexture().srvHeap();
        heightGpu  = m_terrain.heightTexture().gpuHandle();
    }
    else if (renderer().lightingHeap())
    {
        heightHeap = renderer().lightingHeap();
        heightGpu  = renderer().heightTableGpu();
    }
    if (renderer().hasSceneBuffers())
    {
        markPass(PassId::SsrCapture);
        m_scene.captureSsrSceneColor(cmd, renderer());
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
    const bool waterSheet = m_water.wetChunkCount() > 0;
    if (waterSheet || !m_streams.empty())
    {
        markPass(PassId::Water);
        const GpuScope waterScope(cmd, "Water", ProfileColor::Water);
        uint32_t drawIndex = 0;
        if (waterSheet)
        {
            m_water.draw(
                cmd,
                m_waterPipeline,
                m_viewCamera,
                &frustum,
                &m_env,
                &renderer().debugState(),
                waterLightsVa,
                waterLightCount,
                waterIndex,
                renderer().frameIndex(),
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
        for (const SandboxStream& stream : m_streams)
        {
            if (drawIndex >= WaterPipeline::kMaxWaterDrawsPerFrame)
            {
                DE_LOG_WARN(LogCategory::Render, "SandboxApp: water draw cap {} reached", WaterPipeline::kMaxWaterDrawsPerFrame);
                break;
            }
            WaterParams streamParams = m_water.params();
            streamParams.flowSpeed = stream.flowSpeed;
            if (drawStreamRibbon(
                    cmd,
                    m_waterPipeline,
                    stream.mesh,
                    stream.bounds,
                    streamParams,
                    m_viewCamera,
                    &frustum,
                    &renderer().debugState(),
                    m_water.time(),
                    renderer().frameIndex(),
                    drawIndex,
                    heightHeap,
                    heightGpu,
                    &m_shadows,
                    waterSceneColor,
                    waterDepth,
                    waterSsr,
                    &m_terrain.coarse(),
                    &m_env,
                    thermal,
                    ultraviolet))
                ++drawIndex;
        }
    }

    if (renderer().hasGBuffer())
    {
        markPass(PassId::Clouds);
        renderer().bindHdrDepthRead();
        CloudVolumeFrame cf{};
        cf.sunDir       = m_env.lightDir();
        cf.sunColor     = m_env.lightColor();
        cf.ambientColor = m_env.ambientColor();
        cf.time         = m_cloudTime;
        cf.lod          = m_cloudLod;
        m_scene.drawCloudVolumes(cmd, renderer(), world(), m_viewCamera, viewProj, cf);
    }

    if (m_camouflage && renderer().hasSceneBuffers() && m_scene.camouflage().isValid())
    {
        markPass(PassId::Camouflage);
        const Entity body = possessedBody();
        const TransformComponent* xf = body.valid() ? world().get<TransformComponent>(body) : nullptr;
        const ModelComponent* mc = body.valid() ? world().get<ModelComponent>(body) : nullptr;
        const auto model = (mc && xf) ? assets().getAs<Model>(mc->modelAssetID) : AssetRef<Model>{};
        if (xf && model && gpu.ensureModel(model))
        {
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            AnimGraphComponent* ag = world().get<AnimGraphComponent>(body);
            const AnimPose* pose = model->skinned() ? skinnedPose(*model, ag) : nullptr;
            if (ag)
            {
                ag->prevWorld      = worldMat;
                ag->prevWorldValid = true;
            }
            m_scene.camouflage().drawModel(cmd, renderer(), gpu, m_skinRing, *model, pose, worldMat, viewProj, camPos, m_cloudTime, fill);
        }
    }

    {
        markPass(PassId::Translucent);
        const GpuScope translucent(cmd, "Translucent", ProfileColor::Translucent);
        MeshFrameConstants lit{};
        lit.lightDirWS[0] = m_env.lightDir().x;
        lit.lightDirWS[1] = m_env.lightDir().y;
        lit.lightDirWS[2] = m_env.lightDir().z;
        lit.ambientScale  = 0.22f;
        {
            const Vector3f pbrSun = pbrSunLightColor(m_env.lightColor());
            lit.lightColor[0]     = pbrSun.x;
            lit.lightColor[1]     = pbrSun.y;
            lit.lightColor[2]     = pbrSun.z;
        }
        lit.cameraPos[0]  = camPos.x;
        lit.cameraPos[1]  = camPos.y;
        lit.cameraPos[2]  = camPos.z;
        lit.lighting      = renderer().debugState().lightingActive() ? 1.0f : 0.0f;
        std::vector<Entity> translucentEntities;
        std::vector<TranslucentDraw> translucentOrder;
        world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            if (!mc.visible || camouflageHides(e))
                return;
            const TransformComponent* xf = world().get<TransformComponent>(e);
            const auto model = assets().getAs<Model>(mc.modelAssetID);
            if (!xf || xf->scale.MagnitudeSqrd() < 1.0e-12f || !model || !model->hasTranslucent() || !gpu.ensureModel(model))
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
        sortTranslucent(translucentOrder.data(), static_cast<uint32_t>(translucentOrder.size()), m_viewCamera.GetView());
        for (const TranslucentDraw& draw : translucentOrder)
        {
            const Entity e = translucentEntities[draw.user];
            const ModelComponent* mc = world().get<ModelComponent>(e);
            const TransformComponent* xf = world().get<TransformComponent>(e);
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
        }
    }

    if (m_chaseOk && (m_demoScene || m_showDevTools))
    {
        const GpuScope paths(cmd, "Path Debug", ProfileColor::DebugOverlay);
        m_chase.drawPaths(cmd, renderer(), viewProj);
    }
    drawSkeletonOverlay(cmd, viewProj);
    m_scene.drawDecalVolumes(cmd, renderer(), m_chase.linePipeline(), viewProj);

    {
        markPass(PassId::Particles);
        const GpuScope particles(cmd, "Particles", ProfileColor::Particles);
        m_particles.beginFrame(renderer().frameIndex());
    if (m_blood.aliveCount() > 0)
        m_particles.draw(cmd, m_viewCamera, m_blood, false);
    world().each<ParticleEmitterComponent>([&](Entity, ParticleEmitterComponent& pe) {
        if (!pe.runtime)
            return;
        const Material* sprite = nullptr;
        if (AssetRef<Material> mat = assets().getAs<Material>(pe.matAssetID))
        {
            gpu.ensureMaterial(mat);
            sprite = mat.get();
        }
        m_particles.draw(cmd, m_viewCamera, *pe.runtime, pe.runtime->desc().additiveBlend, sprite);
    });
    if (WeaponLoadout* wFx = localWeapons(); wFx && wFx->projectile().impactEmitter().aliveCount() > 0)
        m_particles.draw(cmd, m_viewCamera, wFx->projectile().impactEmitter(), true);
    m_leafRenderer.draw(cmd, m_viewCamera, m_leafField);
    }

    if (!deferredDecals())
        m_bloodSplats.draw(cmd, m_viewCamera);

    if (underDraw > 0.0f && renderer().hasSceneBuffers() && m_scene.underwater().isValid())
    {
        markPass(PassId::Underwater);
        UnderwaterFrame under{};
        copyMatrix(under.invViewProj, viewProj.Inverse());
        under.cameraPos[0] = camPos.x;
        under.cameraPos[1] = camPos.y;
        under.cameraPos[2] = camPos.z;
        under.waterLevel   = m_water.params().waterLevel;
        under.amount       = underDraw;
        m_scene.underwater().draw(cmd, renderer(), under);
        renderer().bindHdr(true);
        m_water.draw(
            cmd,
            m_waterPipeline,
            m_viewCamera,
            &frustum,
            &m_env,
            &renderer().debugState(),
            waterLightsVa,
            waterLightCount,
            waterIndex,
            renderer().frameIndex(),
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
        const bool aces = useAcesTonemap(renderer());
        TonemapSettings post = playerPostFx();
        post.mode            = aces ? 1.0f : 0.0f;
        float exposure       = m_env.exposure();
        m_autoExposureResult = {};
        m_autoExposureResult.finalExposure = exposure;
        const bool deferredMeter = renderer().scenePath() == ScenePath::HybridDeferred && m_scene.autoExposure().isValid();
        if (deferredMeter)
        {
            // Resize drops the 1×1. Zero the correction before this frame can apply a stale sample.
            if (m_scene.syncAutoExposureSize(renderer()))
                resetAutoExposure(m_autoExposureState);
            // Infrared, ultraviolet, echolocation, and the underwater column swing the meter. Hold the correction while any is on.
            const bool holdExposure = m_infrared > 0.0f || m_ultraviolet > 0.0f || m_echo > 0.0f || m_underAmount > 0.0f;
            const float measured = (m_autoExposure.mode == ExposureMode::Auto && !holdExposure)
                ? m_scene.autoExposure().readMeasuredLuma(renderer().frameIndex())
                : 0.0f;
            m_autoExposureResult = adaptExposure(m_autoExposureState, measured, exposure, m_frameDt, m_autoExposure);
            exposure             = m_autoExposureResult.finalExposure;
        }
        post.exposure = exposure;
        markPass(PassId::Post);
        const bool echoImage = deferred && m_echo > 0.0f && renderer().hasGBuffer() && m_scene.echo().isValid();
        if (echoImage)
        {
            EchoPipeline& echo = m_scene.echo();
            bool capturedThisFrame = false;
            if (m_echoCapture)
            {
                EchoSnapshotFrame snap{};
                copyMatrix(snap.invViewProj, viewProj.Inverse());
                snap.cameraPos[0] = camPos.x;
                snap.cameraPos[1] = camPos.y;
                snap.cameraPos[2] = camPos.z;
                if (echo.capture(cmd, renderer(), snap))
                {
                    m_echoChirpViewProj = viewProj;
                    m_echoChirpPos      = camPos;
                    m_echoHaveChirp     = true;
                    m_echoCapture       = false;
                    capturedThisFrame   = true;
                }
            }
            if (m_echoHaveChirp && echo.hasSnapshot())
            {
                EchoCompositeFrame comp{};
                copyMatrix(comp.currInvViewProj, viewProj.Inverse());
                copyMatrix(comp.chirpViewProj, m_echoChirpViewProj);
                comp.chirpCamera[0] = m_echoChirpPos.x;
                comp.chirpCamera[1] = m_echoChirpPos.y;
                comp.chirpCamera[2] = m_echoChirpPos.z;
                float radius = Math::Min(m_echoChirpAge * m_echoChirp.speedMetersPerSec, m_echoChirp.rangeMeters);
                if (capturedThisFrame)
                    radius = Math::Max(radius, EchoPipeline::kEdgeMeters);
                comp.radius   = radius;
                comp.strength = m_echo;
                comp.maxRange = m_echoChirp.rangeMeters;
                echo.composite(cmd, renderer(), comp);
            }
        }
        const bool holdExposure = m_infrared > 0.0f || m_ultraviolet > 0.0f || m_echo > 0.0f || m_underAmount > 0.0f;
        m_scene.applyPost(renderer(), cmd, viewProj, post, deferredMeter && m_autoExposure.mode == ExposureMode::Auto && !holdExposure, m_echo > 0.0f);
    }

    m_scene.endCameraFrame(m_viewCamera, viewProj);

    Health* hudHp = localHealth();
    const Entity hudBody = possessedBody();
    const HudTagComponent* hudTag = hudBody.valid() ? world().get<HudTagComponent>(hudBody) : nullptr;
    const bool drawHealth = !m_menu.visible() && hudHp && hudTag && hudTag->kind == HudKind::HealthBar;
    const bool drawCrosshair = !m_menu.visible() && hudHp && hudHp->alive() && !m_gameplayPaused && !m_dialogue.locksWorld();
    if (drawHealth || drawCrosshair)
    {
        const GpuScope hud(cmd, "HUD", ProfileColor::Hud);
        if (drawHealth)
            m_healthHud.draw(cmd, renderer().width(), renderer().height(), hudHp->ratio());
        if (drawCrosshair)
        {
            bool throwableSlot = false;
            if (hudBody.valid())
            {
                if (const ThrowableBeltComponent* carried = world().get<ThrowableBeltComponent>(hudBody))
                    throwableSlot = carried->belt.any();
            }
            const WeaponKind hudKind = localWeapons() ? localWeapons()->activeKind() : WeaponKind::Melee;
            m_crosshair.draw(cmd, renderer().width(), renderer().height(), hudKind, throwableSlot);
        }
    }

    renderer().stats().drawCalls = m_terrain.lastDrawCalls() + m_water.lastDrawCalls() + meshDraws + 1;
    renderer().stats().triangles =
        m_terrain.lastTriangles() + m_water.lastTriangles() + meshDraws * (m_cubeMesh.indexCount() / 3);

    const bool ssaoTile = renderer().hasGBuffer() && renderer().debugState().ssaoDebug == 1;
    const bool ssrTile  = renderer().hasGBuffer() && renderer().debugState().ssrDebug != 0;
    if (cmd && m_debugOverlay.isValid()
        && (m_showShadowMaps || renderer().debugState().shadowMapTiles || m_showDepth || m_showGBuffer || m_showVelocity || ssaoTile || ssrTile))
        markPass(PassId::Overlay);
    drawDebugOverlays(cmd);
    m_menu.draw(renderer());
    if (m_imgui.isReady() && !m_menu.visible())
    {
        drawNpcInfoOverlay();
        drawGuardAwareness();
        drawGuardSpeech();
        drawDialogue();
        drawArmorHud();
        drawStaminaBar();
        drawQuestHud();
        drawUpgradeHint();
        drawAlarm();
        drawBreathBar();
        drawPickupLabels();
        drawTargetMarker();
        drawDefenseText();
        if (m_showProfiler)
            drawProfiler();
        if (m_gameplayPaused)
            drawPauseOverlay();
        if (m_showDevTools)
            drawDevTools();
        if (m_showJournal)
            drawJournal();
        if (m_showUpgrades)
            drawUpgrades();
        if (m_showInventory)
            drawInventory();
        if (m_lootTarget.valid())
            drawLoot();
        m_imgui.render(renderer());
    }
    else if (m_imgui.isReady() && m_menu.visible())
    {
        // ImGui frame was begun; submit an empty frame so the backend stays paired.
        m_imgui.render(renderer());
    }
    uint32_t tableCount = 0;
    const PassNode* table = deferred ? sandboxDeferredPasses(tableCount) : sandboxForwardPasses(tableCount);
    (void)passSequenceOk(table, tableCount, playedPasses, playedCount);
    renderer().endFrame();
}

void SandboxApp::drawDebugOverlays(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd || !m_debugOverlay.isValid())
        return;
    const bool ssaoTile = renderer().hasGBuffer() && renderer().debugState().ssaoDebug == 1;
    const bool ssrTile  = renderer().hasGBuffer() && renderer().debugState().ssrDebug != 0;
    if (!m_showShadowMaps && !renderer().debugState().shadowMapTiles && !m_showDepth && !m_showGBuffer && !m_showVelocity && !ssaoTile && !ssrTile)
        return;

    const GpuScope overlay(cmd, "Debug Overlay", ProfileColor::DebugOverlay);
    // Unbind the DSV so we can sample the scene depth. Do not rebind it afterwards
    // while it remains PIXEL_SHADER_RESOURCE (endFrame does not write depth).
    renderer().bindColorTargetOnly();
    m_debugOverlay.beginFrame(renderer().frameIndex());

    const LONG sw = static_cast<LONG>(renderer().width());
    const LONG sh = static_cast<LONG>(renderer().height());
    const LONG pad = 12;
    LONG tile = sh / 5;
    if (tile < 96)
        tile = 96;
    if (tile > 220)
        tile = 220;

    if (m_showGBuffer && renderer().hasGBuffer())
    {
        renderer().transitionAlbedo(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        renderer().transitionAttrib(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        const LONG y = pad;
        const D3D12_CPU_DESCRIPTOR_HANDLE albedo = renderer().albedoSrvCpu();
        const D3D12_CPU_DESCRIPTOR_HANDLE attrib = renderer().attribSrvCpu();
        if (albedo.ptr != 0)
            m_debugOverlay.drawColor(cmd, renderer().device(), albedo, pad, y, tile, tile);
        if (attrib.ptr != 0)
            m_debugOverlay.drawColor(cmd, renderer().device(), attrib, pad + tile + 8, y, tile, tile);
    }
    if ((m_showGBuffer || m_showVelocity) && renderer().hasGBuffer())
    {
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

    if (m_showDepth && renderer().depthResource())
    {
        renderer().transitionDepth(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        const LONG x = pad;
        const LONG y = sh - pad - tile;
        m_debugOverlay.draw2D(
            cmd, renderer().device(), renderer().depthSrvCpu(), x, y, tile, tile, 1.0f, true);
    }

    if (m_showShadowMaps && m_shadows.isValid())
    {
        const int n = m_shadows.cascadeCount();
        LONG x0 = pad;
        if (m_showDepth)
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
                renderer().device(),
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

    if (renderer().debugState().shadowMapTiles && m_scene.localShadows().isValid())
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

void SandboxApp::onShutdown()
{
    m_guardVoice.stop();
    network().shutdown();
    renderer().waitForGpu();
    m_foliagePipeline.destroy();
    m_grassPipeline.destroy();
    m_menu.shutdown(renderer());
    m_scene.shutdown();
    m_imgui.shutdown(renderer());
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
    m_particles.destroy(renderer());
    m_leafRenderer.destroy(renderer());
    m_bloodSplats.destroy(renderer());
    DE_LOG_INFO("SandboxApp: shutdown");
}
