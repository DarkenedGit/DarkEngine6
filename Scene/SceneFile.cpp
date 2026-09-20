#include "Scene/SceneFile.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "Math/MathDefines.h"
#include "Terrain/TerrainTileFile.h"

#include "third_party/nlohmann/json.hpp"

#include <fstream>
#include <sstream>

namespace Dark
{
namespace
{

using json = nlohmann::json;

json vec2ToJson(const Math::Vector2f& v)
{
    return json::array({ v.x, v.y });
}

json vec3ToJson(const Math::Vector3f& v)
{
    return json::array({ v.x, v.y, v.z });
}

json quatToJson(const Math::Quaternion& q)
{
    return json::array({ q.w, q.x, q.y, q.z });
}

json colorToJson(const float c[4])
{
    return json::array({ c[0], c[1], c[2], c[3] });
}

void applyTypeLightDefaults(SceneObjectData& o)
{
    if (o.type == SceneObjectType::SpotLight)
    {
        o.lightIntensity = 2513.0f; // 800*π; JSON 800 stays
        o.lightRange     = 16.0f;
    }
    else
    {
        o.lightIntensity = 1885.0f; // 600*π; JSON 600 stays
        o.lightRange     = 8.0f;
    }
    o.lightInnerDeg     = 12.0f;
    o.lightOuterDeg     = 25.0f;
    o.lightSourceRadius = 0.05f;
    o.lightEnabled      = true;
}

bool readVec2(const json& j, Math::Vector2f& out, std::string* err, const char* field)
{
    if (!j.is_array() || j.size() < 2)
    {
        if (err)
            *err = std::string("expected array[2] for ") + field;
        return false;
    }
    out.x = j[0].get<float>();
    out.y = j[1].get<float>();
    return true;
}

bool readVec3(const json& j, Math::Vector3f& out, std::string* err, const char* field)
{
    if (!j.is_array() || j.size() < 2)
    {
        if (err)
            *err = std::string("expected array[2|3] for ") + field;
        return false;
    }
    out.x = j[0].get<float>();
    out.y = j[1].get<float>();
    out.z = (j.size() >= 3) ? j[2].get<float>() : 0.0f;
    return true;
}

bool readQuat(const json& j, Math::Quaternion& out, std::string* err, const char* field)
{
    if (!j.is_array() || j.size() < 4)
    {
        if (err)
            *err = std::string("expected array[4] for ") + field;
        return false;
    }
    out.w = j[0].get<float>();
    out.x = j[1].get<float>();
    out.y = j[2].get<float>();
    out.z = j[3].get<float>();
    return true;
}

bool readColor(const json& j, float out[4], std::string* err, const char* field)
{
    if (!j.is_array() || j.size() < 3)
    {
        if (err)
            *err = std::string("expected array[3|4] for ") + field;
        return false;
    }
    out[0] = j[0].get<float>();
    out[1] = j[1].get<float>();
    out[2] = j[2].get<float>();
    out[3] = (j.size() >= 4) ? j[3].get<float>() : 1.0f;
    return true;
}

} // namespace

bool saveSceneToJson(const std::filesystem::path& path, const SceneFileData& scene, std::string* errorOut)
{
    json root;
    root["version"] = scene.version > 0 ? scene.version : 1;
    root["name"]    = scene.name.empty() ? "untitled" : scene.name;
    root["mode"]    = toString(scene.mode);
    if (scene.mode != SceneMode::Scene2D)
    {
        root["environment"]     = scene.environment;
        root["iblIntensity"]    = scene.iblIntensity;
        root["iblRotationRadY"] = scene.iblRotationRadY;
        if (scene.hasTerrain)
        {
            json t;
            t["bindLayout"]     = scene.terrain.bindLayout;
            t["chunkCells"]     = scene.terrain.chunkCells;
            t["heightBlendK"]   = scene.terrain.heightBlendK;
            t["heightBlendT"]   = scene.terrain.heightBlendT;
            t["triplanarSlope"] = scene.terrain.triplanarSlope;
            const uint64_t tileCount = scene.terrain.hasGrid
                ? static_cast<uint64_t>(scene.terrain.grid.tilesX) * scene.terrain.grid.tilesZ
                : 0ull;
            // Working 4097 sidecars exceed the 32 MB DEHF cap; tiles>1 store coarse + per-tile files only.
            if (!scene.terrain.hasGrid || tileCount <= 1ull)
            {
                t["heightFile"] = scene.terrain.heightFile;
                t["splatFile"]  = scene.terrain.splatFile;
            }
            if (scene.terrain.hasGrid)
            {
                json g;
                g["tilesX"]       = scene.terrain.grid.tilesX;
                g["tilesZ"]       = scene.terrain.grid.tilesZ;
                g["tileCells"]    = scene.terrain.grid.tileCells;
                g["cellSize"]     = scene.terrain.grid.cellSize;
                g["origin"]       = vec3ToJson(scene.terrain.grid.origin);
                g["heightScale"]  = scene.terrain.grid.heightScale;
                g["seed"]         = scene.terrain.grid.seed;
                g["coarseFile"]   = scene.terrain.grid.coarseFile;
                g["tileDir"]      = scene.terrain.grid.tileDir;
                g["seaLevel"]     = scene.terrain.grid.seaLevel;
                g["residentRing"] = scene.terrain.grid.residentRing;
                t["grid"]         = std::move(g);
            }
            json layers = json::array();
            const int n = scene.terrain.layerCount > 0 ? scene.terrain.layerCount : 4;
            const int count = n > 4 ? 4 : n;
            for (int i = 0; i < count; ++i)
            {
                json layer;
                layer["albedo"] = scene.terrain.layers[i].albedo;
                layer["normal"] = scene.terrain.layers[i].normal;
                layer["orm"]    = scene.terrain.layers[i].orm;
                layer["tiling"] = scene.terrain.layers[i].tiling;
                layer["tint"]   = colorToJson(scene.terrain.layers[i].tint);
                layers.push_back(std::move(layer));
            }
            t["layers"] = std::move(layers);
            root["terrain"] = std::move(t);
        }
    }
    if (scene.mode == SceneMode::Scene2D)
    {
        json world;
        world["min"] = vec2ToJson(scene.worldMin);
        world["max"] = vec2ToJson(scene.worldMax);
        root["world"] = std::move(world);
    }

    json arr = json::array();
    for (const SceneObjectData& o : scene.objects)
    {
        json jo;
        jo["type"]     = toString(o.type);
        jo["position"] = vec3ToJson(o.position);
        jo["rotation"] = quatToJson(o.rotation);
        jo["scale"]    = vec3ToJson(o.scale);
        jo["color"]    = colorToJson(o.color);

        if (o.hasParticle || o.type == SceneObjectType::ParticleEmitter)
        {
            json p;
            p["name"]            = o.particleName;
            p["maxParticles"]    = o.maxParticles;
            p["emissionRate"]    = o.emissionRate;
            p["duration"]        = o.duration;
            p["looping"]         = o.looping;
            p["lifetime"]        = json::array({ o.lifetimeMin, o.lifetimeMax });
            p["startSpeed"]      = json::array({ o.startSpeedMin, o.startSpeedMax });
            p["startSize"]       = json::array({ o.startSizeMin, o.startSizeMax });
            p["endSize"]         = json::array({ o.endSizeMin, o.endSizeMax });
            p["startColor"]      = colorToJson(o.startColor);
            p["endColor"]        = colorToJson(o.endColor);
            p["gravity"]         = vec3ToJson(o.gravity);
            p["direction"]       = vec3ToJson(o.direction);
            p["spreadDegrees"]   = o.spreadDegrees;
            p["shape"]           = o.shape;
            p["shapeSize"]       = vec3ToJson(o.shapeSize);
            p["additiveBlend"]   = o.additiveBlend;
            p["simulationSpeed"] = o.simulationSpeed;
            p["renderMode"]      = o.renderMode;
            p["ribbonCount"]     = o.ribbonCount;
            p["ribbonUvScale"]   = o.ribbonUvScale;
            jo["particle"]       = std::move(p);
        }

        if (o.hasLight || o.type == SceneObjectType::PointLight || o.type == SceneObjectType::SpotLight
            || isGlobalLightType(o.type))
        {
            json light;
            light["intensity"]    = o.lightIntensity;
            light["range"]        = o.lightRange;
            light["inner"]        = o.lightInnerDeg;
            light["outer"]        = o.lightOuterDeg;
            light["sourceRadius"] = o.lightSourceRadius;
            light["enabled"]      = o.lightEnabled;
            jo["light"]           = std::move(light);
        }
        if (o.emissive != 0.0f)
            jo["emissive"] = o.emissive;
        if (o.emissiveMeshIndex >= 0)
            jo["emissiveMesh"] = o.emissiveMeshIndex;

        arr.push_back(std::move(jo));
    }
    root["objects"] = std::move(arr);

    std::error_code ec;
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            if (errorOut)
                *errorOut = "failed to create directory: " + ec.message();
            return false;
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        if (errorOut)
            *errorOut = "failed to open for write: " + path.string();
        return false;
    }

    out << root.dump(2);
    if (!out)
    {
        if (errorOut)
            *errorOut = "failed while writing: " + path.string();
        return false;
    }

    DE_LOG_INFO("SceneFile: saved {} objects ({}) → {}", scene.objects.size(), toString(scene.mode), path.string());
    return true;
}

bool loadSceneFromJson(const std::filesystem::path& path, SceneFileData& outScene, std::string* errorOut)
{
    outScene = SceneFileData{};

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        if (errorOut)
            *errorOut = "failed to open for read: " + path.string();
        return false;
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();

    const json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        if (errorOut)
            *errorOut = "invalid JSON in " + path.string();
        return false;
    }

    outScene.version = root.value("version", 1);
    outScene.name    = root.value("name", std::string("untitled"));
    const std::string modeStr = root.value("mode", std::string("3d"));
    if (!tryParseSceneMode(modeStr, outScene.mode))
        outScene.mode = SceneMode::Scene3D;
    if (outScene.mode != SceneMode::Scene2D)
    {
        outScene.environment     = root.value("environment", std::string("env/studio_gradient.hdr"));
        outScene.iblIntensity    = root.value("iblIntensity", 1.0f);
        outScene.iblRotationRadY = root.value("iblRotationRadY", 0.0f);
        if (root.contains("terrain") && root["terrain"].is_object())
        {
            const json& t = root["terrain"];
            int bindLayout = TerrainSceneDesc::kBindLayoutV1;
            if (t.contains("bindLayout") && t["bindLayout"].is_number())
                bindLayout = t["bindLayout"].get<int>();
            if (bindLayout != TerrainSceneDesc::kBindLayoutV1)
            {
                DE_LOG_ERROR("SceneFile: terrain bindLayout {} unknown", bindLayout);
            }
            else
            {
                TerrainSceneDesc desc{};
                desc.bindLayout     = bindLayout;
                desc.chunkCells     = t.value("chunkCells", 16);
                desc.heightBlendK   = t.value("heightBlendK", 0.5f);
                desc.heightBlendT   = t.value("heightBlendT", 0.1f);
                desc.triplanarSlope = t.value("triplanarSlope", 0.45f);
                desc.heightFile     = t.value("heightFile", std::string());
                desc.splatFile      = t.value("splatFile", std::string());
                if (t.contains("grid") && t["grid"].is_object())
                {
                    const json& g = t["grid"];
                    TerrainGridSceneDesc grid{};
                    grid.tilesX       = g.value("tilesX", 4u);
                    grid.tilesZ       = g.value("tilesZ", 4u);
                    grid.tileCells    = g.value("tileCells", 512u);
                    grid.cellSize     = g.value("cellSize", 1.0f);
                    grid.heightScale  = g.value("heightScale", 80.0f);
                    grid.seed         = g.value("seed", 1337u);
                    grid.coarseFile   = g.value("coarseFile", std::string());
                    grid.tileDir      = g.value("tileDir", std::string());
                    grid.seaLevel     = g.value("seaLevel", 0.0f);
                    grid.residentRing = g.value("residentRing", 5);
                    if (g.contains("origin"))
                        readVec3(g["origin"], grid.origin, nullptr, "terrain.grid.origin");
                    if (grid.tilesX < 1u || grid.tilesX > Terrain::kMaxWorldTiles
                        || grid.tilesZ < 1u || grid.tilesZ > Terrain::kMaxWorldTiles)
                    {
                        DE_LOG_ERROR("SceneFile: terrain.grid tiles {}x{} not in [1, {}]", grid.tilesX, grid.tilesZ, Terrain::kMaxWorldTiles);
                    }
                    else
                    {
                        desc.hasGrid = true;
                        desc.grid    = std::move(grid);
                    }
                }
                if (t.contains("layers") && t["layers"].is_array())
                {
                    const json& layers = t["layers"];
                    if (layers.size() > 4)
                        DE_LOG_WARN("SceneFile: terrain layers {} > 4 — using first 4", layers.size());
                    const int n = layers.size() > 4 ? 4 : static_cast<int>(layers.size());
                    desc.layerCount = n;
                    for (int i = 0; i < n; ++i)
                    {
                        if (!layers[i].is_object())
                            continue;
                        const json& layer = layers[i];
                        desc.layers[i].albedo = layer.value("albedo", std::string());
                        desc.layers[i].normal = layer.value("normal", std::string());
                        desc.layers[i].orm    = layer.value("orm", std::string());
                        desc.layers[i].tiling = layer.value("tiling", 8.0f);
                        if (layer.contains("tint"))
                            readColor(layer["tint"], desc.layers[i].tint, nullptr, "terrain.layers.tint");
                    }
                }
                outScene.hasTerrain = true;
                outScene.terrain    = std::move(desc);
            }
        }
    }

    if (root.contains("world") && root["world"].is_object())
    {
        const json& w = root["world"];
        std::string err;
        if (w.contains("min") && !readVec2(w["min"], outScene.worldMin, &err, "world.min"))
        {
            if (errorOut)
                *errorOut = err;
            return false;
        }
        if (w.contains("max") && !readVec2(w["max"], outScene.worldMax, &err, "world.max"))
        {
            if (errorOut)
                *errorOut = err;
            return false;
        }
    }

    if (!root.contains("objects") || !root["objects"].is_array())
    {
        if (errorOut)
            *errorOut = "missing \"objects\" array";
        return false;
    }

    for (const json& jo : root["objects"])
    {
        if (!jo.is_object())
            continue;

        SceneObjectData o{};
        const std::string typeStr = jo.value("type", std::string("cube"));
        if (!tryParseSceneObjectType(typeStr, o.type))
        {
            DE_LOG_WARN("SceneFile: unknown type '{}' — skipped", typeStr);
            continue;
        }

        std::string err;
        if (jo.contains("position") && !readVec3(jo["position"], o.position, &err, "position"))
        {
            if (errorOut)
                *errorOut = err;
            return false;
        }
        if (jo.contains("rotation") && !readQuat(jo["rotation"], o.rotation, &err, "rotation"))
        {
            if (errorOut)
                *errorOut = err;
            return false;
        }
        if (jo.contains("scale") && !readVec3(jo["scale"], o.scale, &err, "scale"))
        {
            if (errorOut)
                *errorOut = err;
            return false;
        }
        if (jo.contains("color") && !readColor(jo["color"], o.color, &err, "color"))
        {
            if (errorOut)
                *errorOut = err;
            return false;
        }

        if (jo.contains("particle") && jo["particle"].is_object())
        {
            const json& p = jo["particle"];
            o.hasParticle     = true;
            o.particleName    = p.value("name", std::string("Emitter"));
            o.maxParticles    = p.value("maxParticles", 512u);
            o.emissionRate    = p.value("emissionRate", 40.0f);
            o.duration        = p.value("duration", 0.0f);
            o.looping         = p.value("looping", true);
            o.spreadDegrees   = p.value("spreadDegrees", 25.0f);
            o.shape           = p.value("shape", 0);
            o.additiveBlend   = p.value("additiveBlend", true);
            o.simulationSpeed = p.value("simulationSpeed", 1.0f);
            o.renderMode      = p.value("renderMode", 0);
            o.ribbonCount     = p.value("ribbonCount", 1u);
            o.ribbonUvScale   = p.value("ribbonUvScale", 1.0f);
            if (p.contains("lifetime") && p["lifetime"].is_array() && p["lifetime"].size() >= 2)
            {
                o.lifetimeMin = p["lifetime"][0].get<float>();
                o.lifetimeMax = p["lifetime"][1].get<float>();
            }
            if (p.contains("startSpeed") && p["startSpeed"].is_array() && p["startSpeed"].size() >= 2)
            {
                o.startSpeedMin = p["startSpeed"][0].get<float>();
                o.startSpeedMax = p["startSpeed"][1].get<float>();
            }
            if (p.contains("startSize") && p["startSize"].is_array() && p["startSize"].size() >= 2)
            {
                o.startSizeMin = p["startSize"][0].get<float>();
                o.startSizeMax = p["startSize"][1].get<float>();
            }
            if (p.contains("endSize") && p["endSize"].is_array() && p["endSize"].size() >= 2)
            {
                o.endSizeMin = p["endSize"][0].get<float>();
                o.endSizeMax = p["endSize"][1].get<float>();
            }
            if (p.contains("startColor"))
                readColor(p["startColor"], o.startColor, nullptr, "startColor");
            if (p.contains("endColor"))
                readColor(p["endColor"], o.endColor, nullptr, "endColor");
            if (p.contains("gravity"))
                readVec3(p["gravity"], o.gravity, nullptr, "gravity");
            if (p.contains("direction"))
                readVec3(p["direction"], o.direction, nullptr, "direction");
            if (p.contains("shapeSize"))
                readVec3(p["shapeSize"], o.shapeSize, nullptr, "shapeSize");
        }
        else if (o.type == SceneObjectType::ParticleEmitter)
        {
            o.hasParticle = true;
        }

        if (jo.contains("light") && jo["light"].is_object())
        {
            const json& light = jo["light"];
            applyTypeLightDefaults(o);
            o.hasLight          = true;
            o.lightIntensity    = light.value("intensity", o.lightIntensity);
            o.lightRange        = light.value("range", o.lightRange);
            o.lightInnerDeg     = light.value("inner", o.lightInnerDeg);
            o.lightOuterDeg     = light.value("outer", o.lightOuterDeg);
            o.lightSourceRadius = light.value("sourceRadius", o.lightSourceRadius);
            o.lightEnabled      = light.value("enabled", o.lightEnabled);
        }
        else if (o.type == SceneObjectType::PointLight || o.type == SceneObjectType::SpotLight)
        {
            o.hasLight = true;
            applyTypeLightDefaults(o);
        }
        else if (isGlobalLightType(o.type))
        {
            o.hasLight       = true;
            o.lightIntensity = (o.type == SceneObjectType::DirectionalLight) ? Math::Pi : 1.0f;
            o.lightEnabled   = true;
        }

        if (jo.contains("emissive") && jo["emissive"].is_number())
            o.emissive = jo["emissive"].get<float>();
        if (jo.contains("emissiveMesh") && jo["emissiveMesh"].is_number_integer())
            o.emissiveMeshIndex = jo["emissiveMesh"].get<int>();

        if (o.scale.x == 0.0f)
            o.scale.x = 1.0f;
        if (o.scale.y == 0.0f)
            o.scale.y = 1.0f;
        if (o.scale.z == 0.0f)
            o.scale.z = 1.0f;

        outScene.objects.push_back(o);
    }

    DE_LOG_INFO("SceneFile: loaded {} objects ({}) ← {}", outScene.objects.size(), toString(outScene.mode), path.string());
    return true;
}

std::filesystem::path defaultScenePath(const std::filesystem::path& preferredName)
{
    namespace fs = std::filesystem;
    const fs::path name = preferredName.empty() ? fs::path("level.json") : preferredName;

    fs::path fallback;
    for (const fs::path& root : contentRootCandidates())
    {
        const fs::path  c  = root / "scenes" / name;
        std::error_code ec;
        if (fs::exists(c, ec) && !ec)
        {
            const fs::path canonical = fs::weakly_canonical(c, ec);
            return ec ? c : canonical;
        }
        if (fallback.empty())
            fallback = c;
    }

    if (!fallback.empty())
        return fallback;

    return fs::path("content") / "scenes" / name;
}

} // namespace Dark
