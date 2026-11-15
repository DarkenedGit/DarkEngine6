#pragma once

#include "AI/Awareness.h"
#include "Math/Quaternion.h"
#include "Math/Vector2f.h"
#include "Math/Vector3f.h"
#include "Terrain/FoliageFile.h"
#include "Terrain/TrailPath.h"
#include "Sky/Environment.h"
#include "Terrain/WindTypes.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Dark
{

    enum class SceneMode : uint8_t
    {
        Scene3D = 0,
        Scene2D,
    };

    inline const char* toString(SceneMode mode)
    {
        return mode == SceneMode::Scene2D ? "2d" : "3d";
    }

    inline bool tryParseSceneMode(std::string_view s, SceneMode& out)
    {
        if (s == "2d" || s == "2D")
        {
            out = SceneMode::Scene2D;
            return true;
        }
        if (s == "3d" || s == "3D" || s.empty())
        {
            out = SceneMode::Scene3D;
            return true;
        }
        return false;
    }

    // Live editor objects are ECS entities with EditorObjectComponent (Editor/EditorObject.h).
    // SceneObjectData is the JSON DTO only — not a parallel runtime world.

    // One placed object. Pose and color are columns. Component payloads and placement markers
    // (conceal, zone, patrol, and the rest) live in overridesJson. An unknown archetype stores
    // the original object JSON there and is written back unchanged. Read through SceneObjectSpawn.
    struct SceneObjectData
    {
        Math::Vector3f   position{ 0, 0, 0 };
        Math::Quaternion rotation{ 1, 0, 0, 0 };
        Math::Vector3f   scale{ 1, 1, 1 };
        float            color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
        std::string      archetype;
        std::string      overridesJson;
    };

    // World-level terrain JSON DTO. Paths are virtual or sidecar filenames.
    struct TerrainLayerSceneDesc
    {
        std::string albedo;
        std::string normal;
        std::string orm;
        float       tiling = 8.0f;
        float       tint[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    struct TerrainGridSceneDesc
    {
        uint32_t       tilesX       = 4;
        uint32_t       tilesZ       = 4;
        uint32_t       tileCells    = 512;
        float          cellSize     = 1.0f;
        Math::Vector3f origin{ -1024.0f, 0.0f, -1024.0f };
        float          heightScale  = 80.0f;
        uint32_t       seed         = 1337u;
        std::string    coarseFile;
        std::string    tileDir;
        float          seaLevel     = 0.0f;
        int            residentRing = 5;
    };

    struct FoliageSceneDesc
    {
        bool        present            = false;
        uint32_t    seed               = 1337u;
        float       dirtTreesPerM2     = 0.002f;
        float       dirtFlowersPerM2   = 0.006f;
        float       grassTreesPerM2    = 0.003f;
        float       grassFlowersPerM2  = 0.008f;
        float       rockPerM2          = 0.004f;
        float       grassPerM2         = 0.0f;
        float       treeMinNormalY     = 0.87f;
        float       flowerMinNormalY   = 0.82f;
        float       tuftMinNormalY     = 0.82f;
        float       rockMinNormalY     = 0.40f;
        float       treeMaxLeanDeg     = 7.0f;
        float       rockMaxPitchDeg    = 30.0f;
        float       treeSpacingM       = 3.0f;
        float       rockSpacingM       = 0.0f;
        float       patchStrength      = 0.0f;
        float       patchLargeM        = 80.0f;
        float       patchMediumM       = 22.0f;
        float       patchFineM         = 4.0f;
        bool        stale              = false;
        float       shedScale          = 1.0f;
        std::string treeModel   = "models/BirchTree/scene.gltf";
        std::string flowerModel = "models/Dandelion/dandelion_01_2k.gltf";
        std::string rockModel   = "models/RockMoss/rock_moss_set_01_2k.gltf";
        std::string grassModel  = "models/Grass/grass_medium_01_2k.gltf";
        std::vector<Terrain::FoliageSpecies> trees   = Terrain::defaultTreeSpecies();
        std::vector<Terrain::FoliageSpecies> flowers = Terrain::defaultFlowerSpecies();
        std::vector<Terrain::FoliageSpecies> rocks   = Terrain::defaultRockSpecies();
        std::vector<Terrain::FoliageSpecies> tufts   = Terrain::defaultTuftSpecies();
    };

    struct GrassSceneDesc
    {
        bool     enabled          = false;
        float    heightMetres     = 0.55f;
        float    flexibility      = 0.65f;
        float    densityScale     = 1.0f;
        float    windBaseYaw      = 1.37f;
        float    windDeflection   = 0.55f;
        float    windTipMetres    = 0.40f;
        float    windSpatialFreq  = 0.004f;
        float    windTemporalFreq = 0.015f;
        float    faceEdge         = 0.35f;
        float    faceYaw          = 0.65f;
        float    faceLean         = 0.30f;
        float    faceSteep        = 0.55f;
        uint32_t seed             = 1337u;
    };

    struct WindSceneDesc
    {
        bool                  present  = false;
        Terrain::WindSettings settings = Terrain::defaultWindSettings();
    };

    struct TerrainSceneDesc
    {
        static constexpr int kBindLayoutV1 = 1;

        int                   bindLayout     = kBindLayoutV1;
        int                   chunkCells     = 16;
        float                 heightBlendK   = 0.5f;
        float                 heightBlendT   = 0.1f;
        float                 triplanarSlope = 0.45f;
        std::string           heightFile;
        std::string           splatFile;
        std::string           source;       // World Engine folder, content-relative or absolute
        float                 worldSize    = 1024.0f;
        float                 importHeight = 480.0f;
        TerrainLayerSceneDesc layers[4];
        int                   layerCount = 0;
        bool                  hasGrid    = false;
        TerrainGridSceneDesc  grid;
        FoliageSceneDesc      foliage;
        GrassSceneDesc        grass;
        WindSceneDesc         wind;
        Terrain::TrailSet     trails;
    };

    // Celestial clock and weather. Absent from a file means the Environment defaults.
    struct SkySceneDesc
    {
        bool        present       = false;
        float       timeOfDay     = Sky::kDefaultTimeOfDay; // hours, [0, 24)
        float       dayOfYear     = 172.0f;
        float       latitude      = 47.6f; // degrees
        float       timeScale     = 0.0f;  // hours advanced per real second; 0 holds the clock
        std::string weather       = "partly"; // clear, partly, overcast, storm, or custom
        float       cloudCoverage = 0.35f;
        float       turbidity     = 2.4f;
        float       windSpeed     = 0.04f;
        float       windDir[2]{ 1.0f, 0.2f };
        float       rain          = 0.0f;

        float       cloudAltitude     = 2000.0f;
        float       cloudAmount       = 1.0f;
        float       cloudWindSpeedMps = 8.0f;
    };

    // Fog knobs. When autoFromWeather is set, evaluate() writes density and color.
    struct FogSceneDesc
    {
        bool  present         = false;
        bool  autoFromWeather = true;
        float distanceScale   = 1.0f;
        float heightScale     = 1.0f;
        float valleyScale     = 1.0f;
        float distanceDensity = 0.004f;
        float heightDensity   = 0.0f;
        float valleyDensity   = 0.012f;
        float heightFalloff   = 0.06f;
        float valleyHeight    = 14.0f;
        float color[3]{ 0.55f, 0.62f, 0.72f };
    };

    struct WaterWaveSceneDesc
    {
        float angleFromFlow = 0.0f;
        float frequency     = 1.0f;
        float amplitude     = 0.1f;
        float speed         = 1.0f;
    };

    // One water sheet for a level. hasLevel false keeps a fraction of the terrain's vertical range.
    struct WaterSceneDesc
    {
        bool               present          = false;
        bool               hasLevel         = false;
        float              level            = 0.0f;
        float              levelFraction    = 0.38f;
        int                chunkCells       = 16;
        float              lodDistances[8]{ 40.0f, 80.0f, 160.0f, 320.0f, 640.0f, 1280.0f, 2560.0f, 5120.0f };
        int                lodDistanceCount = 5;
        float              flowDir[2]{ 1.0f, 0.35f };
        float              flowStrength     = 0.85f;
        float              steepness        = 0.55f;
        float              amplitudeScale   = 1.0f;
        float              speedScale       = 1.0f;
        float              flowSpeed        = 0.4f;
        float              foam             = 1.0f;
        float              foamWidthScale   = 1.0f;
        float              detail           = 1.0f;
        WaterWaveSceneDesc waves[4]{};
        int                waveCount        = 0; // 0 keeps the built-in Gerstner set
    };

    // Meter on top of the artistic exposure. autoExposure defaults off.
    struct ExposureSceneDesc
    {
        bool  present      = false;
        bool  autoExposure = false;
        float evBias       = 0.0f;
        float maxEv        = 1.5f;
        float adaptBright  = 6.0f; // 1/s, view got brighter, exposure falls
        float adaptDark    = 1.0f; // 1/s, view got darker, exposure rises
    };

    // Opt-in guard awareness. Hear fields apply only when their hasHear* flag is set.
    // A missing block leaves awareness off and keeps the host's prey hear radii.
    struct StealthSceneDesc
    {
        bool  present            = false;
        bool  awareness          = false;
        bool  hasHearStand       = false;
        bool  hasHearWalk        = false;
        bool  hasHearSprint      = false;
        bool  hasHearCrouchWalk  = false;
        bool  hasHearCrouchStill = false;
        float hearStand          = 0.0f;
        float hearWalk           = 0.0f;
        float hearSprint         = 0.0f;
        float hearCrouchWalk     = 0.0f;
        float hearCrouchStill    = 0.0f;
        // Partial overlay on content/ai/awareness.json. settingsMask marks authored keys.
        bool                  hasSettings   = false;
        uint64_t              settingsMask  = 0;
        AI::AwarenessSettings settings{};
    };

    // A kit-built building placed in the scene: content/buildings/<id>.building.json.
    struct SceneBuildingDesc
    {
        std::string    id;
        Math::Vector3f position{};
        float          yaw = 0.0f;
    };

    struct SceneFileData
    {
        int         version = 2;
        std::string name    = "untitled";
        SceneMode   mode    = SceneMode::Scene3D;
        Math::Vector2f worldMin{ 0.0f, 0.0f };
        Math::Vector2f worldMax{ 96.0f, 22.0f };
        std::string environment      = "env/studio_gradient.hdr";
        float       iblIntensity     = 1.0f;
        float       iblRotationRadY  = 0.0f; // radians; ImGui shows degrees
        bool              hasTerrain = false;
        TerrainSceneDesc  terrain;
        SkySceneDesc      sky;
        FogSceneDesc      fog;
        WaterSceneDesc    water;
        ExposureSceneDesc exposure;
        StealthSceneDesc  stealth;
        std::string       quest;
        std::vector<SceneBuildingDesc> buildings;
        std::vector<SceneObjectData> objects;
    };

} // namespace Dark
