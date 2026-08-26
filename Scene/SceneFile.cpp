#include "Scene/SceneFile.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"

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
