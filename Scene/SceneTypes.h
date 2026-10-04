#pragma once

#include "Math/Quaternion.h"
#include "Math/Vector2f.h"
#include "Math/Vector3f.h"

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

    // Serializable prop kind used by the level editor.
    enum class SceneObjectType : uint8_t
    {
        Cube = 0,
        Sphere,
        ParticleEmitter,
        Platform,
        Coin,
        Spawn,
        PointLight,
        SpotLight,
        AmbientLight,
        DirectionalLight,
        Player,
        Hunter,
        Wolf,
        Model,
        Water,
        CloudVolume,
        Count
    };

    inline bool isScene3DType(SceneObjectType t)
    {
        return t == SceneObjectType::Cube || t == SceneObjectType::Sphere || t == SceneObjectType::ParticleEmitter
            || t == SceneObjectType::PointLight || t == SceneObjectType::SpotLight
            || t == SceneObjectType::AmbientLight || t == SceneObjectType::DirectionalLight
            || t == SceneObjectType::Player || t == SceneObjectType::Hunter || t == SceneObjectType::Wolf
            || t == SceneObjectType::Model || t == SceneObjectType::Water || t == SceneObjectType::CloudVolume;
    }

    inline bool isPawnType(SceneObjectType t)
    {
        return t == SceneObjectType::Player || t == SceneObjectType::Hunter || t == SceneObjectType::Wolf;
    }

    inline bool usesModelBounds(SceneObjectType t)
    {
        return isPawnType(t) || t == SceneObjectType::Model;
    }

    inline bool isGlobalLightType(SceneObjectType t)
    {
        return t == SceneObjectType::AmbientLight || t == SceneObjectType::DirectionalLight;
    }

    inline bool isScene2DType(SceneObjectType t)
    {
        return t == SceneObjectType::Platform || t == SceneObjectType::Coin || t == SceneObjectType::Spawn;
    }

    inline const char* toString(SceneObjectType t)
    {
        switch (t)
        {
        case SceneObjectType::Cube:            return "cube";
        case SceneObjectType::Sphere:          return "sphere";
        case SceneObjectType::ParticleEmitter: return "particle_emitter";
        case SceneObjectType::Platform:        return "platform";
        case SceneObjectType::Coin:            return "coin";
        case SceneObjectType::Spawn:           return "spawn";
        case SceneObjectType::PointLight:      return "point_light";
        case SceneObjectType::SpotLight:       return "spot_light";
        case SceneObjectType::AmbientLight:    return "ambient_light";
        case SceneObjectType::DirectionalLight: return "directional_light";
        case SceneObjectType::Player:          return "player";
        case SceneObjectType::Hunter:          return "hunter";
        case SceneObjectType::Wolf:            return "wolf";
        case SceneObjectType::Model:           return "model";
        case SceneObjectType::Water:           return "water";
        case SceneObjectType::CloudVolume:     return "cloud_volume";
        default:                               return "unknown";
        }
    }

    inline bool tryParseSceneObjectType(std::string_view s, SceneObjectType& out)
    {
        if (s == "cube")
        {
            out = SceneObjectType::Cube;
            return true;
        }
        if (s == "sphere")
        {
            out = SceneObjectType::Sphere;
            return true;
        }
        if (s == "particle_emitter" || s == "emitter" || s == "particle")
        {
            out = SceneObjectType::ParticleEmitter;
            return true;
        }
        if (s == "platform")
        {
            out = SceneObjectType::Platform;
            return true;
        }
        if (s == "coin")
        {
            out = SceneObjectType::Coin;
            return true;
        }
        if (s == "spawn" || s == "player_spawn")
        {
            out = SceneObjectType::Spawn;
            return true;
        }
        if (s == "point_light")
        {
            out = SceneObjectType::PointLight;
            return true;
        }
        if (s == "spot_light")
        {
            out = SceneObjectType::SpotLight;
            return true;
        }
        if (s == "ambient_light" || s == "ambient")
        {
            out = SceneObjectType::AmbientLight;
            return true;
        }
        if (s == "directional_light" || s == "directional" || s == "sun")
        {
            out = SceneObjectType::DirectionalLight;
            return true;
        }
        if (s == "player")
        {
            out = SceneObjectType::Player;
            return true;
        }
        if (s == "hunter" || s == "enemy")
        {
            out = SceneObjectType::Hunter;
            return true;
        }
        if (s == "wolf")
        {
            out = SceneObjectType::Wolf;
            return true;
        }
        if (s == "model" || s == "gltf")
        {
            out = SceneObjectType::Model;
            return true;
        }
        if (s == "water")
        {
            out = SceneObjectType::Water;
            return true;
        }
        if (s == "cloud_volume" || s == "cloud" || s == "clouds")
        {
            out = SceneObjectType::CloudVolume;
            return true;
        }
        return false;
    }

    // Live editor objects are ECS entities with EditorObjectComponent (Editor/EditorObject.h).
    // SceneObjectData is the JSON DTO only — not a parallel runtime world.

    // Plain data blob used for serialization (no live Entity).
    struct SceneObjectData
    {
        SceneObjectType  type = SceneObjectType::Cube;
        Math::Vector3f   position{ 0, 0, 0 };
        Math::Quaternion rotation{ 1, 0, 0, 0 };
        Math::Vector3f   scale{ 1, 1, 1 };
        float            color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
        std::string      modelPath;

        // Optional particle payload (only when type == ParticleEmitter).
        bool        hasParticle = false;
        std::string particleName = "Emitter";
        uint32_t    maxParticles = 512;
        float       emissionRate = 40.0f;
        float       duration     = 0.0f;
        bool        looping      = true;
        float       lifetimeMin  = 0.8f;
        float       lifetimeMax  = 1.6f;
        float       startSpeedMin = 1.0f;
        float       startSpeedMax = 3.0f;
        float       startSizeMin  = 0.15f;
        float       startSizeMax  = 0.35f;
        float       endSizeMin    = 0.02f;
        float       endSizeMax    = 0.10f;
        float       startColor[4]{ 1.0f, 0.7f, 0.25f, 1.0f };
        float       endColor[4]{ 1.0f, 0.15f, 0.05f, 0.0f };
        Math::Vector3f gravity{ 0.0f, -2.0f, 0.0f };
        Math::Vector3f direction{ 0.0f, 1.0f, 0.0f };
        float       spreadDegrees = 25.0f;
        int         shape         = 0; // ParticleEmitterDesc::Shape
        Math::Vector3f shapeSize{ 0.25f, 0.0f, 0.25f };
        bool        additiveBlend = true;
        float       simulationSpeed = 1.0f;
        int         renderMode    = 0; // ParticleEmitterDesc::RenderMode
        uint32_t    ribbonCount   = 1;
        float       ribbonUvScale = 1.0f;

        // Optional local-light payload (PointLight / SpotLight).
        bool  hasLight           = false;
        float lightIntensity     = 1885.0f; // point default candela (600*π); spot overwritten by applyTypeLightDefaults
        float lightRange         = 8.0f;
        float lightInnerDeg      = 12.0f;
        float lightOuterDeg      = 25.0f;
        float lightSourceRadius  = 0.05f;
        bool  lightEnabled       = true;
        float emissive           = 0.0f;
        int   emissiveMeshIndex  = -1; // objects[] index of glow-prop mesh; -1 = none

        // Optional cloud volume payload (type == CloudVolume).
        bool  hasCloud              = false;
        int   cloudShape            = 1; // CloudShape::Ellipsoid
        float cloudDensity          = 0.90f;
        float cloudCoverage         = 0.58f;
        float cloudSoftness         = 0.42f;
        float cloudAbsorption       = 1.15f;
        float cloudScattering       = 1.00f;
        float cloudAnisotropy       = 0.45f;
        float cloudNoiseScale       = 0.055f;
        float cloudDetailScale      = 3.40f;
        float cloudDetailStrength   = 0.38f;
        float cloudHeightFalloff    = 0.55f;
        float cloudSilverLining     = 0.75f;
        float cloudWindSpeed        = 1.20f;
        float cloudAlbedo[3]{ 0.90f, 0.93f, 1.00f };
        float cloudWindDir[3]{ 1.00f, 0.02f, 0.25f };
        bool  cloudEnabled          = true;
    };

    // World-level terrain JSON DTO. Not a SceneObjectType. Paths are virtual or sidecar filenames.
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
        bool        stale              = false;
        std::string treeModel;
        std::string flowerModel;
        std::string rockModel;
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
    };

    // Celestial clock and weather. Absent from a file means the Environment defaults.
    struct SkySceneDesc
    {
        bool        present       = false;
        float       timeOfDay     = 16.2f; // hours, [0, 24)
        float       dayOfYear     = 172.0f;
        float       latitude      = 47.6f; // degrees
        float       timeScale     = 0.0f;  // hours advanced per real second; 0 holds the clock
        std::string weather       = "partly"; // clear, partly, overcast, storm, or custom
        float       cloudCoverage = 0.35f;
        float       turbidity     = 2.4f;
        float       windSpeed     = 0.04f;
        float       windDir[2]{ 1.0f, 0.2f };
        float       rain          = 0.0f;
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
        std::vector<SceneObjectData> objects;
    };

} // namespace Dark
