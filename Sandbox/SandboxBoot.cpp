#include "SandboxApp.h"
#include "SandboxInternal.h"

#include "AI/AiComponents.h"
#include "Content/ArchetypeParams.h"
#include "Content/ArchetypeSpawn.h"
#include "Content/ContentDb.h"
#include "Content/SceneObjectSpawn.h"
#include "Network/ArchetypeNet.h"
#include "Play/SpawnHost.h"
#include "Play/GuardVoice.h"

#include "Assets/Image.h"
#include "Assets/Material.h"
#include "Assets/Model.h"
#include "Audio/SoundComponents.h"
#include "Character/ShieldView.h"
#include "Core/EntityPins.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/ThrowableBelt.h"
#include "Gameplay/Upgrades.h"
#include "Math/MathHelper.h"
#include "Media/MovieCue.h"
#include "Particles/StatusFxDriver.h"
#include "Save/HostSync.h"
#include "Weapons/WeaponLoadout.h"
#include "Play/AttachedLights.h"
#include "Play/Carry.h"
#include "Play/CharacterAttach.h"
#include "Play/HunterTick.h"
#include "Play/Interaction.h"
#include "Render/AutoExposure.h"
#include "Render/GpuResourceCache.h"
#include "Render/GpuUpload.h"
#include "Render/MeshGen.h"
#include "Save/PersistentId.h"
#include "Scene/EntityMaster.h"
#include "Scene/SceneFile.h"
#include "Scene/SceneSpawn.h"
#include "Sky/CloudVolume.h"
#include "Terrain/FoliageSpawn.h"
#include "Terrain/SplatMap.h"
#include "Terrain/WorldEngineMap.h"
#include "Water/WaterBody.h"
#include "Water/WaterStream.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace Dark;
using namespace Math;
using namespace Terrain;
using namespace Audio;

namespace
{

// The picture is frozen for this whole call. Log when that freeze passes the live-slice cap.
struct HitchClock
{
    const char* name;
    std::chrono::steady_clock::time_point start;

    explicit HitchClock(const char* sliceName)
        : name(sliceName)
        , start(std::chrono::steady_clock::now())
    {
    }

    ~HitchClock()
    {
        const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (ms > 8.0f)
            DE_LOG_WARN("SandboxApp: boot slice {} took {:.1f} ms", name, ms);
    }
};

uint32_t sceneReplicaColor(const float color[4])
{
    auto channel = [](float v) -> uint32_t {
        if (v < 0.0f)
            v = 0.0f;
        if (v > 1.0f)
            v = 1.0f;
        return static_cast<uint32_t>(v * 255.0f + 0.5f);
    };
    return (channel(color[0]) << 24) | (channel(color[1]) << 16) | (channel(color[2]) << 8) | channel(color[3]);
}

void registerSceneReplica(NetworkSystem& network, World& world, Entity entity, std::string_view archetypeId, const float color[4])
{
    if (!entity.valid())
        return;
    NetPrefab prefab = NetPrefab::Unknown;
    uint16_t index = 0;
    if (!archetypeNetRegistration(archetypeId, prefab, index))
        return;
    network.registerEntity(world, entity, prefab, ClientId::Host, sceneReplicaColor(color), index);
}

bool enqueueBootIo(JobSystem& jobs, const char* name, JobStatus (*fn)(void*), void* user)
{
    JobDesc desc{};
    desc.name   = name;
    desc.lane   = JobLane::Io;
    desc.stream = JobStream::Scene;
    desc.reads  = AccessFileRead;
    desc.fn     = fn;
    desc.user   = user;
    return jobs.enqueue(desc);
}

float sandboxBootGroundY(void* user, float x, float z)
{
    const TerrainGrid* terrain = static_cast<const TerrainGrid*>(user);
    return terrain ? terrain->heightAtWorld(x, z) : 0.0f;
}

constexpr int kBootSpawnBatch = 32;

bool zeroScale(const Vector3f& scale)
{
    return scale.x == 0.0f && scale.y == 0.0f && scale.z == 0.0f;
}

} // namespace

struct SandboxApp::SandboxBootLoad
{
    enum class TerrainKind : uint8_t
    {
        None,
        WorldEngine,
        Grid,
        HeightFile,
    };

    enum class SpawnPhase : uint8_t
    {
        Classify,
        Clouds,
        Shadow,
        Camera,
        Cube,
        Physics,
        Objects,
        Buildings,
        Obstacles,
        Chase,
        Quest,
        Grass,
        Lights,
        Boxes,
        ChasePhysics,
        Player,
        Shield,
        Pawns,
        Models,
        Archetypes,
        SpawnerMarker,
        Spawners,
        MovieMarker,
        Movies,
        Bind,
        Packs,
        Throwables,
        Hybrid,
        Session,
    };

    bool                      runScene = false;
    std::filesystem::path     scenePath;
    AssetManager*             assets = nullptr;

    bool                      sceneArmed = false;
    bool                      sceneRunning = false;
    bool                      sceneOk = false;
    bool                      headerApplied = false;
    uint64_t                  sceneGeneration = 0;
    SceneFileData             scene;

    TerrainKind               kind = TerrainKind::None;
    bool                      terrainArmed = false;
    bool                      terrainRunning = false;
    std::atomic<bool>         terrainFinished{ false };
    JobStatus                 terrainStatus = JobStatus::Failed;
    std::filesystem::path     worldDir;
    std::filesystem::path     heightPath;
    WorldEngineLoadDesc       importDesc{};
    WorldEngineMaps           worldMaps;
    HeightMap                 loadedHeight;
    TerrainGridDesc           gridDesc{};
    int                       chunkCells = 64;
    bool                      loadedScene = false;
    bool                      worldEngine = false;
    SplatMap                  matSplat;

    bool                      waterReady = false;
    float                     waterLevel = 0.0f;

    bool                      foliageConsidered = false;
    bool                      foliageArmed = false;
    bool                      foliageRunning = false;
    std::atomic<bool>         foliageFinished{ false };
    JobStatus                 foliageStatus = JobStatus::Failed;
    FoliageSpawnIn            foliageIn;
    FoliageSpawnOut           foliageOut;

    bool                      decodeArmed = false;
    bool                      decodeRunning = false;
    std::atomic<bool>         decodeFinished{ false };
    JobStatus                 decodeStatus = JobStatus::Failed;
    bool                      decodedOk = false;
    WorldEngineCpuImages      decoded;

    size_t                    streamCursor = 0;
    int                       primitiveStep = 0;
    int                       modelStep = 0;
    bool                      demoChecked = false;

    SpawnPhase                phase = SpawnPhase::Classify;
    bool                      expanded = false;
    bool                      questCleared = false;
    std::vector<SceneObjectData> pawns;
    std::vector<SceneObjectData> boxes;
    std::vector<SceneObjectData> lights;
    std::vector<SceneObjectData> spawners;
    std::vector<SceneObjectData> movies;
    std::vector<SceneObjectData> models;
    std::vector<SceneObjectData> archetypes;
    std::vector<SceneObjectData> clouds;
    size_t                    cloudCursor = 0;
    uint32_t                  spawnedClouds = 0;
    size_t                    objectCursor = 0;
    int                       powerStamp = 0;
    int                       pickupStamp = 0;
    size_t                    grassCursor = 0;
    uint32_t                  patchSeed = 1;
    size_t                    lightCursor = 0;
    size_t                    boxCursor = 0;
    size_t                    pawnCursor = 0;
    int                       hunterStamp = 0;
    int                       wolfStamp = 0;
    bool                      pawnsLogged = false;
    size_t                    modelCursor = 0;
    size_t                    archetypeCursor = 0;
    int                       modelStamp = 0;
    int                       archetypeStamp = 0;
    int                       modelsAttached = 0;
    bool                      modelsLogged = false;
    bool                      spawnerMarkerReady = false;
    AssetRef<Model>           spawnerMarker;
    size_t                    spawnerCursor = 0;
    int                       spawnerStamp = 0;
    bool                      spawnersLogged = false;
    bool                      movieMarkerReady = false;
    AssetRef<Model>           movieMarker;
    size_t                    movieCursor = 0;
    int                       movieStamp = 0;
    bool                      moviesLogged = false;
    bool                      physicsBound = false;
    size_t                    stockCursor = 0;
    int                       buildingModelStep = 0;
    BuildingFixtureSpawn      fixtures{};
};

JobStatus SandboxApp::bootTerrainCpu(void* user)
{
    auto* boot = static_cast<SandboxBootLoad*>(user);
    bool ok = false;
    if (boot->kind == SandboxBootLoad::TerrainKind::WorldEngine)
        ok = loadWorldEngineDirectory(boot->worldDir, boot->importDesc, boot->worldMaps);
    else
        ok = boot->loadedHeight.loadBinary(boot->heightPath);
    boot->terrainStatus = ok ? JobStatus::Ok : JobStatus::Failed;
    boot->terrainFinished.store(true, std::memory_order_release);
    return boot->terrainStatus;
}

JobStatus SandboxApp::bootFoliage(void* user)
{
    auto* boot = static_cast<SandboxBootLoad*>(user);
    const bool ok = spawnFoliage(boot->foliageIn, boot->foliageOut);
    boot->foliageStatus = ok ? JobStatus::Ok : JobStatus::Failed;
    boot->foliageFinished.store(true, std::memory_order_release);
    return boot->foliageStatus;
}

JobStatus SandboxApp::bootWorldEngineDecode(void* user)
{
    auto* boot = static_cast<SandboxBootLoad*>(user);
    bool ok = false;
    if (boot->assets)
    {
        AssetRef<Image> diffuse = boot->assets->loadImageFile(boot->worldMaps.diffuse);
        Image ormImage;
        if (diffuse && diffuse->valid() && buildWorldEngineOrmImage(boot->worldMaps.roughness, boot->worldMaps.mask, ormImage))
        {
            diffuse->setColorSpace(Dark::Color::ColorSpace::sRGB);
            const std::string ormKey = std::string("worldengine-orm:") + boot->worldMaps.roughness.generic_string();
            AssetRef<Image> orm = boot->assets->internImage(std::make_shared<Image>(std::move(ormImage)), ormKey);
            if (orm && orm->valid())
            {
                boot->decoded.diffuse = std::move(diffuse);
                boot->decoded.orm     = std::move(orm);
                ok = true;
            }
        }
    }
    boot->decodedOk    = ok;
    boot->decodeStatus = ok ? JobStatus::Ok : JobStatus::Failed;
    boot->decodeFinished.store(true, std::memory_order_release);
    return boot->decodeStatus;
}

SandboxApp::~SandboxApp()
{
    // IO jobs hold pointers into m_boot and the terrain maps. Join them before those members die.
    jobs().shutdown();
    delete m_boot;
    m_boot = nullptr;
}

void SandboxApp::prepareBoot(bool runScene, const std::filesystem::path& scenePath)
{
    SandboxBootLoad* next = new SandboxBootLoad();
    delete m_boot;
    m_boot = next;
    m_boot->runScene  = runScene;
    m_boot->scenePath = scenePath;
    m_boot->assets    = &assets();
    m_bootSlice       = BootSlice::Ibl;
}

void SandboxApp::beginSceneAfterIbl(const std::filesystem::path& scenePath)
{
    // Done still carries the last load's flags. A new record lets a failed load be requested again.
    if (!m_boot || m_bootSlice == BootSlice::Done)
        prepareBoot(true, scenePath);
    m_boot->runScene  = true;
    m_boot->scenePath = scenePath;
    m_boot->assets    = &assets();
    if (m_bootSlice == BootSlice::Done || m_bootSlice == BootSlice::Ibl)
        m_bootSlice = BootSlice::WaitScene;
}

bool SandboxApp::bootSceneLoop()
{
    while (m_bootSlice != BootSlice::Done)
    {
        if (!advanceBootSlice())
            return false;
        if (!pumpBootFrame())
            return false;
    }
    return true;
}

bool SandboxApp::leaveScene()
{
    if (!m_sceneStarted || m_bootPolling)
        return false;
    if (network().role() != NetRole::Idle)
        return false;

    DE_LOG_INFO("Sandbox: leaving '{}'", m_loadedScenePath.filename().string());

    // Voices and movie threads still name entities. Stop them before the world goes.
    m_movie.close();
    resetMovieCues(world());
    if (m_dialogue.active())
        m_dialogue.interrupt(DialogueInterrupt::Menu);
    m_dialogue.speech().stop();
    m_dialogue.runtime().setFacts(nullptr);
    audio().stopAll();
    m_sceneWorld.leafRustle.stopAll(audio());
    clearAllStatusFx(world(), &audio());
    m_play.guardVoice().stop();
    m_play.resetPawn();
    m_play.quest().clear();
    m_play.throws().clear();
    m_play.clearFacility();
    m_play.sceneQuest().clear();
    m_play.buildingContainers().clear();
    m_play.buildingHides().clear();
    m_play.buildingLabels().clear();
    m_play.buildingDoors().clear();
    m_play.buildingCameras().clear();
    m_play.feedback().blood.stop(true);
    m_play.feedback().splats.clear();
    m_play.feedback().muzzleTimer = 0.0f;
    if (WeaponLoadout* weapons = localWeapons())
    {
        weapons->projectile().setAudio(nullptr, nullptr);
        weapons->clear();
    }

    // The last frame drew this scene. Wait, then release height and water resources.
    renderer().waitForGpu();
    Physics::destroyFoliageCollision(m_sceneWorld.physics, m_sceneWorld.foliageCollision);
    m_sceneWorld.physics.destroy();
    m_sceneWorld.physicsGround = Physics::kNullPhysicsBody;
    m_sceneWorld.groundIgnore  = Physics::kNullPhysicsBody;

    std::vector<Entity> ents;
    world().forEachPool([&](IComponentPool& pool) {
        pool.visit([](EntityID id, void*, void* user) {
            static_cast<std::vector<Entity>*>(user)->push_back(Entity{ id });
        }, &ents);
    });
    for (Entity e : ents)
        Save::destroyProgressEntity(world(), e, network(), pins());

    m_play.cameraEntity() = {};
    m_play.muzzle()       = {};
    m_play.shield()       = {};
    m_play.session()      = {};
    m_demo.cube()         = {};
    m_chase.clearLevel();
    m_chaseOk = false;

    m_sceneWorld.terrain.clear();
    m_sceneWorld.water = WaterWorld{};
    m_sceneWorld.foliage.clear();
    m_sceneWorld.grassField.reset();
    m_sceneWorld.leafField.clear();
    m_sceneWorld.leafSimSeconds = 0.0;
    m_sceneWorld.leafShedScale  = 1.0f;
    m_sceneWorld.cloudTime      = 0.0f;
    m_sceneWorld.haveTerrainSea = false;
    m_sceneWorld.terrainSeaLevel = 0.0f;
    m_sceneWorld.showTerrain    = true;

    m_frame.gpuParticles().prepare(world(), 0.0f);
    m_frame.prevWorldByEntity().clear();
    m_frame.scene().discardTemporalHistory();
    m_frame.scene().clearDecals();

    m_vision.camouflage    = false;
    m_vision.nightSightOn  = false;
    m_vision.nightSight    = 0.0f;
    m_vision.infraredOn    = false;
    m_vision.infrared      = 0.0f;
    m_vision.ultravioletOn = false;
    m_vision.ultraviolet   = 0.0f;
    m_vision.echoOn        = false;
    m_vision.echo          = 0.0f;
    m_vision.echoChirpAge  = 0.0f;
    m_vision.echoCapture   = false;
    m_vision.echoHaveChirp = false;
    m_play.setSimPaused(false);
    m_showInventory        = false;
    m_hud.m_showJournal    = false;
    m_hud.m_showUpgrades   = false;
    m_hud.m_lootTarget     = {};
    m_hud.m_questBanners.clear();
    m_hud.m_defenseTextLeft = 0.0f;

    m_sceneStarted = false;
    m_loadedScenePath.clear();
    m_bootSlice   = BootSlice::Done;
    m_bootPolling = false;
    world().bumpGeneration();
    return true;
}

bool SandboxApp::startScene(const std::filesystem::path& scenePath)
{
    if (m_sceneStarted || m_bootPolling)
        return true;
    beginSceneAfterIbl(scenePath);
    // onInit still has the splash, and pumpBootFrame Presents between slices.
    // Menu confirm is after teardown, so the main loop presents between slices.
    if (splashActive())
        return bootSceneLoop();
    m_bootPolling = true;
    return true;
}

bool SandboxApp::advanceBootSlice()
{
    if (!m_boot)
        return false;
    SandboxBootLoad& boot = *m_boot;

    switch (m_bootSlice)
    {
    case BootSlice::Ibl:
    {
        HitchClock clock("Ibl");
        if (!loadAndBakeIbl(renderer(), assets(), m_sceneWorld.ibl, m_sceneWorld.iblImageId))
            DE_LOG_INFO(LogCategory::Render, "SandboxApp: IBL off — using ambient");
        // Menu confirm runs the rest. Ibl alone must not mark the scene started.
        if (!boot.runScene)
            m_bootSlice = BootSlice::Done;
        else
            m_bootSlice = BootSlice::WaitScene;
        return true;
    }
    case BootSlice::WaitScene:
    {
        if (!boot.sceneArmed)
        {
            boot.sceneArmed = true;
            const bool wantAsync = config().asyncSceneLoad && acceptJobPath(assets(), boot.scenePath);
            if (wantAsync)
            {
                boot.sceneGeneration = 1;
                SceneLoadRequest request;
                request.path       = boot.scenePath;
                request.generation = boot.sceneGeneration;
                if (enqueueSceneLoad(jobs(), assets(), request))
                {
                    boot.sceneRunning = true;
                    return true;
                }
            }
            HitchClock clock("WaitScene");
            std::string err;
            const bool ok = loadSceneFromJson(boot.scenePath, boot.scene, &err);
            if (!ok)
            {
                // A failed parse can already hold terrain and atmosphere. That is not a scene.
                boot.scene   = SceneFileData{};
                boot.sceneOk = false;
                DE_LOG_ERROR(LogCategory::Render, "SandboxApp: scene load failed ({}) — {}", boot.scenePath.string(), err);
                m_bootSlice = BootSlice::Done;
                return true;
            }
            boot.sceneOk = true;
            DE_LOG_INFO(LogCategory::Render, "SandboxApp: scene {}", boot.scenePath.string());
        }
        else if (boot.sceneRunning)
        {
            SceneLoadResult result;
            const bool terminal = pollSceneLoad(jobs(), boot.sceneGeneration, result);
            if (!terminal && result.status == JobStatus::NotReady)
                return true;
            boot.sceneRunning = false;
            if (!terminal || result.status != JobStatus::Ok)
            {
                boot.scene   = SceneFileData{};
                boot.sceneOk = false;
                DE_LOG_ERROR(LogCategory::Render, "SandboxApp: scene load failed ({}) — {}", boot.scenePath.string(), result.error);
                m_bootSlice = BootSlice::Done;
                return true;
            }
            boot.scene   = std::move(result.scene);
            boot.sceneOk = true;
            DE_LOG_INFO(LogCategory::Render, "SandboxApp: scene {}", boot.scenePath.string());
        }
        m_bootSlice = BootSlice::WaitTerrainCpu;
        return true;
    }
    case BootSlice::WaitTerrainCpu:
    {
        if (!boot.headerApplied)
        {
            boot.headerApplied = true;
            m_sceneWorld.stealth = boot.scene.stealth;
            applySceneAwareness(m_chase.ai(), m_sceneWorld.stealth);
            m_play.sceneQuest() = boot.scene.quest;
            applySceneAtmosphere(m_sceneWorld.env, boot.scene);
            {
                const ExposureSceneDesc exposure = boot.scene.exposure.present ? boot.scene.exposure : ExposureSceneDesc{};
                m_frame.autoExposure().mode        = exposure.autoExposure ? ExposureMode::Auto : ExposureMode::Manual;
                m_frame.autoExposure().evBias      = exposure.evBias;
                m_frame.autoExposure().maxEv       = exposure.maxEv;
                m_frame.autoExposure().adaptBright = exposure.adaptBright;
                m_frame.autoExposure().adaptDark   = exposure.adaptDark;
                resetAutoExposure(m_frame.autoExposureState());
                DE_LOG_INFO(LogCategory::Render, "SandboxApp: exposure auto={}", exposure.autoExposure);
            }
            m_sceneWorld.foliageDensity.dirtTreesPerM2    = boot.scene.terrain.foliage.dirtTreesPerM2;
            m_sceneWorld.foliageDensity.dirtFlowersPerM2  = boot.scene.terrain.foliage.dirtFlowersPerM2;
            m_sceneWorld.foliageDensity.grassTreesPerM2   = boot.scene.terrain.foliage.grassTreesPerM2;
            m_sceneWorld.foliageDensity.grassFlowersPerM2 = boot.scene.terrain.foliage.grassFlowersPerM2;
            m_sceneWorld.foliageDensity.rockPerM2         = boot.scene.terrain.foliage.rockPerM2;
            m_sceneWorld.foliageDensity.grassPerM2        = boot.scene.terrain.foliage.grassPerM2;
            m_sceneWorld.foliageDensity.treeMinNormalY    = boot.scene.terrain.foliage.treeMinNormalY;
            m_sceneWorld.foliageDensity.flowerMinNormalY  = boot.scene.terrain.foliage.flowerMinNormalY;
            m_sceneWorld.foliageDensity.tuftMinNormalY    = boot.scene.terrain.foliage.tuftMinNormalY;
            m_sceneWorld.foliageDensity.rockMinNormalY    = boot.scene.terrain.foliage.rockMinNormalY;
            m_sceneWorld.foliageDensity.treeMaxLeanDeg    = boot.scene.terrain.foliage.treeMaxLeanDeg;
            m_sceneWorld.foliageDensity.rockMaxPitchDeg   = boot.scene.terrain.foliage.rockMaxPitchDeg;
            m_sceneWorld.foliageDensity.treeSpacingM      = boot.scene.terrain.foliage.treeSpacingM;
            m_sceneWorld.foliageDensity.rockSpacingM      = boot.scene.terrain.foliage.rockSpacingM;
            m_sceneWorld.foliageDensity.patchStrength     = boot.scene.terrain.foliage.patchStrength;
            m_sceneWorld.foliageDensity.patchLargeM       = boot.scene.terrain.foliage.patchLargeM;
            m_sceneWorld.foliageDensity.patchMediumM      = boot.scene.terrain.foliage.patchMediumM;
            m_sceneWorld.foliageDensity.patchFineM        = boot.scene.terrain.foliage.patchFineM;
            m_sceneWorld.foliageDensity.seed              = boot.scene.terrain.foliage.seed;
            m_sceneWorld.foliageDensity.treeModel         = boot.scene.terrain.foliage.treeModel;
            m_sceneWorld.foliageDensity.flowerModel       = boot.scene.terrain.foliage.flowerModel;
            m_sceneWorld.foliageDensity.rockModel         = boot.scene.terrain.foliage.rockModel;
            m_sceneWorld.foliageDensity.grassModel        = boot.scene.terrain.foliage.grassModel;
            m_sceneWorld.foliageDensity.trees             = boot.scene.terrain.foliage.trees;
            m_sceneWorld.foliageDensity.flowers           = boot.scene.terrain.foliage.flowers;
            m_sceneWorld.foliageDensity.rocks             = boot.scene.terrain.foliage.rocks;
            m_sceneWorld.foliageDensity.tufts             = boot.scene.terrain.foliage.tufts;
            m_sceneWorld.leafShedScale                    = boot.scene.terrain.foliage.shedScale;
            m_sceneWorld.wind = boot.scene.terrain.wind.settings;
            clampWindSettings(m_sceneWorld.wind);
            const GrassSceneDesc& grassScene = boot.scene.terrain.grass;
            GrassParams grassParams;
            grassParams.enabled       = grassScene.enabled;
            grassParams.heightMetres  = grassScene.heightMetres;
            grassParams.flexibility   = grassScene.flexibility;
            grassParams.densityScale  = grassScene.densityScale;
            grassParams.windTipMetres = grassScene.windTipMetres;
            grassParams.faceEdge      = grassScene.faceEdge;
            grassParams.faceYaw       = grassScene.faceYaw;
            grassParams.faceLean      = grassScene.faceLean;
            grassParams.faceSteep     = grassScene.faceSteep;
            applyWindToGrass(m_sceneWorld.wind, grassParams);
            if (!m_frame.grassPipeline().isValid())
                grassParams.enabled = false;
            m_sceneWorld.grassField.setParams(grassParams);
        }

        if (!boot.terrainArmed)
        {
            boot.terrainArmed = true;
            auto sidecar = [&](const std::string& file) {
                if (file.empty())
                    return std::filesystem::path{};
                const std::filesystem::path p(file);
                if (p.is_absolute())
                    return p;
                const std::filesystem::path parent = boot.scenePath.has_parent_path() ? boot.scenePath.parent_path() : std::filesystem::path{};
                return parent / p;
            };

            const bool wantIo = config().asyncSceneLoad;
            if (boot.scene.mode != SceneMode::Scene2D && boot.scene.hasTerrain)
            {
                if (!boot.scene.terrain.source.empty())
                {
                    boot.worldDir = resolveWorldEngineDirectory(boot.scene.terrain.source);
                    boot.importDesc.worldSizeMeters   = boot.scene.terrain.worldSize > 1.0f ? boot.scene.terrain.worldSize : 1024.0f;
                    boot.importDesc.heightRangeMeters = boot.scene.terrain.importHeight > 0.0f ? boot.scene.terrain.importHeight : 480.0f;
                    boot.chunkCells = boot.scene.terrain.chunkCells > 0 ? boot.scene.terrain.chunkCells : 64;
                    if (boot.worldDir.empty())
                        DE_LOG_ERROR(LogCategory::Render, "SandboxApp: World Engine folder '{}' failed", boot.scene.terrain.source);
                    else
                    {
                        boot.kind = SandboxBootLoad::TerrainKind::WorldEngine;
                        const bool queued = wantIo && acceptJobPath(assets(), boot.worldDir)
                            && enqueueBootIo(jobs(), "TerrainCpu", &SandboxApp::bootTerrainCpu, m_boot);
                        if (queued)
                            boot.terrainRunning = true;
                        else
                        {
                            HitchClock clock("WaitTerrainCpu");
                            const bool ok = loadWorldEngineDirectory(boot.worldDir, boot.importDesc, boot.worldMaps);
                            boot.terrainStatus = ok ? JobStatus::Ok : JobStatus::Failed;
                            boot.terrainFinished.store(true, std::memory_order_release);
                        }
                    }
                }
                else if (boot.scene.terrain.hasGrid)
                {
                    boot.gridDesc.tilesX       = boot.scene.terrain.grid.tilesX;
                    boot.gridDesc.tilesZ       = boot.scene.terrain.grid.tilesZ;
                    boot.gridDesc.tileCells    = boot.scene.terrain.grid.tileCells;
                    boot.gridDesc.chunkCells   = boot.scene.terrain.chunkCells > 0 ? boot.scene.terrain.chunkCells : 64;
                    boot.gridDesc.cellSize     = boot.scene.terrain.grid.cellSize;
                    boot.gridDesc.heightScale  = boot.scene.terrain.grid.heightScale;
                    boot.gridDesc.origin       = boot.scene.terrain.grid.origin;
                    boot.gridDesc.coarseFile   = sidecar(boot.scene.terrain.grid.coarseFile);
                    boot.gridDesc.tileDir      = sidecar(boot.scene.terrain.grid.tileDir);
                    boot.gridDesc.residentRing = boot.scene.terrain.grid.residentRing;
                    boot.kind = SandboxBootLoad::TerrainKind::Grid;
                    // create() rejects a ".." component and never reads the file. Canonicalize, then loadBinary.
                    std::filesystem::path coarse = boot.gridDesc.coarseFile;
                    if (!coarse.empty())
                    {
                        std::error_code ec;
                        const std::filesystem::path canonical = std::filesystem::weakly_canonical(coarse, ec);
                        if (!ec && !canonical.empty())
                            coarse = canonical;
                    }
                    boot.heightPath = coarse;
                    const bool accepted = !boot.heightPath.empty() && wantIo && acceptJobPath(assets(), boot.heightPath);
                    if (accepted && enqueueBootIo(jobs(), "TerrainCpu", &SandboxApp::bootTerrainCpu, m_boot))
                        boot.terrainRunning = true;
                    else if (!boot.heightPath.empty())
                    {
                        HitchClock clock("WaitTerrainCpu");
                        const bool ok = boot.loadedHeight.loadBinary(boot.heightPath);
                        boot.terrainStatus = ok ? JobStatus::Ok : JobStatus::Failed;
                        boot.terrainFinished.store(true, std::memory_order_release);
                    }
                }
                else if (!boot.scene.terrain.heightFile.empty())
                {
                    boot.kind = SandboxBootLoad::TerrainKind::HeightFile;
                    boot.heightPath = sidecar(boot.scene.terrain.heightFile);
                    boot.chunkCells = boot.scene.terrain.chunkCells > 0 ? boot.scene.terrain.chunkCells : 16;
                    const bool queued = wantIo && acceptJobPath(assets(), boot.heightPath)
                        && enqueueBootIo(jobs(), "TerrainCpu", &SandboxApp::bootTerrainCpu, m_boot);
                    if (queued)
                        boot.terrainRunning = true;
                    else
                    {
                        HitchClock clock("WaitTerrainCpu");
                        const bool ok = boot.loadedHeight.loadBinary(boot.heightPath);
                        boot.terrainStatus = ok ? JobStatus::Ok : JobStatus::Failed;
                        boot.terrainFinished.store(true, std::memory_order_release);
                    }
                }
            }
        }

        if (boot.terrainRunning && !boot.terrainFinished.load(std::memory_order_acquire))
            return true;
        m_bootSlice = BootSlice::TerrainMesh;
        return true;
    }
    case BootSlice::TerrainMesh:
    {
        HitchClock clock("TerrainMesh");
        if (boot.kind == SandboxBootLoad::TerrainKind::WorldEngine)
        {
            const bool loaded = boot.terrainStatus == JobStatus::Ok
                && m_sceneWorld.terrain.createFromHeightMap(std::move(boot.worldMaps.height), boot.chunkCells);
            if (loaded)
            {
                boot.matSplat = boot.worldMaps.splat;
                if (!m_sceneWorld.terrain.setWorkingSplat(std::move(boot.worldMaps.splat)))
                    DE_LOG_WARN(LogCategory::Render, "SandboxApp: World Engine splat was not kept");
                boot.loadedScene = true;
                boot.worldEngine = true;
                DE_LOG_INFO(LogCategory::Render, "SandboxApp: loaded World Engine terrain '{}'", boot.scene.terrain.source);
            }
            else
                DE_LOG_ERROR(LogCategory::Render, "SandboxApp: World Engine folder '{}' failed", boot.scene.terrain.source);
        }
        else if (boot.kind == SandboxBootLoad::TerrainKind::Grid)
        {
            bool ok = false;
            if (boot.terrainStatus == JobStatus::Ok)
                ok = m_sceneWorld.terrain.createFromCoarse(boot.gridDesc, std::move(boot.loadedHeight));
            if (ok)
            {
                boot.loadedScene  = true;
                m_sceneWorld.haveTerrainSea  = true;
                m_sceneWorld.terrainSeaLevel = boot.scene.terrain.grid.seaLevel;
                DE_LOG_INFO(LogCategory::Render, "SandboxApp: streaming scene terrain {}x{} tiles", boot.gridDesc.tilesX, boot.gridDesc.tilesZ);
            }
            else
                DE_LOG_ERROR(LogCategory::Render, "SandboxApp: scene terrain.grid failed — FBM fallback");
        }
        else if (boot.kind == SandboxBootLoad::TerrainKind::HeightFile)
        {
            if (boot.terrainStatus == JobStatus::Ok
                && m_sceneWorld.terrain.createFromHeightMap(std::move(boot.loadedHeight), boot.chunkCells))
            {
                boot.loadedScene = true;
                DE_LOG_INFO(LogCategory::Render, "SandboxApp: loaded legacy heightFile as 1-tile Grid");
            }
        }

        if (!boot.loadedScene)
        {
            const bool bareGround = !boot.scene.hasTerrain && boot.scene.mode != SceneMode::Scene2D;
            HeightMap base;
            const float extent = 128.0f * 2.0f;
            bool heightOk = false;
            if (bareGround)
                heightOk = base.create(129, 129, 2.0f, 1.0f);
            else
            {
                HeightMap detail;
                heightOk = base.createFbm(129, 129, 1337u, 6, 3.5f, 1.0f, 2.1f, 0.48f, 2.0f, 22.0f)
                    && detail.createFbm(129, 129, 9001u, 3, 18.0f, 0.12f, 2.0f, 0.5f, 2.0f, 22.0f)
                    && base.addLayer(detail, 1.0f);
            }
            if (!heightOk)
            {
                DE_LOG_FATAL("SandboxApp: height map create failed");
                requestQuit();
                return false;
            }
            base.setOrigin(Vector3f{ -0.5f * extent, 0.0f, -0.5f * extent });
            if (!boot.matSplat.generateFromHeight(base))
            {
                DE_LOG_FATAL("SandboxApp: splat generate failed");
                requestQuit();
                return false;
            }
            if (!m_sceneWorld.terrain.createFromHeightMap(std::move(base), 16))
            {
                DE_LOG_FATAL("SandboxApp: terrain create failed");
                requestQuit();
                return false;
            }
            m_sceneWorld.terrain.setWorkingSplat(SplatMap(boot.matSplat));
            if (bareGround)
            {
                m_sceneWorld.showTerrain = false;
                DE_LOG_INFO(LogCategory::Render, "SandboxApp: scene has no terrain; flat ground, mesh hidden");
            }
        }
        m_bootSlice = BootSlice::WaitFoliage;
        return true;
    }
    case BootSlice::WaitFoliage:
    {
        if (!boot.waterReady)
        {
            const AABox3f terrainBox = m_sceneWorld.terrain.bounds();
            const WaterSceneDesc& waterScene = boot.scene.water;
            float waterLevel = Lerp(terrainBox.Min.y, terrainBox.Max.y, waterScene.levelFraction);
            if (waterScene.hasLevel)
                waterLevel = waterScene.level;
            else if (m_sceneWorld.haveTerrainSea)
                waterLevel = m_sceneWorld.terrainSeaLevel;
            else if (!m_sceneWorld.showTerrain)
                waterLevel = -50.0f;
            boot.waterLevel = waterLevel;
            boot.waterReady = true;
        }

        if (!boot.foliageConsidered)
        {
            boot.foliageConsidered = true;
            bool inlineWork = false;
            if (boot.worldEngine)
            {
                const HeightMap* height = m_sceneWorld.terrain.editableWorking();
                const SplatMap*  splat  = m_sceneWorld.terrain.editableWorkingSplat();
                if (height && splat)
                {
                    boot.foliageIn.height    = height;
                    boot.foliageIn.splat     = splat;
                    boot.foliageIn.density   = m_sceneWorld.foliageDensity;
                    boot.foliageIn.trails    = boot.scene.terrain.trails.paths;
                    boot.foliageIn.tilesX    = m_sceneWorld.terrain.tilesX();
                    boot.foliageIn.tilesZ    = m_sceneWorld.terrain.tilesZ();
                    const int cells = m_sceneWorld.terrain.tileCells();
                    boot.foliageIn.tileCells = cells > 0 ? static_cast<uint32_t>(cells) : 0u;
                    boot.foliageIn.cellSize  = m_sceneWorld.terrain.cellSize();
                    boot.foliageIn.origin    = m_sceneWorld.terrain.origin();
                    boot.foliageIn.seaLevel  = height->origin().y;
                    m_frame.foliagePrototypes().sync(renderer(), assets(), m_sceneWorld.foliageDensity);
                    const AssetRef<Model>& treeModel = m_frame.foliagePrototypes().model(FoliageKind::Tree);
                    if (treeModel && treeModel->bounds().IsValid())
                    {
                        const Vector3f extent = treeModel->bounds().Size();
                        if (extent.x > 0.0f && extent.z > 0.0f)
                        {
                            boot.foliageIn.treeExtentX = extent.x;
                            boot.foliageIn.treeExtentZ = extent.z;
                        }
                    }
                    FoliageWaterVolume lake;
                    lake.entireMap = true;
                    lake.surfaceY  = boot.waterLevel;
                    boot.foliageIn.water.push_back(lake);
                    for (const SceneObjectData& object : boot.scene.objects)
                    {
                        if (!sceneHasComponent(object, "Water"))
                            continue;
                        float minX = 0.0f;
                        float maxX = 0.0f;
                        float minZ = 0.0f;
                        float maxZ = 0.0f;
                        if (!placedWaterFootprint(object.position.x, object.position.z, object.scale.x, object.scale.z, minX, maxX, minZ, maxZ))
                            continue;
                        FoliageWaterVolume pond;
                        pond.minX     = minX;
                        pond.maxX     = maxX;
                        pond.minZ     = minZ;
                        pond.maxZ     = maxZ;
                        pond.surfaceY = object.position.y;
                        boot.foliageIn.water.push_back(pond);
                    }
                    boot.foliageArmed = true;
                    // Working height and splat stay put until this job finishes. Streaming starts later.
                    if (config().asyncSceneLoad && enqueueBootIo(jobs(), "FoliageSpawn", &SandboxApp::bootFoliage, m_boot))
                        boot.foliageRunning = true;
                    else
                        inlineWork = true;
                }
                if (!boot.worldMaps.diffuse.empty())
                {
                    boot.decodeArmed = true;
                    if (config().asyncSceneLoad && enqueueBootIo(jobs(), "WorldEngineDecode", &SandboxApp::bootWorldEngineDecode, m_boot))
                        boot.decodeRunning = true;
                    else
                        inlineWork = true;
                }
            }
            if (inlineWork)
            {
                HitchClock clock("WaitFoliage");
                if (boot.foliageArmed && !boot.foliageRunning)
                {
                    const bool ok = spawnFoliage(boot.foliageIn, boot.foliageOut);
                    boot.foliageStatus = ok ? JobStatus::Ok : JobStatus::Failed;
                    boot.foliageFinished.store(true, std::memory_order_release);
                }
                if (boot.decodeArmed && !boot.decodeRunning)
                    bootWorldEngineDecode(m_boot);
            }
            if (boot.foliageRunning || boot.decodeRunning)
                return true;
        }

        if (boot.foliageRunning && !boot.foliageFinished.load(std::memory_order_acquire))
            return true;
        if (boot.decodeRunning && !boot.decodeFinished.load(std::memory_order_acquire))
            return true;

        if (boot.worldEngine)
        {
            const bool foliageOk = boot.foliageArmed
                && boot.foliageFinished.load(std::memory_order_acquire)
                && boot.foliageStatus == JobStatus::Ok;
            if (foliageOk)
            {
                m_sceneWorld.foliage.swap(boot.foliageOut.records);
                m_sceneWorld.terrain.setWorkingCover(std::move(boot.foliageOut.cover));
                m_sceneWorld.grassField.markAllDirty();
            }
            else
                DE_LOG_WARN(LogCategory::Render, "SandboxApp: splat foliage spawn failed");
        }
        m_bootSlice = BootSlice::TerrainMaterial;
        return true;
    }
    case BootSlice::TerrainMaterial:
    {
        HitchClock clock("TerrainMaterial");
        if (!boot.matSplat.valid() && !boot.matSplat.create(2, 2))
        {
            DE_LOG_FATAL("SandboxApp: dummy splat failed");
            requestQuit();
            return false;
        }
        if (boot.worldEngine)
        {
            // A failed decode leaves the upload on its own file read.
            const WorldEngineCpuImages* decoded = boot.decodedOk ? &boot.decoded : nullptr;
            if (!uploadWorldEngineMaterial(renderer(), assets(), boot.matSplat, boot.worldMaps, m_frame.terrainMaterial(), nullptr, decoded))
            {
                DE_LOG_FATAL("SandboxApp: World Engine material failed");
                requestQuit();
                return false;
            }
            DE_LOG_INFO(LogCategory::Render, "Terrain: World Engine albedo and roughness");
        }
        else if (tryCreateTerrainFromContent(renderer(), assets(), boot.matSplat, m_frame.terrainMaterial()))
            DE_LOG_INFO(LogCategory::Render, "Terrain: using content layer set");
        else
        {
            DE_LOG_INFO(LogCategory::Render, "Terrain: checkers fallback");
            if (!m_frame.terrainMaterial().createDefault(renderer(), boot.matSplat))
            {
                DE_LOG_FATAL("SandboxApp: terrain material create failed");
                requestQuit();
                return false;
            }
        }
        m_frame.terrainMaterial().setLayerSamplingRaw(renderer().device(), renderer().debugState().legacyUnormAlbedo);
        m_bootSlice = BootSlice::PrimeStream;
        return true;
    }
    case BootSlice::PrimeStream:
    {
        HitchClock clock("PrimeStream");
        m_sceneWorld.terrain.updateStreaming(Vector3f{ 0.0f, 50.0f, -80.0f }, &renderer(), &m_frame.terrainMaterial());
        m_bootSlice = BootSlice::CoarseHeight;
        return true;
    }
    case BootSlice::CoarseHeight:
    {
        HitchClock clock("CoarseHeight");
        if (!m_sceneWorld.terrain.uploadCoarseHeightTexture(renderer()))
        {
            DE_LOG_FATAL("SandboxApp: coarse height texture upload failed");
            requestQuit();
            return false;
        }
        if (m_sceneWorld.terrain.heightTexture().valid())
            renderer().setHeightSrv(m_sceneWorld.terrain.heightTexture().cpuHandle());
        m_bootSlice = BootSlice::WaterCpu;
        return true;
    }
    case BootSlice::WaterCpu:
    {
        HitchClock clock("WaterCpu");
        const WaterSceneDesc& waterScene = boot.scene.water;
        WaterDesc waterDesc;
        waterDesc.chunkCells = waterScene.chunkCells > 0 ? waterScene.chunkCells : 16;
        waterDesc.waterLevel = boot.waterLevel;
        const int lodCount = waterScene.lodDistanceCount < 1 ? 1 : (waterScene.lodDistanceCount > kMaxLodLevels ? kMaxLodLevels : waterScene.lodDistanceCount);
        waterDesc.lodDistanceCount = lodCount;
        for (int i = 0; i < lodCount; ++i)
            waterDesc.lodDistances[i] = waterScene.lodDistances[i];
        waterDesc.params = sceneWaterParams(waterScene, boot.waterLevel);
        if (!m_sceneWorld.water.create(m_sceneWorld.terrain.coarse(), waterDesc))
        {
            DE_LOG_FATAL("SandboxApp: water create failed");
            requestQuit();
            return false;
        }
        m_sceneWorld.water.updateLod(Vector3f{ 0.0f, 50.0f, -80.0f });
        m_bootSlice = BootSlice::WaterGpu;
        return true;
    }
    case BootSlice::WaterGpu:
    {
        HitchClock clock("WaterGpu");
        if (!m_sceneWorld.water.createGpu(renderer()))
        {
            DE_LOG_FATAL("SandboxApp: water GPU upload failed");
            requestQuit();
            return false;
        }
        DE_LOG_INFO("SandboxApp: water level {:.2f}, {} wet chunks", boot.waterLevel, m_sceneWorld.water.wetChunkCount());
        m_bootSlice = BootSlice::StreamRibbon;
        return true;
    }
    case BootSlice::StreamRibbon:
    {
        while (boot.streamCursor < boot.scene.objects.size())
        {
            const SceneObjectData& o = boot.scene.objects[boot.streamCursor++];
            float width = 0.0f;
            float flow = 0.0f;
            std::vector<Math::Vector2f> points;
            if (!sceneReadStream(o, width, flow, points))
                continue;
            Dark::StreamDesc desc;
            desc.width        = width;
            desc.flowSpeed    = flow;
            desc.bedClearance = 0.45f;
            desc.pointsXZ     = std::move(points);
            MeshData cpu;
            Math::AABox3f bounds;
            std::string err;
            if (!buildStreamRibbon(m_sceneWorld.terrain.coarse(), desc, boot.waterLevel, cpu, &bounds, &err))
                return true;
            Mesh mesh;
            if (!Mesh::tryCreate(renderer(), cpu, mesh))
            {
                DE_LOG_ERROR(LogCategory::Render, "SandboxApp: stream upload failed");
                return true;
            }
            if (!m_sceneWorld.water.addRibbon(std::move(mesh), flow, bounds))
                DE_LOG_WARN(LogCategory::Render, "SandboxApp: stream {} skipped", boot.streamCursor - 1);
            return true;
        }
        m_bootSlice = BootSlice::PrimitiveMesh;
        return true;
    }
    case BootSlice::PrimitiveMesh:
    {
        if (boot.primitiveStep == 0)
        {
            MeshData cubeData;
            CreateCube(cubeData, 1.0f);
            m_demo.cubeMesh() = Mesh::Create(renderer(), cubeData);
            if (!m_demo.cubeMesh().valid())
            {
                DE_LOG_FATAL("SandboxApp: cube mesh upload failed");
                requestQuit();
                return false;
            }
            boot.primitiveStep = 1;
            return true;
        }
        MeshData crossData;
        if (!CreateCross(crossData, 1.0f, 0.30f, 0.22f) || !Mesh::tryCreate(renderer(), crossData, m_demo.crossMesh()))
        {
            DE_LOG_FATAL("SandboxApp: health pack mesh failed");
            requestQuit();
            return false;
        }
        m_bootSlice = BootSlice::SandboxModel;
        return true;
    }
    case BootSlice::SandboxModel:
    {
        if (boot.modelStep < 4)
        {
            if (!m_demo.createSandboxModels(boot.modelStep))
            {
                DE_LOG_FATAL("SandboxApp: procedural models failed");
                requestQuit();
                return false;
            }
            ++boot.modelStep;
            return true;
        }
        if (!boot.demoChecked)
        {
            boot.demoChecked = true;
            renderer().gpuResources().setShadowSrv(m_frame.scene().shadows().srvCpu());
            m_frame.scene().localShadows().setDepthPipeline(&m_frame.scene().shadows().pipeline());
            {
                const LocalShadowSystem::CpuSrvs localSrvs = m_frame.scene().localShadows().cpuSrvs();
                renderer().setLocalShadowSrvs(localSrvs.arraySrv, localSrvs.recordsSrv);
            }
            m_demo.demoScene() = boot.scenePath.filename() == "level.json";
            m_hud.npcInfo().enabled = m_demo.demoScene();
            if (!m_demo.demoScene())
                m_bootSlice = BootSlice::SpawnBatch;
            return true;
        }
        const int gltfStep = boot.modelStep - 4;
        m_demo.spawnGltfDemo(gltfStep);
        ++boot.modelStep;
        if (boot.modelStep >= 7)
            m_bootSlice = BootSlice::SpawnBatch;
        return true;
    }
    case BootSlice::SpawnBatch:
        if (!boot.sceneOk)
        {
            m_bootSlice = BootSlice::Done;
            return true;
        }
        return advanceSpawnBatch();
    case BootSlice::Done:
        return true;
    }
    return false;
}

bool SandboxApp::ensureScenePrimitive(void* user, Entity entity, const char* primitive)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app || !entity.valid() || !primitive || primitive[0] == '\0')
        return false;
    const bool sphere = std::strcmp(primitive, "sphere") == 0;
    const bool cube   = std::strcmp(primitive, "cube") == 0;
    if (!sphere && !cube)
        return true;

    const ArchetypeParamsComponent* params = app->world().get<ArchetypeParamsComponent>(entity);
    const float c0 = params ? params->color[0] : 1.0f;
    const float c1 = params ? params->color[1] : 1.0f;
    const float c2 = params ? params->color[2] : 1.0f;
    const uint8_t r = static_cast<uint8_t>(Math::Clamp(c0, 0.0f, 1.0f) * 255.0f);
    const uint8_t g = static_cast<uint8_t>(Math::Clamp(c1, 0.0f, 1.0f) * 255.0f);
    const uint8_t b = static_cast<uint8_t>(Math::Clamp(c2, 0.0f, 1.0f) * 255.0f);
    const std::string key = std::format("runtime:/scene/{}-{:02x}{:02x}{:02x}", sphere ? "sphere" : "box", r, g, b);

    MeshData mesh;
    const bool built = sphere ? CreateSphere(mesh, 0.5f) : CreateCube(mesh, 1.0f);
    if (!built)
        return false;
    AssetRef<Model> model = internAndUploadProceduralModel(app->renderer(), app->assets(), std::move(mesh),
        internSolidMaterial(app->assets(), r, g, b, 255, key + "-mat"), key);
    if (!model)
        return false;
    ModelComponent mc{};
    mc.modelAssetID = model->id;
    mc.castShadow   = true;
    setModelComponent(app->world(), app->pins(), app->assets(), entity, mc);
    return true;
}

bool SandboxApp::ensureSceneMarker(void* user, Entity entity)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app || !entity.valid() || !app->m_boot)
        return false;
    const SandboxBootLoad& boot = *app->m_boot;
    AssetRef<Model> marker;
    if (app->world().has<SpawnerComponent>(entity))
        marker = boot.spawnerMarker;
    else if (app->world().has<MovieCueComponent>(entity))
        marker = boot.movieMarker;
    if (!marker)
        return true;
    ModelComponent mc{};
    mc.modelAssetID = marker->id;
    mc.castShadow   = true;
    setModelComponent(app->world(), app->pins(), app->assets(), entity, mc);
    return true;
}

Dark::Entity SandboxApp::spawnAuthoredArchetype(const Dark::SceneObjectData& object)
{
    if (sceneIsPlaceholder(object))
        return {};
    SceneObjectData placed = object;
    if (placed.archetype.empty())
        return {};
    if (zeroScale(placed.scale))
        placed.scale = Vector3f{ 1.0f, 1.0f, 1.0f };
    SpawnServices services;
    services.world           = &world();
    services.assets          = &assets();
    services.pins            = &pins();
    services.renderer        = &renderer();
    services.audio           = &audio();
    services.ground          = &m_sceneWorld.ground;
    services.ai              = m_chaseOk ? const_cast<AiSystem*>(&m_chase.ai()) : nullptr;
    services.ensurePrimitive = &SandboxApp::ensureScenePrimitive;
    services.ensureMarker    = &SandboxApp::ensureSceneMarker;
    services.onWeaponHit     = &SandboxApp::onWeaponHitThunk;
    services.weaponHitUser   = this;
    services.user            = this;
    services.crows           = &m_play.crows();
    services.crowTerrain     = &m_sceneWorld.terrain;
    const ArchetypeSpawnHost host = makeSpawnHost(services);
    ContentError err;
    const Entity e = instantiateSceneObject(host, placed, &err);
    if (!e.valid())
    {
        DE_LOG_WARN(LogCategory::AI, "Sandbox: archetype '{}' failed ({})", placed.archetype, err.message);
        return {};
    }
    registerSceneReplica(network(), world(), e, placed.archetype, placed.color);
    int stamp = 0;
    if (m_boot)
        stamp = m_boot->archetypeStamp++;
    stampProceduralId(world(), e, std::format("sandbox/level/archetype/{}", stamp), placed.archetype);
    return e;
}

bool SandboxApp::advanceSpawnBatch()
{
    SandboxBootLoad& boot = *m_boot;
    auto spawnCloud = [&](const Vector3f& pos, const Vector3f& scale, const CloudVolumeDesc& desc) {
        Entity e = world().createEntity();
        TransformComponent xf{};
        xf.position = pos;
        xf.scale    = scale;
        world().emplace<TransformComponent>(e, xf);
        CloudVolumeComponent cloud{};
        cloud.desc = desc;
        world().emplace<CloudVolumeComponent>(e, cloud);
    };
    auto sceneHost = [&](bool animatedOnly) {
        SpawnServices services;
        services.world             = &world();
        services.assets            = &assets();
        services.pins              = &pins();
        services.renderer          = &renderer();
        services.audio             = &audio();
        services.ground            = &m_sceneWorld.ground;
        services.ai                = m_chaseOk ? const_cast<AiSystem*>(&m_chase.ai()) : nullptr;
        services.ensurePrimitive   = &SandboxApp::ensureScenePrimitive;
        services.ensureMarker      = &SandboxApp::ensureSceneMarker;
        services.onWeaponHit       = &SandboxApp::onWeaponHitThunk;
        services.weaponHitUser     = this;
        services.modelAnimatedOnly = animatedOnly;
        services.user              = this;
        services.crows             = &m_play.crows();
        services.crowTerrain       = &m_sceneWorld.terrain;
        return makeSpawnHost(services);
    };

    for (int guard = 0; guard < 64; ++guard)
    {
        switch (boot.phase)
        {
        case SandboxBootLoad::SpawnPhase::Classify:
            if (boot.sceneOk && !boot.expanded)
            {
                expandSceneBuildings(boot.scene);
                boot.expanded = true;
            }
            for (const SceneObjectData& o : boot.scene.objects)
            {
                if (sceneIsPlaceholder(o))
                    continue;
                if (sceneHasBehavior(o, "player.controller"))
                {
                    m_chase.setPlayerStart(o.position);
                    continue;
                }
                if (sceneHasBehavior(o, "ai.hunter") || sceneHasBehavior(o, "ai.wolf") || sceneHasBehavior(o, "ai.animal"))
                {
                    boot.pawns.push_back(o);
                    continue;
                }
                if (sceneHasComponent(o, "Spawner"))
                {
                    boot.spawners.push_back(o);
                    continue;
                }
                if (sceneHasComponent(o, "Movie"))
                {
                    boot.movies.push_back(o);
                    continue;
                }
                // Particles list a local light for the glow. They are not light entities.
                if (sceneHasComponent(o, "ParticleEmitter"))
                    continue;
                const bool platform = sceneHasBehavior(o, "platform.marker");
                if (!platform && (sceneHasBehavior(o, "prop.solid") || sceneHasComponent(o, "Mesh")))
                {
                    boot.boxes.push_back(o);
                    continue;
                }
                if (sceneHasComponent(o, "LocalLight"))
                {
                    boot.lights.push_back(o);
                    continue;
                }
                if (sceneHasBehavior(o, "model"))
                {
                    boot.models.push_back(o);
                    continue;
                }
                if (sceneHasComponent(o, "CloudVolume"))
                {
                    boot.clouds.push_back(o);
                    continue;
                }
                if (sceneIsDataDriven(o))
                    boot.archetypes.push_back(o);
            }
            boot.phase = SandboxBootLoad::SpawnPhase::Clouds;
            return true;
        case SandboxBootLoad::SpawnPhase::Clouds:
        {
            int spawned = 0;
            while (boot.cloudCursor < boot.clouds.size() && spawned < kBootSpawnBatch)
            {
                const SceneObjectData& o = boot.clouds[boot.cloudCursor++];
                ContentError err;
                const Entity e = instantiateSceneObject(sceneHost(false), o, &err);
                if (!e.valid())
                    DE_LOG_WARN(LogCategory::Render, "SandboxApp: cloud failed ({})", err.message);
                else
                {
                    registerSceneReplica(network(), world(), e, o.archetype, o.color);
                    ++boot.spawnedClouds;
                }
                ++spawned;
            }
            if (spawned > 0)
                return true;
            if (boot.spawnedClouds == 0)
            {
                CloudVolumeDesc desc{};
                spawnCloud(Vector3f(0.0f, 36.0f, 0.0f), Vector3f(140.0f, 32.0f, 140.0f), desc);
                ++boot.spawnedClouds;
            }
            DE_LOG_INFO(LogCategory::Render, "SandboxApp: {} cloud volume(s)", boot.spawnedClouds);
            boot.phase = SandboxBootLoad::SpawnPhase::Shadow;
            return true;
        }
        case SandboxBootLoad::SpawnPhase::Shadow:
            renderer().gpuResources().setShadowSrv(m_frame.scene().shadows().srvCpu());
            m_frame.scene().localShadows().setDepthPipeline(&m_frame.scene().shadows().pipeline());
            {
                const LocalShadowSystem::CpuSrvs localSrvs = m_frame.scene().localShadows().cpuSrvs();
                renderer().setLocalShadowSrvs(localSrvs.arraySrv, localSrvs.recordsSrv);
            }
            boot.phase = SandboxBootLoad::SpawnPhase::Camera;
            return true;
        case SandboxBootLoad::SpawnPhase::Camera:
        {
            const float aspect = (renderer().height() > 0) ? static_cast<float>(renderer().width()) / static_cast<float>(renderer().height()) : 1.0f;
            m_sceneWorld.camera.SetLens(1.04719755f, aspect, 0.05f, 1.0e5f);
            m_sceneWorld.camera.LookAt(Vector3f(0.0f, 48.0f, -86.0f), Vector3f(0.0f, 8.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f));
            m_play.cameraEntity() = world().createEntity();
            world().emplace<TagComponent>(m_play.cameraEntity(), "Main Camera");
            world().emplace<TransformComponent>(m_play.cameraEntity(), m_sceneWorld.camera.GetPosition(), Quaternion::IDENTITY, Vector3f{ 1, 1, 1 });
            world().emplace<CameraComponent>(m_play.cameraEntity(), 60.0f, 0.5f, 1.0e5f, true);
            world().emplace<AudioListenerComponent>(m_play.cameraEntity());
            attachCameraSounds(world(), pins(), assets(), audio(), m_play.cameraEntity());
            boot.phase = SandboxBootLoad::SpawnPhase::Cube;
            return true;
        }
        case SandboxBootLoad::SpawnPhase::Cube:
        {
            const float aspect = (renderer().height() > 0) ? static_cast<float>(renderer().width()) / static_cast<float>(renderer().height()) : 1.0f;
            const float groundY = m_sceneWorld.terrain.heightAtWorld(0.0f, 0.0f) + 0.5f;
            m_demo.cube() = world().createEntity();
            world().emplace<TagComponent>(m_demo.cube(), "Cube");
            world().emplace<TransformComponent>(m_demo.cube(), Vector3f{ 0.0f, groundY, 0.0f }, Quaternion::IDENTITY, Vector3f{ 1, 1, 1 });
            attachSandboxCubeModel(world(), pins(), assets(), m_demo.cube(), m_demo.cubeModelId());
            network().setWantsPawn(true);
            network().setSceneMode(0);
            network().setPlayerName("Sandbox");
            network().setSpawnCallback(&SandboxApp::onNetSpawn, this);
            network().setDespawnCallback(&SandboxApp::onNetDespawn, this);
            network().setPeerCallback(&SandboxApp::onNetPeer, this);
            if (!network().registerEntity(world(), m_demo.cube(), NetPrefab::Cube))
            {
                onEntityRemoved(world(), m_demo.cube(), &pins());
                world().destroyEntity(m_demo.cube());
                m_demo.cube() = {};
                DE_LOG_ERROR(LogCategory::Networking, "Sandbox: failed to register init cube");
            }
            DE_LOG_INFO(
                "SandboxApp: cube mesh {} verts / {} indices, aspect {:.3f}, cube model id={}, terrain {}x{} tiles",
                m_demo.cubeMesh().vertexCount(),
                m_demo.cubeMesh().indexCount(),
                aspect,
                m_demo.cubeModelId(),
                m_sceneWorld.terrain.tilesX(),
                m_sceneWorld.terrain.tilesZ());
            DE_LOG_INFO(LogCategory::Networking, "Sandbox net: Sandbox.exe -host   and   Sandbox.exe -join 127.0.0.1");
            DE_LOG_INFO(LogCategory::Networking, "Sandbox net: M opens Dev Tools (host / join / browse / debugger)");
            boot.phase = SandboxBootLoad::SpawnPhase::Physics;
            return true;
        }
        case SandboxBootLoad::SpawnPhase::Physics:
            createPhysicsWorld();
            boot.phase = SandboxBootLoad::SpawnPhase::Objects;
            return true;
        case SandboxBootLoad::SpawnPhase::Objects:
        {
            if (!boot.questCleared)
            {
                m_play.quest().clear();
                m_hud.m_questBanners.clear();
                boot.questCleared = true;
            }
            if (boot.objectCursor < boot.scene.objects.size())
            {
                const size_t remain = boot.scene.objects.size() - boot.objectCursor;
                const size_t count = remain > static_cast<size_t>(kBootSpawnBatch) ? static_cast<size_t>(kBootSpawnBatch) : remain;
                SceneSpawnHost host{};
                host.world       = &world();
                host.physics     = m_sceneWorld.physics.valid() ? &m_sceneWorld.physics : nullptr;
                host.pins        = &pins();
                host.assets      = &assets();
                host.renderer    = &renderer();
                host.stampPrefix = "sandbox/level";
                instantiateSceneObjects(boot.scene.objects.data() + boot.objectCursor, static_cast<int>(count), host, &boot.powerStamp, &boot.pickupStamp);
                boot.objectCursor += count;
                return true;
            }
            boot.phase = SandboxBootLoad::SpawnPhase::Buildings;
            return true;
        }
        case SandboxBootLoad::SpawnPhase::Buildings:
        {
            if (boot.buildingModelStep == 0)
            {
                boot.buildingModelStep = 1;
                if (!m_play.buildingDoors().empty())
                {
                    HitchClock clock("BuildingDoorModel");
                    boot.fixtures.doorModel = loadAndUploadModel(renderer(), assets(), kUnitCubeModel);
                    return true;
                }
            }
            if (boot.buildingModelStep == 1)
            {
                boot.buildingModelStep = 2;
                if (!m_play.buildingCameras().empty())
                {
                    HitchClock clock("BuildingCameraModel");
                    boot.fixtures.camModel = loadAndUploadModel(renderer(), assets(), kUnitCubeModel);
                    return true;
                }
            }
            boot.fixtures.sliceBudget = kBootSpawnBatch;
            if (!spawnBuildingExtras(boot.fixtures))
                return true;
            boot.phase = SandboxBootLoad::SpawnPhase::Obstacles;
            return true;
        }
        case SandboxBootLoad::SpawnPhase::Obstacles:
        {
            world().each<ObstacleVolumeComponent>([&](Entity, ObstacleVolumeComponent& block) { m_chase.addObstacle(block.box); });
            std::vector<AABox3f> conceal;
            gatherConcealVolumes(world(), conceal);
            m_chase.ai().setConcealVolumes(std::move(conceal));
            boot.phase = SandboxBootLoad::SpawnPhase::Chase;
            return true;
        }
        case SandboxBootLoad::SpawnPhase::Chase:
            m_chaseOk = m_chase.init(renderer(), m_sceneWorld.terrain, m_sceneWorld.water, world(), pins(), assets());
            boot.phase = SandboxBootLoad::SpawnPhase::Quest;
            return true;
        case SandboxBootLoad::SpawnPhase::Quest:
            if (!m_play.sceneQuest().empty())
                startQuest(m_play.sceneQuest());
            m_play.clearFacility();
            if (m_play.facility().defs.empty())
            {
                std::string upgradeError;
                if (!loadUpgrades(resolveContentFile("upgrades.json"), m_play.facility().defs, &upgradeError))
                    DE_LOG_WARN("Upgrades: {}", upgradeError);
            }
            if (m_chaseOk)
            {
                GuardBarks barks;
                std::string barkError;
                if (loadGuardBarks(resolveContentFile("ai/barks.json"), barks, &barkError))
                    m_play.guardVoice().start(std::move(barks));
                else
                    DE_LOG_WARN(LogCategory::AI, "Guard barks: {}", barkError);
            }
            boot.phase = SandboxBootLoad::SpawnPhase::Grass;
            return true;
        case SandboxBootLoad::SpawnPhase::Grass:
            while (boot.grassCursor < boot.boxes.size())
            {
                const SceneObjectData& o = boot.boxes[boot.grassCursor++];
                if (!sceneConceal(o) || sceneMeshPrimitive(o) != "cube")
                    continue;
                const uint8_t r = static_cast<uint8_t>(Math::Clamp(o.color[0], 0.0f, 1.0f) * 255.0f);
                const uint8_t g = static_cast<uint8_t>(Math::Clamp(o.color[1], 0.0f, 1.0f) * 255.0f);
                const uint8_t b = static_cast<uint8_t>(Math::Clamp(o.color[2], 0.0f, 1.0f) * 255.0f);
                const std::string key = std::format("runtime:/scene/grass-{}", boot.patchSeed);
                const Entity e = world().createEntity();
                world().emplace<TransformComponent>(e, Vector3f{ o.position.x, m_sceneWorld.terrain.heightAtWorld(o.position.x, o.position.z), o.position.z }, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
                AssetRef<Model> patch = internAndUploadProceduralModel(renderer(), assets(), makeGrassPatch(o.scale.x, o.scale.z, boot.patchSeed++),
                    internSolidMaterial(assets(), r, g, b, 255, "runtime:/scene/grass-mat"), key);
                if (!patch)
                {
                    world().destroyEntity(e);
                    return true;
                }
                ModelComponent mc{};
                mc.modelAssetID = patch->id;
                mc.castShadow   = true;
                setModelComponent(world(), pins(), assets(), e, mc);
                return true;
            }
            boot.phase = SandboxBootLoad::SpawnPhase::Lights;
            continue;
        case SandboxBootLoad::SpawnPhase::Lights:
        {
            int spawned = 0;
            while (boot.lightCursor < boot.lights.size() && spawned < kBootSpawnBatch)
            {
                const SceneObjectData& o = boot.lights[boot.lightCursor++];
                SceneObjectData placed = o;
                placed.scale = Vector3f{ 1.0f, 1.0f, 1.0f };
                ContentError err;
                const Entity e = instantiateSceneObject(sceneHost(false), placed, &err);
                if (!e.valid())
                    DE_LOG_WARN(LogCategory::Render, "SandboxApp: light failed ({})", err.message);
                else
                {
                    registerSceneReplica(network(), world(), e, placed.archetype, placed.color);
                    applySceneActor(world(), e, o, nullptr, nullptr);
                }
                ++spawned;
            }
            if (spawned > 0)
                return true;
            boot.phase = SandboxBootLoad::SpawnPhase::Boxes;
            continue;
        }
        case SandboxBootLoad::SpawnPhase::Boxes:
            while (boot.boxCursor < boot.boxes.size())
            {
                const SceneObjectData& o = boot.boxes[boot.boxCursor++];
                if (sceneConceal(o) || !sceneZone(o).empty() || !scenePickup(o).empty())
                    continue;
                ContentError err;
                const Entity e = instantiateSceneObject(sceneHost(false), o, &err);
                if (!e.valid())
                    DE_LOG_WARN(LogCategory::Render, "SandboxApp: scene prop failed ({})", err.message);
                else
                    registerSceneReplica(network(), world(), e, o.archetype, o.color);
                return true;
            }
            boot.phase = SandboxBootLoad::SpawnPhase::ChasePhysics;
            continue;
        case SandboxBootLoad::SpawnPhase::ChasePhysics:
            if (!m_chaseOk)
                DE_LOG_ERROR(LogCategory::AI, "SandboxApp: path chase init failed");
            else
            {
                addChaseObstaclesToPhysics();
                m_chase.ai().setHunterHitReactionSettings(defaultHunterHitReaction());
            }
            boot.phase = SandboxBootLoad::SpawnPhase::Player;
            return true;
        case SandboxBootLoad::SpawnPhase::Player:
            if (m_chase.walker().valid())
                attachLocalPlayer(m_chase.walker());
            boot.phase = SandboxBootLoad::SpawnPhase::Shield;
            return true;
        case SandboxBootLoad::SpawnPhase::Shield:
            m_play.shield() = spawnPlayerShield(world(), pins(), assets(), renderer());
            boot.phase = SandboxBootLoad::SpawnPhase::Pawns;
            return true;
        case SandboxBootLoad::SpawnPhase::Pawns:
            if (m_chaseOk && boot.pawnCursor < boot.pawns.size())
            {
                const SceneObjectData& o = boot.pawns[boot.pawnCursor++];
                const bool wolf = sceneHasBehavior(o, "ai.wolf") || sceneHasBehavior(o, "ai.animal");
                SceneObjectData placedObject = o;
                if (zeroScale(placedObject.scale))
                    placedObject.scale = Vector3f{ 1.0f, 1.0f, 1.0f };
                ContentError err;
                const Entity hunter = instantiateSceneObject(sceneHost(true), placedObject, &err);
                if (!hunter.valid())
                {
                    DE_LOG_WARN(LogCategory::AI, "SandboxApp: scene {} failed ({})", wolf ? "wolf" : "hunter", err.message);
                    return true;
                }
                registerSceneReplica(network(), world(), hunter, placedObject.archetype, placedObject.color);
                if (PathAgentComponent* path = world().get<PathAgentComponent>(hunter))
                    path->repathAt = static_cast<float>(m_chase.hunterCount()) * (0.5f / 3.0f);
                m_chase.addHunter(hunter);
                applySceneActor(world(), hunter, o, &sandboxBootGroundY, &m_sceneWorld.terrain);
                const TransformComponent* placed = world().get<TransformComponent>(hunter);
                const float placedX = placed ? placed->position.x : o.position.x;
                const float placedZ = placed ? placed->position.z : o.position.z;
                if (!m_chase.ai().walkability().walkableWorld(placedX, placedZ))
                {
                    DE_LOG_WARN(LogCategory::AI, "SandboxApp: scene {} at ({:.1f}, {:.1f}) is not walkable",
                        wolf ? "wolf" : "hunter", placedX, placedZ);
                }
                const int stamp = wolf ? boot.wolfStamp++ : boot.hunterStamp++;
                stampProceduralId(world(), hunter, std::format("sandbox/level/{}/{}", wolf ? "wolf" : "hunter", stamp), wolf ? "wolf" : "hunter");
                if (!wolf)
                {
                    const Entity torch = spawnGuardTorch(world(), hunter, placed ? placed->position : o.position);
                    stampProceduralId(world(), torch, std::format("sandbox/level/torch/{}", stamp), "torch");
                }
                return true;
            }
            if (m_chaseOk && !boot.pawnsLogged)
            {
                DE_LOG_INFO(LogCategory::AI, "SandboxApp: {} scene pawn(s)", m_chase.hunterCount());
                boot.pawnsLogged = true;
            }
            boot.phase = SandboxBootLoad::SpawnPhase::Models;
            continue;
        case SandboxBootLoad::SpawnPhase::Models:
            while (boot.modelCursor < boot.models.size())
            {
                const SceneObjectData& o = boot.models[boot.modelCursor++];
                const std::string modelPath = sceneModelPath(o);
                if (modelPath.empty())
                {
                    DE_LOG_WARN(LogCategory::Render, "SandboxApp: scene model has no path");
                    continue;
                }
                SceneObjectData placedObject = o;
                if (zeroScale(placedObject.scale))
                    placedObject.scale = Vector3f{ 1.0f, 1.0f, 1.0f };
                const Vector3f authoredScale = placedObject.scale;
                ContentError err;
                const Entity e = instantiateSceneObject(sceneHost(true), placedObject, &err);
                if (!e.valid())
                {
                    DE_LOG_WARN(LogCategory::Render, "SandboxApp: scene model '{}' did not attach ({})", modelPath, err.message);
                    return true;
                }
                registerSceneReplica(network(), world(), e, placedObject.archetype, placedObject.color);
                if (TransformComponent* placed = world().get<TransformComponent>(e))
                    placed->scale = authoredScale;
                stampProceduralId(world(), e, std::format("sandbox/level/model/{}", boot.modelStamp++), "model");
                ++boot.modelsAttached;
                return true;
            }
            if (!boot.models.empty() && !boot.modelsLogged)
            {
                DE_LOG_INFO(LogCategory::Render, "SandboxApp: {} scene model(s)", boot.modelsAttached);
                boot.modelsLogged = true;
            }
            boot.phase = SandboxBootLoad::SpawnPhase::Archetypes;
            continue;
        case SandboxBootLoad::SpawnPhase::Archetypes:
            while (boot.archetypeCursor < boot.archetypes.size())
            {
                const SceneObjectData& o = boot.archetypes[boot.archetypeCursor++];
                spawnAuthoredArchetype(o);
                return true;
            }
            boot.phase = SandboxBootLoad::SpawnPhase::SpawnerMarker;
            continue;
        case SandboxBootLoad::SpawnPhase::SpawnerMarker:
            if (boot.spawners.empty())
            {
                boot.phase = SandboxBootLoad::SpawnPhase::MovieMarker;
                continue;
            }
            if (!boot.spawnerMarkerReady)
            {
                MeshData markerMesh;
                if (CreateCube(markerMesh, 1.0f))
                {
                    boot.spawnerMarker = internAndUploadProceduralModel(renderer(), assets(), std::move(markerMesh),
                        internSolidMaterial(assets(), 255, 158, 38, 255, "runtime:/scene/spawner-mat"), "runtime:/scene/spawner");
                }
                boot.spawnerMarkerReady = true;
                return true;
            }
            boot.phase = SandboxBootLoad::SpawnPhase::Spawners;
            continue;
        case SandboxBootLoad::SpawnPhase::Spawners:
        {
            int spawned = 0;
            while (boot.spawnerCursor < boot.spawners.size() && spawned < kBootSpawnBatch)
            {
                const SceneObjectData& o = boot.spawners[boot.spawnerCursor++];
                SceneObjectData placedObject = o;
                if (zeroScale(placedObject.scale))
                    placedObject.scale = Vector3f{ 0.8f, 0.8f, 0.8f };
                placedObject.position.y = m_sceneWorld.terrain.heightAtWorld(o.position.x, o.position.z) + 0.5f * std::fabs(placedObject.scale.y);
                ContentError err;
                const Entity e = instantiateSceneObject(sceneHost(false), placedObject, &err);
                if (!e.valid())
                    DE_LOG_WARN(LogCategory::AI, "SandboxApp: spawner failed ({})", err.message);
                else
                {
                    registerSceneReplica(network(), world(), e, placedObject.archetype, placedObject.color);
                    stampProceduralId(world(), e, std::format("sandbox/level/spawner/{}", boot.spawnerStamp++), "spawner");
                }
                ++spawned;
            }
            if (spawned > 0)
                return true;
            if (!boot.spawnersLogged)
            {
                DE_LOG_INFO(LogCategory::AI, "SandboxApp: {} spawner(s)", boot.spawners.size());
                boot.spawnersLogged = true;
            }
            boot.phase = SandboxBootLoad::SpawnPhase::MovieMarker;
            continue;
        }
        case SandboxBootLoad::SpawnPhase::MovieMarker:
            if (boot.movies.empty())
            {
                boot.phase = SandboxBootLoad::SpawnPhase::Bind;
                continue;
            }
            if (!boot.movieMarkerReady)
            {
                MeshData markerMesh;
                if (CreateCube(markerMesh, 1.0f))
                {
                    boot.movieMarker = internAndUploadProceduralModel(renderer(), assets(), std::move(markerMesh),
                        internSolidMaterial(assets(), 64, 115, 242, 255, "runtime:/scene/movie-mat"), "runtime:/scene/movie");
                }
                boot.movieMarkerReady = true;
                return true;
            }
            boot.phase = SandboxBootLoad::SpawnPhase::Movies;
            continue;
        case SandboxBootLoad::SpawnPhase::Movies:
        {
            int spawned = 0;
            while (boot.movieCursor < boot.movies.size() && spawned < kBootSpawnBatch)
            {
                const SceneObjectData& o = boot.movies[boot.movieCursor++];
                SceneObjectData placedObject = o;
                if (zeroScale(placedObject.scale))
                    placedObject.scale = Vector3f{ 0.8f, 0.8f, 0.8f };
                placedObject.position.y = m_sceneWorld.terrain.heightAtWorld(o.position.x, o.position.z) + 0.5f * std::fabs(placedObject.scale.y);
                ContentError err;
                const Entity e = instantiateSceneObject(sceneHost(false), placedObject, &err);
                if (!e.valid())
                    DE_LOG_WARN(LogCategory::Media, "SandboxApp: movie failed ({})", err.message);
                else
                {
                    registerSceneReplica(network(), world(), e, placedObject.archetype, placedObject.color);
                    stampProceduralId(world(), e, std::format("sandbox/level/movie/{}", boot.movieStamp++), "movie");
                }
                ++spawned;
            }
            if (spawned > 0)
                return true;
            if (!boot.moviesLogged)
            {
                DE_LOG_INFO(LogCategory::Media, "SandboxApp: {} movie cue(s)", boot.movies.size());
                boot.moviesLogged = true;
            }
            boot.phase = SandboxBootLoad::SpawnPhase::Bind;
            continue;
        }
        case SandboxBootLoad::SpawnPhase::Bind:
            if (m_chaseOk && !boot.physicsBound)
            {
                bindCharacterPhysics();
                boot.physicsBound = true;
                return true;
            }
            while (m_chaseOk && boot.stockCursor < static_cast<size_t>(m_chase.hunterCount()))
            {
                const Entity e = m_chase.hunterEntity(static_cast<int>(boot.stockCursor++));
                if (!isInteractGuard(world(), e))
                    continue;
                stockHunter(e);
                return true;
            }
            boot.phase = SandboxBootLoad::SpawnPhase::Packs;
            continue;
        case SandboxBootLoad::SpawnPhase::Packs:
            m_demo.placeHealthPacks();
            boot.phase = SandboxBootLoad::SpawnPhase::Throwables;
            return true;
        case SandboxBootLoad::SpawnPhase::Throwables:
            if (const TransformComponent* start = m_chase.walker().valid() ? world().get<TransformComponent>(m_chase.walker()) : nullptr)
            {
                stockMissingThrowablePickups(world(), pins(), assets(), renderer(), m_sceneWorld.physics, start->position,
                    worldHasThrowablePickup(world(), "rock"), worldHasThrowablePickup(world(), "bottle"));
            }
            boot.phase = SandboxBootLoad::SpawnPhase::Hybrid;
            return true;
        case SandboxBootLoad::SpawnPhase::Hybrid:
            m_demo.spawnHybridLocalLights();
            boot.phase = SandboxBootLoad::SpawnPhase::Session;
            return true;
        case SandboxBootLoad::SpawnPhase::Session:
            m_loadedScenePath = boot.scenePath;
            ensureSessionEntity();
            installSaveHost();
            m_save.captureBaseline(world());
            resetMovieCues(world());
            m_bootSlice = BootSlice::Done;
            if (boot.runScene && boot.sceneOk)
                m_sceneStarted = true;
            return true;
        }
    }
    return true;
}
