#pragma once

#include "Math/Vector2f.h"
#include "third_party/nlohmann/json.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Dark::Terrain
{
    class FoliageCover;

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

} // namespace Dark::Terrain
