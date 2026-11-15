#pragma once

#include "Math/Vector2f.h"
#include "third_party/nlohmann/json.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Dark::Terrain
{
    class FoliageCover;
    class HeightMap;
    class SplatMap;

    // Still water. entireMap is the level lake (every XZ). Otherwise the rectangle is one placed body.
    // Trees, grass, and flowers whose bed is below surfaceY are not planted. Rocks are.
    struct FoliageWaterVolume
    {
        float minX      = 0.0f;
        float maxX      = 0.0f;
        float minZ      = 0.0f;
        float maxZ      = 0.0f;
        float surfaceY  = 0.0f;
        bool  entireMap = false;
    };

    constexpr float kTrailHumanWidth       = 1.6f;
    constexpr float kTrailAnimalWidth      = 0.8f;
    constexpr float kTrailHumanShoulderM   = 1.0f;
    constexpr float kTrailAnimalShoulderM  = 0.6f;
    constexpr float kTrailHumanEdgeNoiseM  = 0.25f;
    constexpr float kTrailAnimalEdgeNoiseM = 0.35f;
    constexpr float kTrailStepM            = 0.75f;
    constexpr float kTrailWidthMax         = 8.0f;
    constexpr int   kTrailMaxAutoPathsDefault = 12;
    constexpr int   kTrailMaxAutoPathsLimit   = 64;
    constexpr uint32_t kSaltTrailEdge      = 0x9100u;

    enum class TrailKind : uint8_t
    {
        Human = 0,
        Animal
    };

    struct TrailPoint
    {
        float x = 0.0f;
        float z = 0.0f;
    };

    struct TrailPath
    {
        TrailKind               kind     = TrailKind::Human;
        float                   width    = kTrailHumanWidth;
        bool                    authored = true;
        bool                    closed   = false;
        std::vector<TrailPoint> points;
    };

    struct TrailSet
    {
        bool                    autoGenerate = false;
        int                     maxAutoPaths = kTrailMaxAutoPathsDefault;
        float                   humanWidth   = kTrailHumanWidth;
        float                   animalWidth  = kTrailAnimalWidth;
        std::vector<TrailPath>  paths;
    };

    float clampTrailWidth(float width, float fallback);
    int   clampMaxAutoPaths(int count);
    void  clampTrailSet(TrailSet& set);

    // Vector2f::y is world z. Steps land in [0.5, 1] m when the span is long enough.
    void sampleTrailPolyline(const std::vector<Math::Vector2f>& points, bool closed, std::vector<Math::Vector2f>& out);

    void rasterTrail(FoliageCover& cover, const TrailPath& path, uint32_t seed, uint32_t salt);

    nlohmann::json writeTrailSet(const TrailSet& set);
    bool readTrailSet(const nlohmann::json& object, TrailSet& out, std::string* error);

    struct TrailPoiCell
    {
        int cx = 0;
        int cz = 0;
    };

    struct TrailGenerateIn
    {
        const HeightMap*                         height       = nullptr;
        const SplatMap*                          splat        = nullptr;
        const std::vector<FoliageWaterVolume>*   water        = nullptr;
        float                                    seaLevel     = 0.0f;
        uint32_t                                 seed         = 0;
        int                                      maxAutoPaths = kTrailMaxAutoPathsDefault;
        float                                    humanWidth   = kTrailHumanWidth;
        float                                    animalWidth  = kTrailAnimalWidth;
        bool                                   (*onProgress)(float t, const char* phase, void* user) = nullptr;
        void*                                    user         = nullptr;
    };

    struct TrailGenerateOut
    {
        std::vector<TrailPoiCell> water;
        std::vector<TrailPoiCell> clearings;
        std::vector<TrailPoiCell> forest;
        std::vector<TrailPath>    generated;
    };

    struct TrailRouteIn
    {
        const HeightMap*                       height   = nullptr;
        const SplatMap*                        splat    = nullptr;
        const std::vector<FoliageWaterVolume>* water    = nullptr;
        float                                  seaLevel = 0.0f;
        TrailKind                              kind     = TrailKind::Human;
        float                                  startX   = 0.0f;
        float                                  startZ   = 0.0f;
        float                                  endX     = 0.0f;
        float                                  endZ     = 0.0f;
        uint32_t                               seed     = 0;
        bool                                 (*onProgress)(float t, const char* phase, void* user) = nullptr;
        void*                                  user     = nullptr;
    };

    // Rebuilds authored == false. Cancel or a missing map leaves the set unchanged.
    bool generateGameTrails(const TrailGenerateIn& in, TrailSet& set, TrailGenerateOut* out);

    // One smoothed route. No route leaves points empty and returns true. Cancel returns false.
    bool findTrailRoute(const TrailRouteIn& in, std::vector<TrailPoint>& points);

    // Dirt disks, layer 0, amount 1. Cancel returns false; the caller discards a partial paint.
    bool stampTrailDirt(const HeightMap& height, SplatMap& splat, const std::vector<TrailPath>& paths, bool (*onProgress)(float t, const char* phase, void* user) = nullptr, void* user = nullptr);

} // namespace Dark::Terrain
