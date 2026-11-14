#include "Terrain/TrailPath.h"

#include "Core/Json.h"
#include "Core/Log.h"
#include "Math/Curves.h"
#include "Math/MathDefines.h"
#include "Terrain/FoliageCover.h"
#include "Terrain/HeightMap.h"
#include "Terrain/SplatMap.h"
#include "Terrain/TerrainGen.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <queue>
#include <utility>

namespace Dark::Terrain
{
    namespace
    {
        bool jsonNumber(const nlohmann::json& value, float& out)
        {
            if (const auto* number = value.get_ptr<const nlohmann::json::number_float_t*>())
            {
                if (!std::isfinite(static_cast<double>(*number)))
                    return false;
                out = static_cast<float>(*number);
                return true;
            }
            if (const auto* number = value.get_ptr<const nlohmann::json::number_integer_t*>())
            {
                out = static_cast<float>(*number);
                return true;
            }
            if (const auto* number = value.get_ptr<const nlohmann::json::number_unsigned_t*>())
            {
                out = static_cast<float>(*number);
                return true;
            }
            return false;
        }

        int stepCount(float length)
        {
            if (!(length > 0.0f))
                return 1;
            int steps = static_cast<int>(std::floor(length / kTrailStepM));
            if (steps < 1)
                steps = 1;
            if (length / static_cast<float>(steps) > 1.0f)
                ++steps;
            return steps;
        }
    } // namespace

    float clampTrailWidth(float width, float fallback)
    {
        if (!std::isfinite(width) || !(width > 0.0f))
            width = fallback;
        if (!std::isfinite(width) || !(width > 0.0f))
            width = kTrailHumanWidth;
        if (width > kTrailWidthMax)
            width = kTrailWidthMax;
        return width;
    }

    int clampMaxAutoPaths(int count)
    {
        if (count < 0)
            return 0;
        if (count > kTrailMaxAutoPathsLimit)
            return kTrailMaxAutoPathsLimit;
        return count;
    }

    void clampTrailSet(TrailSet& set)
    {
        set.maxAutoPaths = clampMaxAutoPaths(set.maxAutoPaths);
        set.humanWidth   = clampTrailWidth(set.humanWidth, kTrailHumanWidth);
        set.animalWidth  = clampTrailWidth(set.animalWidth, kTrailAnimalWidth);
        for (TrailPath& path : set.paths)
        {
            const float fallback = path.kind == TrailKind::Animal ? set.animalWidth : set.humanWidth;
            const float def      = path.kind == TrailKind::Animal ? kTrailAnimalWidth : kTrailHumanWidth;
            path.width = clampTrailWidth(path.width, clampTrailWidth(fallback, def));
        }
    }

    void sampleTrailPolyline(const std::vector<Math::Vector2f>& points, bool closed, std::vector<Math::Vector2f>& out)
    {
        out.clear();
        const int n = static_cast<int>(points.size());
        if (n < 2)
            return;

        const bool loop = closed && n >= 3;
        auto at = [&](int index) -> Math::Vector2f {
            if (loop)
            {
                int wrapped = index % n;
                if (wrapped < 0)
                    wrapped += n;
                return points[static_cast<size_t>(wrapped)];
            }
            if (index < 0)
                return points[0];
            if (index >= n)
                return points[static_cast<size_t>(n - 1)];
            return points[static_cast<size_t>(index)];
        };

        auto push = [&](const Math::Vector2f& p) {
            if (!out.empty())
            {
                const Math::Vector2f delta = p - out.back();
                if (delta.MagnitudeSqrd() <= 1.0e-8f)
                    return;
            }
            out.push_back(p);
        };

        const int segments = loop ? n : n - 1;
        const bool straight = n == 2;
        for (int segment = 0; segment < segments; ++segment)
        {
            const Math::Vector2f p0 = at(segment - 1);
            const Math::Vector2f p1 = at(segment);
            const Math::Vector2f p2 = at(segment + 1);
            const Math::Vector2f p3 = at(segment + 2);
            const int steps = stepCount((p2 - p1).Magnitude());
            for (int i = 0; i < steps; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(steps);
                if (straight)
                    push(p1 + (p2 - p1) * t);
                else
                    push(Math::CatmullRom(p0, p1, p2, p3, t));
            }
        }
        push(loop ? points[0] : points[static_cast<size_t>(n - 1)]);
    }

    void rasterTrail(FoliageCover& cover, const TrailPath& path, uint32_t seed, uint32_t salt)
    {
        if (!cover.valid() || path.points.size() < 2)
            return;
        for (const TrailPoint& point : path.points)
        {
            if (!std::isfinite(point.x) || !std::isfinite(point.z))
                return;
        }

        const float kindDefault = path.kind == TrailKind::Animal ? kTrailAnimalWidth : kTrailHumanWidth;
        const float width = clampTrailWidth(path.width, kindDefault);
        if (!(width > 0.0f))
            return;

        const float shoulder = path.kind == TrailKind::Animal ? kTrailAnimalShoulderM : kTrailHumanShoulderM;
        const float noise    = path.kind == TrailKind::Animal ? kTrailAnimalEdgeNoiseM : kTrailHumanEdgeNoiseM;

        std::vector<Math::Vector2f> controls;
        controls.reserve(path.points.size());
        for (const TrailPoint& point : path.points)
            controls.emplace_back(point.x, point.z);

        std::vector<Math::Vector2f> samples;
        sampleTrailPolyline(controls, path.closed, samples);
        if (samples.size() < 2)
            return;

        cover.stampTrail(samples, width * 0.5f, shoulder, noise, seed ^ salt ^ kSaltTrailEdge, true);
    }

    nlohmann::json writeTrailSet(const TrailSet& set)
    {
        TrailSet copy = set;
        clampTrailSet(copy);

        nlohmann::json object;
        object["autoGenerate"] = copy.autoGenerate;
        object["maxAutoPaths"] = copy.maxAutoPaths;
        object["humanWidth"]   = copy.humanWidth;
        object["animalWidth"]  = copy.animalWidth;
        nlohmann::json paths = nlohmann::json::array();
        for (const TrailPath& path : copy.paths)
        {
            nlohmann::json entry;
            entry["kind"]     = path.kind == TrailKind::Animal ? "animal" : "human";
            entry["width"]    = path.width;
            entry["authored"] = path.authored;
            entry["closed"]   = path.closed;
            nlohmann::json points = nlohmann::json::array();
            for (const TrailPoint& point : path.points)
                points.push_back(nlohmann::json::array({ point.x, point.z }));
            entry["points"] = std::move(points);
            paths.push_back(std::move(entry));
        }
        object["paths"] = std::move(paths);
        return object;
    }

    bool readTrailSet(const nlohmann::json& object, TrailSet& out, std::string* error)
    {
        out = TrailSet{};
        if (!object.is_object())
            return false;

        bool ok = true;
        if (!jsonBoolOr(object, "autoGenerate", out.autoGenerate, false, "TrailPath", error))
            ok = false;

        int maxAuto = kTrailMaxAutoPathsDefault;
        if (!jsonIntOr(object, "maxAutoPaths", maxAuto, kTrailMaxAutoPathsDefault, "TrailPath", error))
            ok = false;
        out.maxAutoPaths = clampMaxAutoPaths(maxAuto);

        float human = kTrailHumanWidth;
        float animal = kTrailAnimalWidth;
        if (!jsonFloatOr(object, "humanWidth", human, kTrailHumanWidth, "TrailPath", error))
            ok = false;
        if (!jsonFloatOr(object, "animalWidth", animal, kTrailAnimalWidth, "TrailPath", error))
            ok = false;
        out.humanWidth  = clampTrailWidth(human, kTrailHumanWidth);
        out.animalWidth = clampTrailWidth(animal, kTrailAnimalWidth);

        if (!object.contains("paths") || !object["paths"].is_array())
            return ok;

        for (const nlohmann::json& entry : object["paths"])
        {
            if (!entry.is_object())
                continue;

            std::string kindName;
            if (!jsonStringOr(entry, "kind", kindName, std::string("human"), "TrailPath", error))
                ok = false;

            TrailKind kind = TrailKind::Human;
            float fallback = kTrailHumanWidth;
            if (kindName == "animal")
            {
                kind = TrailKind::Animal;
                fallback = kTrailAnimalWidth;
            }
            else if (kindName != "human")
            {
                DE_LOG_WARN(LogCategory::Render, "TrailPath: unknown kind '{}' — skipped", kindName);
                continue;
            }

            TrailPath path;
            path.kind     = kind;
            path.authored = true;
            if (!jsonBoolOr(entry, "authored", path.authored, true, "TrailPath", error))
                ok = false;
            if (!jsonBoolOr(entry, "closed", path.closed, false, "TrailPath", error))
                ok = false;

            float width = fallback;
            if (!jsonFloatOr(entry, "width", width, fallback, "TrailPath", error))
                ok = false;
            path.width = clampTrailWidth(width, fallback);

            bool pointsOk = true;
            if (entry.contains("points"))
            {
                if (!entry["points"].is_array())
                    pointsOk = false;
                else
                {
                    for (const nlohmann::json& pt : entry["points"])
                    {
                        float x = 0.0f;
                        float z = 0.0f;
                        if (!pt.is_array() || pt.size() < 2 || !jsonNumber(pt[0], x) || !jsonNumber(pt[1], z))
                        {
                            pointsOk = false;
                            break;
                        }
                        path.points.push_back(TrailPoint{ x, z });
                    }
                }
            }
            if (!pointsOk)
                continue;
            for (const TrailPoint& point : path.points)
            {
                if (!std::isfinite(point.x) || !std::isfinite(point.z))
                {
                    pointsOk = false;
                    break;
                }
            }
            if (!pointsOk)
                continue;
            out.paths.push_back(std::move(path));
        }
        return ok;
    }

    namespace
    {
        constexpr int   kTrailGridMax      = 1024;
        constexpr int   kTrailProgressStep = 4096;
        constexpr float kTrailCellStartM   = 4.0f;
        constexpr float kSqrt2             = 1.41421356f;
        constexpr float kMinStepCost       = 0.6f;
        constexpr float kDirtCost          = 0.6f;
        constexpr float kRockCost          = 2.5f;
        constexpr float kAnimalLateralM    = 0.4f;
        constexpr uint32_t kSaltTrailAnimal = 0x9200u;

        enum class BuildStatus
        {
            Ok,
            Bad,
            Cancelled
        };

        enum class RouteStatus
        {
            Routed,
            Empty,
            Cancelled
        };

        struct OpenItem
        {
            float f = 0.0f;
            float g = 0.0f;
            int   i = 0;

            bool operator<(const OpenItem& other) const
            {
                if (f != other.f)
                    return f > other.f;
                return i > other.i;
            }
        };

        struct TrailGrid
        {
            int   cellsX    = 0;
            int   cellsZ    = 0;
            int   cellCount = 0;
            float cell      = kTrailCellStartM;
            float originX   = 0.0f;
            float originZ   = 0.0f;
            std::vector<uint8_t> blockedHuman;
            std::vector<uint8_t> blockedAnimal;
            std::vector<float>   costHuman;
            std::vector<float>   costAnimal;
            std::vector<float>   grass;
            std::vector<uint8_t> wet;
            std::vector<float>   surfaceY;
            std::vector<float>   bedY;
        };

        struct ProgressSink
        {
            bool (*fn)(float, const char*, void*) = nullptr;
            void* user = nullptr;
            int   span = 1;

            bool report(int count, const char* phase) const
            {
                if (fn == nullptr)
                    return true;
                if (count % kTrailProgressStep != 0)
                    return true;
                float t = static_cast<float>(count) / static_cast<float>(span > 0 ? span : 1);
                if (t < 0.0f)
                    t = 0.0f;
                if (t > 1.0f)
                    t = 1.0f;
                return fn(t, phase, user);
            }
        };

        const std::vector<FoliageWaterVolume>& volumesOrEmpty(const std::vector<FoliageWaterVolume>* water, std::vector<FoliageWaterVolume>& scratch)
        {
            if (water != nullptr)
                return *water;
            scratch.clear();
            return scratch;
        }

        bool wetAt(const std::vector<FoliageWaterVolume>& water, float seaLevel, float x, float z, float bedY, float& surfaceY)
        {
            bool wet = false;
            surfaceY = seaLevel;
            if (bedY < seaLevel)
                wet = true;
            for (const FoliageWaterVolume& volume : water)
            {
                if (!(bedY < volume.surfaceY))
                    continue;
                if (volume.entireMap || (x >= volume.minX && x <= volume.maxX && z >= volume.minZ && z <= volume.maxZ))
                {
                    wet = true;
                    surfaceY = volume.surfaceY;
                }
            }
            return wet;
        }

        int cellIndex(const TrailGrid& grid, int x, int z)
        {
            return z * grid.cellsX + x;
        }

        bool insideGrid(const TrailGrid& grid, int x, int z)
        {
            return x >= 0 && z >= 0 && x < grid.cellsX && z < grid.cellsZ;
        }

        int cellOf(float world, float origin, float cell, int count)
        {
            const double q = (static_cast<double>(world) - static_cast<double>(origin)) / static_cast<double>(cell);
            int c = static_cast<int>(std::floor(q));
            if (c < 0)
                c = 0;
            if (c >= count)
                c = count - 1;
            return c;
        }

        float cellCenter(float origin, int index, float cell)
        {
            return origin + (static_cast<float>(index) + 0.5f) * cell;
        }

        void logGrid(const TrailGrid& grid, std::chrono::steady_clock::time_point started)
        {
            const auto elapsed = std::chrono::steady_clock::now() - started;
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            DE_LOG_INFO(LogCategory::Render, "TrailPath: grid {} m, {}x{} cells, {} ms", static_cast<double>(grid.cell), grid.cellsX, grid.cellsZ, static_cast<long long>(ms));
        }

        BuildStatus buildTrailGrid(const HeightMap& height, const SplatMap& splat, const std::vector<FoliageWaterVolume>& water, float seaLevel, const ProgressSink& progress, TrailGrid& grid)
        {
            if (!height.valid() || !splat.valid() || !(height.cellSize() > 0.0f))
                return BuildStatus::Bad;
            const float worldW = static_cast<float>(height.width() - 1u) * height.cellSize();
            const float worldD = static_cast<float>(height.height() - 1u) * height.cellSize();
            if (!(worldW > 0.0f) || !(worldD > 0.0f))
                return BuildStatus::Bad;

            float cell = kTrailCellStartM;
            const float longSide = worldW > worldD ? worldW : worldD;
            if (longSide / cell > static_cast<float>(kTrailGridMax))
                cell = longSide / static_cast<float>(kTrailGridMax);

            int cellsX = static_cast<int>(std::ceil(static_cast<double>(worldW) / static_cast<double>(cell)));
            int cellsZ = static_cast<int>(std::ceil(static_cast<double>(worldD) / static_cast<double>(cell)));
            if (cellsX < 1)
                cellsX = 1;
            if (cellsZ < 1)
                cellsZ = 1;
            if (cellsX > kTrailGridMax || cellsZ > kTrailGridMax)
            {
                const float over = static_cast<float>(cellsX > cellsZ ? cellsX : cellsZ) / static_cast<float>(kTrailGridMax);
                if (over > 1.0f)
                    cell *= over;
                cellsX = static_cast<int>(std::ceil(static_cast<double>(worldW) / static_cast<double>(cell)));
                cellsZ = static_cast<int>(std::ceil(static_cast<double>(worldD) / static_cast<double>(cell)));
                if (cellsX > kTrailGridMax)
                    cellsX = kTrailGridMax;
                if (cellsZ > kTrailGridMax)
                    cellsZ = kTrailGridMax;
                if (cellsX < 1)
                    cellsX = 1;
                if (cellsZ < 1)
                    cellsZ = 1;
            }

            const int count = cellsX * cellsZ;
            if (count <= 0 || count > kTrailGridMax * kTrailGridMax)
                return BuildStatus::Bad;

            grid.cellsX    = cellsX;
            grid.cellsZ    = cellsZ;
            grid.cellCount = count;
            grid.cell      = cell;
            grid.originX   = height.origin().x;
            grid.originZ   = height.origin().z;
            grid.blockedHuman.assign(static_cast<size_t>(count), 1u);
            grid.blockedAnimal.assign(static_cast<size_t>(count), 1u);
            grid.costHuman.assign(static_cast<size_t>(count), 0.0f);
            grid.costAnimal.assign(static_cast<size_t>(count), 0.0f);
            grid.grass.assign(static_cast<size_t>(count), 0.0f);
            grid.wet.assign(static_cast<size_t>(count), 0u);
            grid.surfaceY.assign(static_cast<size_t>(count), seaLevel);
            grid.bedY.assign(static_cast<size_t>(count), 0.0f);

            const float humanNy = std::cos(28.0f * Math::DegToRad);
            const float animalNy = std::cos(36.0f * Math::DegToRad);
            if (!progress.report(0, "trails"))
                return BuildStatus::Cancelled;

            int seen = 0;
            for (int z = 0; z < cellsZ; ++z)
            {
                const float wz = cellCenter(grid.originZ, z, cell);
                for (int x = 0; x < cellsX; ++x)
                {
                    const float wx = cellCenter(grid.originX, x, cell);
                    const int index = cellIndex(grid, x, z);
                    const size_t slot = static_cast<size_t>(index);
                    ++seen;
                    if (!progress.report(seen, "trails"))
                        return BuildStatus::Cancelled;

                    const float bed = height.heightAtWorld(wx, wz);
                    grid.bedY[slot] = bed;
                    float surface = seaLevel;
                    const bool wet = !height.containsXZ(wx, wz) || wetAt(water, seaLevel, wx, wz, bed, surface);
                    grid.wet[slot] = wet ? 1u : 0u;
                    grid.surfaceY[slot] = surface;

                    const Math::Vector3f normal = height.normalAtWorld(wx, wz);
                    float ny = normal.y;
                    if (ny < -1.0f)
                        ny = -1.0f;
                    if (ny > 1.0f)
                        ny = 1.0f;

                    float fx = 0.0f;
                    float fz = 0.0f;
                    height.worldToSample(wx, wz, fx, fz);
                    float w[kMaxTerrainLayers] = {};
                    splat.sampleWeights(fx, fz, w);
                    grid.grass[slot] = w[1];

                    float scale = kDirtCost * w[0] + w[1] + kRockCost * w[2] + w[3];
                    if (!(scale > 0.05f))
                        scale = 1.0f;
                    const float slope = 1.0f + 4.0f * (1.0f - ny) * (1.0f - ny);
                    const float cost = slope * scale;
                    const bool humanBlocked = wet || !(ny >= humanNy);
                    const bool animalBlocked = wet || !(ny >= animalNy);
                    grid.blockedHuman[slot] = humanBlocked ? 1u : 0u;
                    grid.blockedAnimal[slot] = animalBlocked ? 1u : 0u;
                    grid.costHuman[slot] = humanBlocked ? 0.0f : cost;
                    grid.costAnimal[slot] = animalBlocked ? 0.0f : cost;
                }
            }
            return BuildStatus::Ok;
        }

        const uint8_t* blockedFor(const TrailGrid& grid, TrailKind kind)
        {
            return kind == TrailKind::Animal ? grid.blockedAnimal.data() : grid.blockedHuman.data();
        }

        const float* costFor(const TrailGrid& grid, TrailKind kind)
        {
            return kind == TrailKind::Animal ? grid.costAnimal.data() : grid.costHuman.data();
        }

        bool blockedCell(const TrailGrid& grid, const uint8_t* blocked, int x, int z)
        {
            if (!insideGrid(grid, x, z))
                return true;
            return blocked[cellIndex(grid, x, z)] != 0;
        }

        bool lineClear(const TrailGrid& grid, const uint8_t* blocked, int x0, int z0, int x1, int z1)
        {
            if (blockedCell(grid, blocked, x0, z0) || blockedCell(grid, blocked, x1, z1))
                return false;
            if (x0 == x1 && z0 == z1)
                return true;

            const float dx = static_cast<float>(x1 - x0);
            const float dz = static_cast<float>(z1 - z0);
            const int stepX = dx > 0.0f ? 1 : (dx < 0.0f ? -1 : 0);
            const int stepZ = dz > 0.0f ? 1 : (dz < 0.0f ? -1 : 0);
            const float tDeltaX = stepX != 0 ? std::fabs(1.0f / dx) : 1.0e30f;
            const float tDeltaZ = stepZ != 0 ? std::fabs(1.0f / dz) : 1.0e30f;
            float tMaxX = stepX != 0 ? 0.5f * tDeltaX : 1.0e30f;
            float tMaxZ = stepZ != 0 ? 0.5f * tDeltaZ : 1.0e30f;
            int x = x0;
            int z = z0;
            const int guard = std::abs(x1 - x0) + std::abs(z1 - z0) + 4;
            for (int n = 0; n < guard; ++n)
            {
                if (x == x1 && z == z1)
                    return true;
                if (tMaxX < tMaxZ)
                {
                    if (tMaxX > 1.0f)
                        return false;
                    x += stepX;
                    tMaxX += tDeltaX;
                }
                else if (tMaxZ < tMaxX)
                {
                    if (tMaxZ > 1.0f)
                        return false;
                    z += stepZ;
                    tMaxZ += tDeltaZ;
                }
                else
                {
                    if (tMaxX > 1.0f)
                        return false;
                    if (blockedCell(grid, blocked, x + stepX, z) || blockedCell(grid, blocked, x, z + stepZ))
                        return false;
                    x += stepX;
                    z += stepZ;
                    tMaxX += tDeltaX;
                    tMaxZ += tDeltaZ;
                }
                if (blockedCell(grid, blocked, x, z))
                    return false;
            }
            return x == x1 && z == z1;
        }

        // A corner cut can leave the cells the search cleared.
        bool polylineStaysClear(const TrailGrid& grid, const uint8_t* blocked, const std::vector<TrailPoint>& points)
        {
            if (points.size() < 2)
                return false;
            for (size_t i = 1; i < points.size(); ++i)
            {
                const TrailPoint& a = points[i - 1u];
                const TrailPoint& b = points[i];
                const int x0 = cellOf(a.x, grid.originX, grid.cell, grid.cellsX);
                const int z0 = cellOf(a.z, grid.originZ, grid.cell, grid.cellsZ);
                const int x1 = cellOf(b.x, grid.originX, grid.cell, grid.cellsX);
                const int z1 = cellOf(b.z, grid.originZ, grid.cell, grid.cellsZ);
                if (!lineClear(grid, blocked, x0, z0, x1, z1))
                    return false;
            }
            return true;
        }

        float heuristic(int x, int z, int goalX, int goalZ)
        {
            const int adx = std::abs(goalX - x);
            const int adz = std::abs(goalZ - z);
            const int diag = adx < adz ? adx : adz;
            const int ortho = (adx > adz ? adx : adz) - diag;
            return (static_cast<float>(diag) * kSqrt2 + static_cast<float>(ortho)) * kMinStepCost;
        }

        void chaikin(std::vector<TrailPoint>& points)
        {
            if (points.size() < 2)
                return;
            std::vector<TrailPoint> next;
            next.reserve(points.size() * 2u);
            next.push_back(points.front());
            for (size_t i = 0; i + 1u < points.size(); ++i)
            {
                const TrailPoint& a = points[i];
                const TrailPoint& b = points[i + 1u];
                TrailPoint q;
                q.x = a.x * 0.75f + b.x * 0.25f;
                q.z = a.z * 0.75f + b.z * 0.25f;
                TrailPoint r;
                r.x = a.x * 0.25f + b.x * 0.75f;
                r.z = a.z * 0.25f + b.z * 0.75f;
                next.push_back(q);
                next.push_back(r);
            }
            next.push_back(points.back());
            points.swap(next);
        }

        void offsetAnimal(std::vector<TrailPoint>& points, uint32_t seed)
        {
            const int count = static_cast<int>(points.size());
            if (count < 2)
                return;
            for (int i = 0; i < count; ++i)
            {
                const int prev = i > 0 ? i - 1 : i;
                const int next = i + 1 < count ? i + 1 : i;
                float dx = points[static_cast<size_t>(next)].x - points[static_cast<size_t>(prev)].x;
                float dz = points[static_cast<size_t>(next)].z - points[static_cast<size_t>(prev)].z;
                const float mag = std::sqrt(dx * dx + dz * dz);
                if (!(mag > 1.0e-4f))
                    continue;
                dx /= mag;
                dz /= mag;
                const float side = hash21(i, static_cast<int>(seed & 0x7fffffffu), seed ^ kSaltTrailAnimal) * kAnimalLateralM;
                points[static_cast<size_t>(i)].x += -dz * side;
                points[static_cast<size_t>(i)].z += dx * side;
            }
        }

        RouteStatus routeOnGrid(const TrailGrid& grid, TrailKind kind, int startX, int startZ, int goalX, int goalZ, uint32_t seed, const ProgressSink& progress, std::vector<TrailPoint>& points)
        {
            points.clear();
            if (!insideGrid(grid, startX, startZ) || !insideGrid(grid, goalX, goalZ))
                return RouteStatus::Empty;
            const uint8_t* blocked = blockedFor(grid, kind);
            const float* cost = costFor(grid, kind);
            if (blockedCell(grid, blocked, startX, startZ) || blockedCell(grid, blocked, goalX, goalZ))
                return RouteStatus::Empty;
            if (startX == goalX && startZ == goalZ)
                return RouteStatus::Empty;
            if (!progress.report(0, "trails"))
                return RouteStatus::Cancelled;

            const int start = cellIndex(grid, startX, startZ);
            const int goal = cellIndex(grid, goalX, goalZ);
            const int cap = grid.cellCount;
            std::vector<float> g(static_cast<size_t>(cap), std::numeric_limits<float>::infinity());
            std::vector<int> parent(static_cast<size_t>(cap), -1);
            std::vector<uint8_t> closed(static_cast<size_t>(cap), 0u);
            g[static_cast<size_t>(start)] = 0.0f;

            std::priority_queue<OpenItem> open;
            open.push(OpenItem{ heuristic(startX, startZ, goalX, goalZ), 0.0f, start });

            constexpr int kDx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
            constexpr int kDz[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

            int expanded = 0;
            bool reached = false;
            while (!open.empty())
            {
                const OpenItem cur = open.top();
                open.pop();
                if (closed[static_cast<size_t>(cur.i)] != 0)
                    continue;
                if (cur.g > g[static_cast<size_t>(cur.i)])
                    continue;
                closed[static_cast<size_t>(cur.i)] = 1u;
                ++expanded;
                if (expanded > cap)
                    return RouteStatus::Empty;
                if (!progress.report(expanded, "trails"))
                    return RouteStatus::Cancelled;
                if (cur.i == goal)
                {
                    reached = true;
                    break;
                }

                const int x = cur.i % grid.cellsX;
                const int z = cur.i / grid.cellsX;
                for (int d = 0; d < 8; ++d)
                {
                    const int nx = x + kDx[d];
                    const int nz = z + kDz[d];
                    if (!insideGrid(grid, nx, nz))
                        continue;
                    if (blockedCell(grid, blocked, nx, nz))
                        continue;
                    const bool diagonal = d >= 4;
                    if (diagonal && (blockedCell(grid, blocked, x + kDx[d], z) || blockedCell(grid, blocked, x, z + kDz[d])))
                        continue;
                    const int ni = cellIndex(grid, nx, nz);
                    if (closed[static_cast<size_t>(ni)] != 0)
                        continue;
                    const float step = cost[ni] * (diagonal ? kSqrt2 : 1.0f);
                    const float ng = g[static_cast<size_t>(cur.i)] + step;
                    if (!(ng < g[static_cast<size_t>(ni)]))
                        continue;
                    g[static_cast<size_t>(ni)] = ng;
                    parent[static_cast<size_t>(ni)] = cur.i;
                    open.push(OpenItem{ ng + heuristic(nx, nz, goalX, goalZ), ng, ni });
                }
            }

            if (!reached)
                return RouteStatus::Empty;

            std::vector<int> cells;
            int at = goal;
            int guard = cap + 1;
            while (at >= 0 && guard-- > 0)
            {
                cells.push_back(at);
                if (at == start)
                    break;
                at = parent[static_cast<size_t>(at)];
            }
            if (cells.empty() || cells.back() != start)
                return RouteStatus::Empty;
            std::reverse(cells.begin(), cells.end());

            std::vector<int> keep;
            keep.push_back(cells.front());
            size_t anchor = 0;
            for (size_t i = 1; i < cells.size(); ++i)
            {
                const int ax = cells[anchor] % grid.cellsX;
                const int az = cells[anchor] / grid.cellsX;
                const int ix = cells[i] % grid.cellsX;
                const int iz = cells[i] / grid.cellsX;
                if (!lineClear(grid, blocked, ax, az, ix, iz))
                {
                    keep.push_back(cells[i - 1u]);
                    anchor = i - 1u;
                }
            }
            if (keep.back() != cells.back())
                keep.push_back(cells.back());

            points.reserve(keep.size());
            for (int cell : keep)
            {
                TrailPoint point;
                point.x = cellCenter(grid.originX, cell % grid.cellsX, grid.cell);
                point.z = cellCenter(grid.originZ, cell / grid.cellsX, grid.cell);
                if (!points.empty())
                {
                    const float dx = point.x - points.back().x;
                    const float dz = point.z - points.back().z;
                    if (dx * dx + dz * dz <= 1.0e-8f)
                        continue;
                }
                points.push_back(point);
            }
            if (points.size() < 2)
            {
                points.clear();
                return RouteStatus::Empty;
            }
            std::vector<TrailPoint> pulled = points;
            chaikin(points);
            if (!polylineStaysClear(grid, blocked, points))
                points.swap(pulled);
            if (kind == TrailKind::Animal)
                offsetAnimal(points, seed);
            return RouteStatus::Routed;
        }

        bool sameCell(const TrailPoiCell& a, const TrailPoiCell& b)
        {
            return a.cx == b.cx && a.cz == b.cz;
        }

        void sortPois(std::vector<TrailPoiCell>& cells)
        {
            std::sort(cells.begin(), cells.end(), [](const TrailPoiCell& a, const TrailPoiCell& b) {
                if (a.cz != b.cz)
                    return a.cz < b.cz;
                return a.cx < b.cx;
            });
        }

        void strideKeep(std::vector<TrailPoiCell>& cells)
        {
            sortPois(cells);
            std::vector<TrailPoiCell> kept;
            const int count = static_cast<int>(cells.size());
            for (int i = 0; i < 4; ++i)
            {
                if (count <= 0)
                    break;
                const TrailPoiCell& cell = cells[static_cast<size_t>(i * count / 4)];
                bool duplicate = false;
                for (const TrailPoiCell& have : kept)
                {
                    if (sameCell(have, cell))
                        duplicate = true;
                }
                if (!duplicate)
                    kept.push_back(cell);
            }
            cells.swap(kept);
        }

        void dropClosePois(std::vector<TrailPoiCell>& water, std::vector<TrailPoiCell>& clearings, std::vector<TrailPoiCell>& forest)
        {
            struct Tagged
            {
                TrailPoiCell cell;
                int          set = 0;
            };
            std::vector<Tagged> merged;
            merged.reserve(water.size() + clearings.size() + forest.size());
            auto append = [&](const std::vector<TrailPoiCell>& cells, int set) {
                for (const TrailPoiCell& cell : cells)
                {
                    bool close = false;
                    for (const Tagged& have : merged)
                    {
                        const int d = std::abs(have.cell.cx - cell.cx) + std::abs(have.cell.cz - cell.cz);
                        if (d <= 2)
                        {
                            close = true;
                            break;
                        }
                    }
                    if (!close)
                        merged.push_back(Tagged{ cell, set });
                }
            };
            append(water, 0);
            append(clearings, 1);
            append(forest, 2);
            water.clear();
            clearings.clear();
            forest.clear();
            for (const Tagged& item : merged)
            {
                if (item.set == 0)
                    water.push_back(item.cell);
                else if (item.set == 1)
                    clearings.push_back(item.cell);
                else
                    forest.push_back(item.cell);
            }
        }

        void collectPois(const TrailGrid& grid, std::vector<TrailPoiCell>& water, std::vector<TrailPoiCell>& clearings, std::vector<TrailPoiCell>& forest)
        {
            water.clear();
            clearings.clear();
            forest.clear();
            constexpr int kDx[4] = { 1, -1, 0, 0 };
            constexpr int kDz[4] = { 0, 0, 1, -1 };
            for (int z = 0; z < grid.cellsZ; ++z)
            {
                for (int x = 0; x < grid.cellsX; ++x)
                {
                    const int index = cellIndex(grid, x, z);
                    const size_t slot = static_cast<size_t>(index);
                    const bool humanWalk = grid.blockedHuman[slot] == 0;
                    const bool animalWalk = grid.blockedAnimal[slot] == 0;
                    if (humanWalk)
                    {
                        bool shore = false;
                        int grassyNeighbours = 0;
                        for (int d = 0; d < 4; ++d)
                        {
                            const int nx = x + kDx[d];
                            const int nz = z + kDz[d];
                            if (!insideGrid(grid, nx, nz))
                                continue;
                            const size_t nslot = static_cast<size_t>(cellIndex(grid, nx, nz));
                            if (grid.wet[nslot] != 0)
                            {
                                const float above = grid.bedY[slot] - grid.surfaceY[nslot];
                                if (above >= -1.0e-3f && above <= 1.0f)
                                    shore = true;
                            }
                            if (grid.blockedHuman[nslot] == 0 && grid.grass[nslot] >= 0.60f)
                                ++grassyNeighbours;
                        }
                        if (shore)
                            water.push_back(TrailPoiCell{ x, z });
                        if (grid.grass[slot] >= 0.60f && grassyNeighbours < 2)
                            clearings.push_back(TrailPoiCell{ x, z });
                    }
                    if (!animalWalk || !(grid.grass[slot] < 0.60f))
                        continue;
                    bool edge = false;
                    for (int d = 0; d < 4; ++d)
                    {
                        const int nx = x + kDx[d];
                        const int nz = z + kDz[d];
                        if (!insideGrid(grid, nx, nz))
                            continue;
                        const size_t nslot = static_cast<size_t>(cellIndex(grid, nx, nz));
                        if (grid.blockedAnimal[nslot] != 0)
                            continue;
                        if (grid.grass[nslot] <= grid.grass[slot] - 0.35f)
                            edge = true;
                    }
                    if (edge)
                        forest.push_back(TrailPoiCell{ x, z });
                }
            }
            strideKeep(water);
            strideKeep(clearings);
            strideKeep(forest);
            dropClosePois(water, clearings, forest);
        }

        bool nearestClearing(const std::vector<TrailPoiCell>& clearings, const TrailPoiCell& from, TrailPoiCell& out)
        {
            const TrailPoiCell* best = nullptr;
            int bestD = 0;
            for (const TrailPoiCell& cell : clearings)
            {
                const int dx = cell.cx - from.cx;
                const int dz = cell.cz - from.cz;
                const int d = dx * dx + dz * dz;
                const bool closer = best == nullptr || d < bestD;
                const bool tie = best != nullptr && d == bestD && (cell.cz < best->cz || (cell.cz == best->cz && cell.cx < best->cx));
                if (closer || tie)
                {
                    best = &cell;
                    bestD = d;
                }
            }
            if (best == nullptr)
                return false;
            out = *best;
            return true;
        }

        bool mapsReady(const HeightMap* height, const SplatMap* splat)
        {
            return height != nullptr && splat != nullptr && height->valid() && splat->valid();
        }
    } // namespace

    bool findTrailRoute(const TrailRouteIn& in, std::vector<TrailPoint>& points)
    {
        points.clear();
        if (!mapsReady(in.height, in.splat))
            return false;
        std::vector<FoliageWaterVolume> scratch;
        const std::vector<FoliageWaterVolume>& water = volumesOrEmpty(in.water, scratch);
        ProgressSink progress;
        progress.fn = in.onProgress;
        progress.user = in.user;
        progress.span = 1;
        const auto started = std::chrono::steady_clock::now();
        TrailGrid grid;
        const BuildStatus built = buildTrailGrid(*in.height, *in.splat, water, in.seaLevel, progress, grid);
        if (built == BuildStatus::Bad)
            return false;
        progress.span = grid.cellCount > 0 ? grid.cellCount : 1;
        if (built == BuildStatus::Cancelled)
        {
            logGrid(grid, started);
            return false;
        }
        const int sx = cellOf(in.startX, grid.originX, grid.cell, grid.cellsX);
        const int sz = cellOf(in.startZ, grid.originZ, grid.cell, grid.cellsZ);
        const int ex = cellOf(in.endX, grid.originX, grid.cell, grid.cellsX);
        const int ez = cellOf(in.endZ, grid.originZ, grid.cell, grid.cellsZ);
        const RouteStatus status = routeOnGrid(grid, in.kind, sx, sz, ex, ez, in.seed, progress, points);
        logGrid(grid, started);
        if (status == RouteStatus::Cancelled)
        {
            points.clear();
            return false;
        }
        if (status != RouteStatus::Routed)
            points.clear();
        return true;
    }

    bool generateGameTrails(const TrailGenerateIn& in, TrailSet& set, TrailGenerateOut* out)
    {
        if (!mapsReady(in.height, in.splat))
            return false;
        std::vector<FoliageWaterVolume> scratch;
        const std::vector<FoliageWaterVolume>& water = volumesOrEmpty(in.water, scratch);
        ProgressSink progress;
        progress.fn = in.onProgress;
        progress.user = in.user;
        progress.span = 1;
        const auto started = std::chrono::steady_clock::now();
        TrailGrid grid;
        const BuildStatus built = buildTrailGrid(*in.height, *in.splat, water, in.seaLevel, progress, grid);
        if (built == BuildStatus::Bad)
            return false;
        progress.span = grid.cellCount > 0 ? grid.cellCount : 1;
        if (built == BuildStatus::Cancelled)
        {
            logGrid(grid, started);
            return false;
        }

        std::vector<TrailPoiCell> waterPois;
        std::vector<TrailPoiCell> clearings;
        std::vector<TrailPoiCell> forest;
        collectPois(grid, waterPois, clearings, forest);

        const int limit = clampMaxAutoPaths(in.maxAutoPaths);
        const float humanWidth = clampTrailWidth(in.humanWidth, kTrailHumanWidth);
        const float animalWidth = clampTrailWidth(in.animalWidth, kTrailAnimalWidth);
        std::vector<TrailPath> generated;
        bool cancelled = false;

        auto addRoute = [&](const TrailPoiCell& from, const TrailPoiCell& to, TrailKind kind, float width) {
            if (cancelled || static_cast<int>(generated.size()) >= limit)
                return;
            if (from.cx == to.cx && from.cz == to.cz)
                return;
            std::vector<TrailPoint> points;
            const RouteStatus status = routeOnGrid(grid, kind, from.cx, from.cz, to.cx, to.cz, in.seed, progress, points);
            if (status == RouteStatus::Cancelled)
            {
                cancelled = true;
                return;
            }
            if (status != RouteStatus::Routed || points.size() < 2)
                return;
            TrailPath path;
            path.kind = kind;
            path.width = width;
            path.authored = false;
            path.closed = false;
            path.points = std::move(points);
            generated.push_back(std::move(path));
        };

        for (const TrailPoiCell& shore : waterPois)
        {
            if (cancelled || static_cast<int>(generated.size()) >= limit)
                break;
            TrailPoiCell clearing;
            if (!nearestClearing(clearings, shore, clearing))
                break;
            addRoute(shore, clearing, TrailKind::Human, humanWidth);
        }
        for (size_t i = 1; i < clearings.size(); ++i)
        {
            if (cancelled || static_cast<int>(generated.size()) >= limit)
                break;
            addRoute(clearings[i - 1u], clearings[i], TrailKind::Human, humanWidth);
        }
        int animalBranches = 0;
        for (const TrailPoiCell& edge : forest)
        {
            if (cancelled || static_cast<int>(generated.size()) >= limit || animalBranches >= 4)
                break;
            TrailPoiCell clearing;
            if (!nearestClearing(clearings, edge, clearing))
                break;
            const size_t before = generated.size();
            addRoute(edge, clearing, TrailKind::Animal, animalWidth);
            if (generated.size() > before)
                ++animalBranches;
        }

        logGrid(grid, started);
        if (cancelled)
            return false;

        std::vector<TrailPath> kept;
        kept.reserve(set.paths.size() + generated.size());
        for (const TrailPath& path : set.paths)
        {
            if (path.authored)
                kept.push_back(path);
        }
        for (const TrailPath& path : generated)
            kept.push_back(path);
        set.paths = std::move(kept);
        if (out != nullptr)
        {
            out->water = std::move(waterPois);
            out->clearings = std::move(clearings);
            out->forest = std::move(forest);
            out->generated = std::move(generated);
        }
        return true;
    }

    bool stampTrailDirt(const HeightMap& height, SplatMap& splat, const std::vector<TrailPath>& paths, bool (*onProgress)(float t, const char* phase, void* user), void* user)
    {
        if (!height.valid() || !splat.valid() || !(height.cellSize() > 0.0f))
            return false;
        if (onProgress != nullptr && !onProgress(0.0f, "splat", user))
            return false;

        int painted = 0;
        for (const TrailPath& path : paths)
        {
            if (path.points.size() < 2)
                continue;
            bool finite = true;
            for (const TrailPoint& point : path.points)
            {
                if (!std::isfinite(point.x) || !std::isfinite(point.z))
                    finite = false;
            }
            if (!finite)
                continue;

            const float fallback = path.kind == TrailKind::Animal ? kTrailAnimalWidth : kTrailHumanWidth;
            const float width = clampTrailWidth(path.width, fallback);
            const float radius = (width * 0.5f) / height.cellSize();
            std::vector<Math::Vector2f> controls;
            controls.reserve(path.points.size());
            for (const TrailPoint& point : path.points)
                controls.emplace_back(point.x, point.z);
            std::vector<Math::Vector2f> samples;
            sampleTrailPolyline(controls, path.closed, samples);
            for (const Math::Vector2f& sample : samples)
            {
                float fx = 0.0f;
                float fz = 0.0f;
                height.worldToSample(sample.x, sample.y, fx, fz);
                splat.paintDisk(fx, fz, radius, 0, 1.0f);
                ++painted;
                if (onProgress != nullptr && (painted % 64) == 0 && !onProgress(0.0f, "splat", user))
                    return false;
            }
        }
        return true;
    }

} // namespace Dark::Terrain
