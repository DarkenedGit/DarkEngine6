#include "Terrain/TrailPath.h"

#include "Core/Json.h"
#include "Core/Log.h"
#include "Math/Curves.h"
#include "Terrain/FoliageCover.h"

#include <cmath>
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

} // namespace Dark::Terrain
