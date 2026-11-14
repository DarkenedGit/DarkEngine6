#include "SandboxApp.h"
#include "SandboxInternal.h"

#include "Assets/Image.h"
#include "Assets/Material.h"
#include "Assets/Model.h"
#include "Audio/SoundComponents.h"
#include "Character/ShieldView.h"
#include "Core/EntityPins.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "Gameplay/ThrowableBelt.h"
#include "Gameplay/Upgrades.h"
#include "Math/MathHelper.h"
#include "Media/MovieCue.h"
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
    int                       modelStamp = 0;
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
        if (!loadAndBakeIbl(renderer(), assets(), m_ibl, m_iblImageId))
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
            m_sceneStealth = boot.scene.stealth;
            applySceneAwareness(m_chase.ai(), m_sceneStealth);
            m_sceneQuest = boot.scene.quest;
            applySceneAtmosphere(m_env, boot.scene);
            {
                const ExposureSceneDesc exposure = boot.scene.exposure.present ? boot.scene.exposure : ExposureSceneDesc{};
                m_autoExposure.mode        = exposure.autoExposure ? ExposureMode::Auto : ExposureMode::Manual;
                m_autoExposure.evBias      = exposure.evBias;
                m_autoExposure.maxEv       = exposure.maxEv;
                m_autoExposure.adaptBright = exposure.adaptBright;
                m_autoExposure.adaptDark   = exposure.adaptDark;
                resetAutoExposure(m_autoExposureState);
                DE_LOG_INFO(LogCategory::Render, "SandboxApp: exposure auto={}", exposure.autoExposure);
            }
            m_foliageDensity.dirtTreesPerM2    = boot.scene.terrain.foliage.dirtTreesPerM2;
            m_foliageDensity.dirtFlowersPerM2  = boot.scene.terrain.foliage.dirtFlowersPerM2;
            m_foliageDensity.grassTreesPerM2   = boot.scene.terrain.foliage.grassTreesPerM2;
            m_foliageDensity.grassFlowersPerM2 = boot.scene.terrain.foliage.grassFlowersPerM2;
            m_foliageDensity.rockPerM2         = boot.scene.terrain.foliage.rockPerM2;
            m_foliageDensity.grassPerM2        = boot.scene.terrain.foliage.grassPerM2;
            m_foliageDensity.treeMinNormalY    = boot.scene.terrain.foliage.treeMinNormalY;
            m_foliageDensity.flowerMinNormalY  = boot.scene.terrain.foliage.flowerMinNormalY;
            m_foliageDensity.tuftMinNormalY    = boot.scene.terrain.foliage.tuftMinNormalY;
            m_foliageDensity.rockMinNormalY    = boot.scene.terrain.foliage.rockMinNormalY;
            m_foliageDensity.treeMaxLeanDeg    = boot.scene.terrain.foliage.treeMaxLeanDeg;
            m_foliageDensity.rockMaxPitchDeg   = boot.scene.terrain.foliage.rockMaxPitchDeg;
            m_foliageDensity.treeSpacingM      = boot.scene.terrain.foliage.treeSpacingM;
            m_foliageDensity.rockSpacingM      = boot.scene.terrain.foliage.rockSpacingM;
            m_foliageDensity.patchStrength     = boot.scene.terrain.foliage.patchStrength;
            m_foliageDensity.patchLargeM       = boot.scene.terrain.foliage.patchLargeM;
            m_foliageDensity.patchMediumM      = boot.scene.terrain.foliage.patchMediumM;
            m_foliageDensity.patchFineM        = boot.scene.terrain.foliage.patchFineM;
            m_foliageDensity.seed              = boot.scene.terrain.foliage.seed;
            m_foliageDensity.treeModel         = boot.scene.terrain.foliage.treeModel;
            m_foliageDensity.flowerModel       = boot.scene.terrain.foliage.flowerModel;
            m_foliageDensity.rockModel         = boot.scene.terrain.foliage.rockModel;
            m_foliageDensity.grassModel        = boot.scene.terrain.foliage.grassModel;
            m_leafShedScale                    = boot.scene.terrain.foliage.shedScale;
            m_wind = boot.scene.terrain.wind.settings;
            clampWindSettings(m_wind);
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
            applyWindToGrass(m_wind, grassParams);
            if (!m_grassPipeline.isValid())
                grassParams.enabled = false;
            m_grassField.setParams(grassParams);
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
                && m_terrain.createFromHeightMap(std::move(boot.worldMaps.height), boot.chunkCells);
            if (loaded)
            {
                boot.matSplat = boot.worldMaps.splat;
                if (!m_terrain.setWorkingSplat(std::move(boot.worldMaps.splat)))
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
                ok = m_terrain.createFromCoarse(boot.gridDesc, std::move(boot.loadedHeight));
            if (ok)
            {
                boot.loadedScene  = true;
                m_haveTerrainSea  = true;
                m_terrainSeaLevel = boot.scene.terrain.grid.seaLevel;
                DE_LOG_INFO(LogCategory::Render, "SandboxApp: streaming scene terrain {}x{} tiles", boot.gridDesc.tilesX, boot.gridDesc.tilesZ);
            }
            else
                DE_LOG_ERROR(LogCategory::Render, "SandboxApp: scene terrain.grid failed — FBM fallback");
        }
        else if (boot.kind == SandboxBootLoad::TerrainKind::HeightFile)
        {
            if (boot.terrainStatus == JobStatus::Ok
                && m_terrain.createFromHeightMap(std::move(boot.loadedHeight), boot.chunkCells))
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
            if (!m_terrain.createFromHeightMap(std::move(base), 16))
            {
                DE_LOG_FATAL("SandboxApp: terrain create failed");
                requestQuit();
                return false;
            }
            m_terrain.setWorkingSplat(SplatMap(boot.matSplat));
            if (bareGround)
            {
                m_showTerrain = false;
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
            const AABox3f terrainBox = m_terrain.bounds();
            const WaterSceneDesc& waterScene = boot.scene.water;
            float waterLevel = Lerp(terrainBox.Min.y, terrainBox.Max.y, waterScene.levelFraction);
            if (waterScene.hasLevel)
                waterLevel = waterScene.level;
            else if (m_haveTerrainSea)
                waterLevel = m_terrainSeaLevel;
            else if (!m_showTerrain)
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
                const HeightMap* height = m_terrain.editableWorking();
                const SplatMap*  splat  = m_terrain.editableWorkingSplat();
                if (height && splat)
                {
                    boot.foliageIn.height    = height;
                    boot.foliageIn.splat     = splat;
                    boot.foliageIn.density   = m_foliageDensity;
                    boot.foliageIn.trails    = boot.scene.terrain.trails.paths;
                    boot.foliageIn.tilesX    = m_terrain.tilesX();
                    boot.foliageIn.tilesZ    = m_terrain.tilesZ();
                    const int cells = m_terrain.tileCells();
                    boot.foliageIn.tileCells = cells > 0 ? static_cast<uint32_t>(cells) : 0u;
                    boot.foliageIn.cellSize  = m_terrain.cellSize();
                    boot.foliageIn.origin    = m_terrain.origin();
                    boot.foliageIn.seaLevel  = height->origin().y;
                    m_foliagePrototypes.sync(renderer(), assets(), m_foliageDensity);
                    const AssetRef<Model>& treeModel = m_foliagePrototypes.model(FoliageKind::Tree);
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
                        if (object.type != SceneObjectType::Water)
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
                m_foliage.swap(boot.foliageOut.records);
                m_terrain.setWorkingCover(std::move(boot.foliageOut.cover));
                m_grassField.markAllDirty();
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
            if (!uploadWorldEngineMaterial(renderer(), assets(), boot.matSplat, boot.worldMaps, m_terrainMaterial, nullptr, decoded))
            {
                DE_LOG_FATAL("SandboxApp: World Engine material failed");
                requestQuit();
                return false;
            }
            DE_LOG_INFO(LogCategory::Render, "Terrain: World Engine albedo and roughness");
        }
        else if (tryCreateTerrainFromContent(renderer(), assets(), boot.matSplat, m_terrainMaterial))
            DE_LOG_INFO(LogCategory::Render, "Terrain: using content layer set");
        else
        {
            DE_LOG_INFO(LogCategory::Render, "Terrain: checkers fallback");
            if (!m_terrainMaterial.createDefault(renderer(), boot.matSplat))
            {
                DE_LOG_FATAL("SandboxApp: terrain material create failed");
                requestQuit();
                return false;
            }
        }
        m_terrainMaterial.setLayerSamplingRaw(renderer().device(), renderer().debugState().legacyUnormAlbedo);
        m_bootSlice = BootSlice::PrimeStream;
        return true;
    }
    case BootSlice::PrimeStream:
    {
        HitchClock clock("PrimeStream");
        m_terrain.updateStreaming(Vector3f{ 0.0f, 50.0f, -80.0f }, &renderer(), &m_terrainMaterial);
        m_bootSlice = BootSlice::CoarseHeight;
        return true;
    }
    case BootSlice::CoarseHeight:
    {
        HitchClock clock("CoarseHeight");
        if (!m_terrain.uploadCoarseHeightTexture(renderer()))
        {
            DE_LOG_FATAL("SandboxApp: coarse height texture upload failed");
            requestQuit();
            return false;
        }
        if (m_terrain.heightTexture().valid())
        {
            renderer().setHeightSrv(m_terrain.heightTexture().cpuHandle());
            m_frame.scene().waterPipeline().setHeightSrv(renderer().device(), m_terrain.heightTexture().cpuHandle());
        }
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
        if (!m_water.create(m_terrain.coarse(), waterDesc))
        {
            DE_LOG_FATAL("SandboxApp: water create failed");
            requestQuit();
            return false;
        }
        m_water.updateLod(Vector3f{ 0.0f, 50.0f, -80.0f });
        m_bootSlice = BootSlice::WaterGpu;
        return true;
    }
    case BootSlice::WaterGpu:
    {
        HitchClock clock("WaterGpu");
        if (!m_water.createGpu(renderer()))
        {
            DE_LOG_FATAL("SandboxApp: water GPU upload failed");
            requestQuit();
            return false;
        }
        DE_LOG_INFO("SandboxApp: water level {:.2f}, {} wet chunks", boot.waterLevel, m_water.wetChunkCount());
        m_bootSlice = BootSlice::StreamRibbon;
        return true;
    }
    case BootSlice::StreamRibbon:
    {
        while (boot.streamCursor < boot.scene.objects.size() && m_streams.size() < 16)
        {
            const SceneObjectData& o = boot.scene.objects[boot.streamCursor++];
            if (o.type != SceneObjectType::Stream || !o.hasStream)
                continue;
            Dark::StreamDesc desc;
            desc.width        = o.streamWidth;
            desc.flowSpeed    = o.streamFlowSpeed;
            desc.bedClearance = 0.45f;
            desc.pointsXZ     = o.streamPoints;
            MeshData cpu;
            SandboxStream slot;
            std::string err;
            if (!buildStreamRibbon(m_terrain.coarse(), desc, boot.waterLevel, cpu, &slot.bounds, &err))
                return true;
            if (!Mesh::tryCreate(renderer(), cpu, slot.mesh))
            {
                DE_LOG_ERROR(LogCategory::Render, "SandboxApp: stream upload failed");
                return true;
            }
            slot.flowSpeed = o.streamFlowSpeed;
            m_streams.push_back(std::move(slot));
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
        if (!CreateCross(crossData, 1.0f, 0.30f, 0.22f) || !Mesh::tryCreate(renderer(), crossData, m_crossMesh))
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
            renderer().setShadowSrv(m_frame.scene().shadows().srvCpu());
            m_frame.scene().skyPipeline().setShadowSrv(renderer().device(), m_frame.scene().shadows().srvCpu());
            m_frame.scene().waterPipeline().setShadowSrv(renderer().device(), m_frame.scene().shadows().srvCpu());
            m_frame.scene().localShadows().setDepthPipeline(&m_frame.scene().shadows().pipeline());
            {
                const LocalShadowSystem::CpuSrvs localSrvs = m_frame.scene().localShadows().cpuSrvs();
                renderer().setLocalShadowSrvs(localSrvs.arraySrv, localSrvs.recordsSrv);
            }
            m_demoScene = boot.scenePath.filename() == "level.json";
            if (!m_demoScene)
                m_npcInfo.enabled = false;
            if (!m_demoScene)
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
                if (o.type == SceneObjectType::Hunter || o.type == SceneObjectType::Wolf)
                    boot.pawns.push_back(o);
                if (o.type == SceneObjectType::Spawner)
                    boot.spawners.push_back(o);
                if (o.type == SceneObjectType::Movie)
                    boot.movies.push_back(o);
                if (o.type == SceneObjectType::Cube)
                    boot.boxes.push_back(o);
                if (o.type == SceneObjectType::PointLight || o.type == SceneObjectType::SpotLight)
                    boot.lights.push_back(o);
                if (o.type == SceneObjectType::Model)
                    boot.models.push_back(o);
                if (o.type == SceneObjectType::Player)
                    m_chase.setPlayerStart(o.position);
                if (o.type == SceneObjectType::CloudVolume)
                    boot.clouds.push_back(o);
            }
            boot.phase = SandboxBootLoad::SpawnPhase::Clouds;
            return true;
        case SandboxBootLoad::SpawnPhase::Clouds:
        {
            int spawned = 0;
            while (boot.cloudCursor < boot.clouds.size() && spawned < kBootSpawnBatch)
            {
                const SceneObjectData& o = boot.clouds[boot.cloudCursor++];
                CloudVolumeDesc desc{};
                if (o.hasCloud)
                    cloudDescFromSceneData(o, desc);
                spawnCloud(o.position, o.scale, desc);
                ++boot.spawnedClouds;
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
            renderer().setShadowSrv(m_frame.scene().shadows().srvCpu());
            m_frame.scene().skyPipeline().setShadowSrv(renderer().device(), m_frame.scene().shadows().srvCpu());
            m_frame.scene().waterPipeline().setShadowSrv(renderer().device(), m_frame.scene().shadows().srvCpu());
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
            m_viewCamera.SetLens(1.04719755f, aspect, 0.05f, 1.0e5f);
            m_viewCamera.LookAt(Vector3f(0.0f, 48.0f, -86.0f), Vector3f(0.0f, 8.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f));
            m_camera = world().createEntity();
            world().emplace<TagComponent>(m_camera, "Main Camera");
            world().emplace<TransformComponent>(m_camera, m_viewCamera.GetPosition(), Quaternion::IDENTITY, Vector3f{ 1, 1, 1 });
            world().emplace<CameraComponent>(m_camera, 60.0f, 0.5f, 1.0e5f, true);
            world().emplace<AudioListenerComponent>(m_camera);
            attachCameraSounds(world(), pins(), assets(), audio(), m_camera);
            boot.phase = SandboxBootLoad::SpawnPhase::Cube;
            return true;
        }
        case SandboxBootLoad::SpawnPhase::Cube:
        {
            const float aspect = (renderer().height() > 0) ? static_cast<float>(renderer().width()) / static_cast<float>(renderer().height()) : 1.0f;
            const float groundY = m_terrain.heightAtWorld(0.0f, 0.0f) + 0.5f;
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
                m_terrain.tilesX(),
                m_terrain.tilesZ());
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
                host.physics     = m_physics.valid() ? &m_physics : nullptr;
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
                if (!m_buildingDoors.empty())
                {
                    HitchClock clock("BuildingDoorModel");
                    boot.fixtures.doorModel = loadAndUploadModel(renderer(), assets(), "models/unit_cube.gltf");
                    return true;
                }
            }
            if (boot.buildingModelStep == 1)
            {
                boot.buildingModelStep = 2;
                if (!m_buildingCameras.empty())
                {
                    HitchClock clock("BuildingCameraModel");
                    boot.fixtures.camModel = loadAndUploadModel(renderer(), assets(), "models/unit_cube.gltf");
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
            m_chaseOk = m_chase.init(renderer(), m_terrain, m_water, world(), pins(), assets());
            boot.phase = SandboxBootLoad::SpawnPhase::Quest;
            return true;
        case SandboxBootLoad::SpawnPhase::Quest:
            if (!m_sceneQuest.empty())
                startQuest(m_sceneQuest);
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
                    m_guardVoice.start(std::move(barks));
                else
                    DE_LOG_WARN(LogCategory::AI, "Guard barks: {}", barkError);
            }
            boot.phase = SandboxBootLoad::SpawnPhase::Grass;
            return true;
        case SandboxBootLoad::SpawnPhase::Grass:
            while (boot.grassCursor < boot.boxes.size())
            {
                const SceneObjectData& o = boot.boxes[boot.grassCursor++];
                if (!o.conceal)
                    continue;
                const uint8_t r = static_cast<uint8_t>(Math::Clamp(o.color[0], 0.0f, 1.0f) * 255.0f);
                const uint8_t g = static_cast<uint8_t>(Math::Clamp(o.color[1], 0.0f, 1.0f) * 255.0f);
                const uint8_t b = static_cast<uint8_t>(Math::Clamp(o.color[2], 0.0f, 1.0f) * 255.0f);
                const std::string key = std::format("runtime:/scene/grass-{}", boot.patchSeed);
                const Entity e = world().createEntity();
                world().emplace<TransformComponent>(e, Vector3f{ o.position.x, m_terrain.heightAtWorld(o.position.x, o.position.z), o.position.z }, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
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
                applySceneActor(world(), e, o, nullptr, nullptr);
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
                if (o.conceal || !o.zone.empty() || !o.pickup.empty())
                    continue;
                const uint8_t r = static_cast<uint8_t>(Math::Clamp(o.color[0], 0.0f, 1.0f) * 255.0f);
                const uint8_t g = static_cast<uint8_t>(Math::Clamp(o.color[1], 0.0f, 1.0f) * 255.0f);
                const uint8_t b = static_cast<uint8_t>(Math::Clamp(o.color[2], 0.0f, 1.0f) * 255.0f);
                const std::string key = std::format("runtime:/scene/box-{:02x}{:02x}{:02x}", r, g, b);
                MeshData mesh;
                if (!CreateCube(mesh, 1.0f))
                    return true;
                AssetRef<Model> box = internAndUploadProceduralModel(renderer(), assets(), std::move(mesh), internSolidMaterial(assets(), r, g, b, 255, key + "-mat"), key);
                if (!box)
                    return true;
                const Entity e = world().createEntity();
                world().emplace<TransformComponent>(e, o.position, o.rotation, o.scale);
                ModelComponent mc{};
                mc.modelAssetID = box->id;
                mc.castShadow   = true;
                setModelComponent(world(), pins(), assets(), e, mc);
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
            m_shield = spawnPlayerShield(world(), pins(), assets(), renderer());
            boot.phase = SandboxBootLoad::SpawnPhase::Pawns;
            return true;
        case SandboxBootLoad::SpawnPhase::Pawns:
            if (m_chaseOk && boot.pawnCursor < boot.pawns.size())
            {
                const SceneObjectData& o = boot.pawns[boot.pawnCursor++];
                const bool wolf = o.type == SceneObjectType::Wolf;
                TransformComponent xf{};
                xf.position = o.position;
                xf.rotation = o.rotation;
                const bool zeroScale = o.scale.x == 0.0f && o.scale.y == 0.0f && o.scale.z == 0.0f;
                xf.scale = zeroScale ? Vector3f{ 1.0f, 1.0f, 1.0f } : o.scale;
                const Entity hunter = m_chase.spawnListedHunter(world(), pins(), assets(), xf);
                if (hunter.valid())
                {
                    applySceneActor(world(), hunter, o, &sandboxBootGroundY, &m_terrain);
                    const TransformComponent* placed = world().get<TransformComponent>(hunter);
                    const float placedX = placed ? placed->position.x : xf.position.x;
                    const float placedZ = placed ? placed->position.z : xf.position.z;
                    if (!m_chase.ai().walkability().walkableWorld(placedX, placedZ))
                    {
                        DE_LOG_WARN(LogCategory::AI, "SandboxApp: scene {} at ({:.1f}, {:.1f}) is not walkable",
                            wolf ? "wolf" : "hunter", placedX, placedZ);
                    }
                    const CharacterHost host{ world(), renderer(), assets(), pins(), audio(), m_ground };
                    attachNpcVisual(host, hunter, wolf ? NpcKind::Wolf : NpcKind::Hunter, ModelAttach::AnimatedOnly);
                    const int stamp = wolf ? boot.wolfStamp++ : boot.hunterStamp++;
                    stampProceduralId(world(), hunter, std::format("sandbox/level/{}/{}", wolf ? "wolf" : "hunter", stamp), wolf ? "wolf" : "hunter");
                    if (!wolf)
                    {
                        const Entity torch = spawnGuardTorch(world(), hunter, placed ? placed->position : xf.position);
                        stampProceduralId(world(), torch, std::format("sandbox/level/torch/{}", stamp), "torch");
                    }
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
                if (o.modelPath.empty())
                {
                    DE_LOG_WARN(LogCategory::Render, "SandboxApp: scene model has no path");
                    continue;
                }
                const bool zeroScale = o.scale.x == 0.0f && o.scale.y == 0.0f && o.scale.z == 0.0f;
                TransformComponent xf{};
                xf.position = o.position;
                xf.rotation = o.rotation;
                xf.scale    = zeroScale ? Vector3f{ 1.0f, 1.0f, 1.0f } : o.scale;
                const Entity e = world().createEntity();
                const std::string name = std::filesystem::path(o.modelPath).stem().string();
                world().emplace<TagComponent>(e, name.empty() ? std::string("Model") : name);
                world().emplace<TransformComponent>(e, xf);
                const Vector3f authoredScale = xf.scale;
                if (!attachAnimatedCharacter(e, o.modelPath.c_str()))
                {
                    onEntityRemoved(world(), e, &pins());
                    world().destroyEntity(e);
                    DE_LOG_WARN(LogCategory::Render, "SandboxApp: scene model '{}' did not attach", o.modelPath);
                    return true;
                }
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
                if (boot.spawnerMarker)
                {
                    ModelComponent mc{};
                    mc.modelAssetID = boot.spawnerMarker->id;
                    mc.castShadow   = true;
                    setModelComponent(world(), pins(), assets(), e, mc);
                }
                stampProceduralId(world(), e, std::format("sandbox/level/spawner/{}", boot.spawnerStamp++), "spawner");
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
                const bool zeroScale = o.scale.x == 0.0f && o.scale.y == 0.0f && o.scale.z == 0.0f;
                const Vector3f scale = zeroScale ? Vector3f{ 0.8f, 0.8f, 0.8f } : o.scale;
                TransformComponent xf{};
                xf.rotation = o.rotation;
                xf.scale    = scale;
                xf.position = o.position;
                xf.position.y = m_terrain.heightAtWorld(o.position.x, o.position.z) + 0.5f * std::fabs(scale.y);
                const Entity e = world().createEntity();
                world().emplace<TagComponent>(e, "Movie");
                world().emplace<TransformComponent>(e, xf);
                applyMovieCue(world(), e, o);
                if (boot.movieMarker)
                {
                    ModelComponent mc{};
                    mc.modelAssetID = boot.movieMarker->id;
                    mc.castShadow   = true;
                    setModelComponent(world(), pins(), assets(), e, mc);
                }
                stampProceduralId(world(), e, std::format("sandbox/level/movie/{}", boot.movieStamp++), "movie");
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
                stockMissingThrowablePickups(world(), pins(), assets(), renderer(), m_physics, start->position,
                    worldHasThrowablePickup(world(), "rock"), worldHasThrowablePickup(world(), "bottle"));
            }
            boot.phase = SandboxBootLoad::SpawnPhase::Hybrid;
            return true;
        case SandboxBootLoad::SpawnPhase::Hybrid:
            m_demo.spawnHybridLocalLights();
            boot.phase = SandboxBootLoad::SpawnPhase::Session;
            return true;
        case SandboxBootLoad::SpawnPhase::Session:
            ensureSessionEntity();
            installSaveHost();
            m_save.captureBaseline(world());
            resetMovieCues(world());
            m_loadedScenePath = boot.scenePath;
            m_bootSlice = BootSlice::Done;
            if (boot.runScene && boot.sceneOk)
                m_sceneStarted = true;
            return true;
        }
    }
    return true;
}
