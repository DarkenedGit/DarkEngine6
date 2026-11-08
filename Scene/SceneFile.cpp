#include "Scene/SceneFile.h"
#include "Assets/AssetManager.h"
#include "Core/ContentRoots.h"
#include "Core/Json.h"
#include "Core/Log.h"
#include "Math/MathDefines.h"
#include "Math/MathHelper.h"
#include "Terrain/GrassTypes.h"
#include "Terrain/TerrainTileFile.h"
#include "Terrain/WindField.h"

#include "third_party/nlohmann/json.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>

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

// NaN and negatives become 0. Math::Clamp would leave a NaN in place.
float clampShedScale(float shed)
{
    if (!(shed > 0.0f))
        return 0.0f;
    if (shed > 1.0f)
        return 1.0f;
    return shed;
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

bool jsonElemFloat(const json& value, float& out);

bool readVec2(const json& j, Math::Vector2f& out, std::string* err, const char* field)
{
    if (!j.is_array() || j.size() < 2)
    {
        if (err)
            *err = std::string("expected array[2] for ") + field;
        return false;
    }
    if (!jsonElemFloat(j[0], out.x) || !jsonElemFloat(j[1], out.y))
    {
        if (err)
            *err = std::string("expected array[2] for ") + field;
        return false;
    }
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
    float z = 0.0f;
    if (!jsonElemFloat(j[0], out.x) || !jsonElemFloat(j[1], out.y) || (j.size() >= 3 && !jsonElemFloat(j[2], z)))
    {
        if (err)
            *err = std::string("expected array[2|3] for ") + field;
        return false;
    }
    out.z = (j.size() >= 3) ? z : 0.0f;
    return true;
}

void readSpawnerBlock(const json& j, SpawnerDesc& desc)
{
    std::string text;
    SpawnerMode mode{};
    if (jsonStringOr(j, "mode", text, std::string(), "SceneFile") && tryParseSpawnerMode(text, mode))
        desc.mode = mode;
    SpawnerUnit unit{};
    if (jsonStringOr(j, "unit", text, std::string(), "SceneFile") && tryParseSpawnerUnit(text, unit))
        desc.unit = unit;
    SpawnerWaveGate gate{};
    if (jsonStringOr(j, "gate", text, std::string(), "SceneFile") && tryParseSpawnerWaveGate(text, gate))
        desc.gate = gate;
    jsonIntOr(j, "count", desc.count, desc.count, "SceneFile");
    jsonFloatOr(j, "radius", desc.radius, desc.radius, "SceneFile");
    jsonFloatOr(j, "respawnDelay", desc.respawnDelay, desc.respawnDelay, "SceneFile");
    jsonFloatOr(j, "waveInterval", desc.waveInterval, desc.waveInterval, "SceneFile");
    jsonIntOr(j, "maxWaves", desc.maxWaves, desc.maxWaves, "SceneFile");
    double seed = static_cast<double>(desc.seed);
    if (jsonDoubleOr(j, "seed", seed, seed, "SceneFile") && seed >= 1.0 && seed <= 4294967295.0)
        desc.seed = static_cast<uint32_t>(seed);
    jsonBoolOr(j, "enabled", desc.enabled, desc.enabled, "SceneFile");
    clampSpawnerDesc(desc);
}

bool readQuat(const json& j, Math::Quaternion& out, std::string* err, const char* field)
{
    if (!j.is_array() || j.size() < 4)
    {
        if (err)
            *err = std::string("expected array[4] for ") + field;
        return false;
    }
    if (!jsonElemFloat(j[0], out.w) || !jsonElemFloat(j[1], out.x) || !jsonElemFloat(j[2], out.y) || !jsonElemFloat(j[3], out.z))
    {
        if (err)
            *err = std::string("expected array[4] for ") + field;
        return false;
    }
    return true;
}

Sky::WeatherState weatherPreset(const std::string& name)
{
    if (name == "clear")
        return Sky::WeatherState::Clear();
    if (name == "overcast")
        return Sky::WeatherState::Overcast();
    if (name == "storm")
        return Sky::WeatherState::Storm();
    return Sky::WeatherState::PartlyCloudy();
}

bool weatherMatches(const Sky::WeatherState& a, const Sky::WeatherState& b)
{
    return std::fabs(a.cloudCoverage - b.cloudCoverage) < 1.0e-3f && std::fabs(a.turbidity - b.turbidity) < 1.0e-3f
        && std::fabs(a.windSpeed - b.windSpeed) < 1.0e-3f && std::fabs(a.windDir.x - b.windDir.x) < 1.0e-3f
        && std::fabs(a.windDir.y - b.windDir.y) < 1.0e-3f && std::fabs(a.rain - b.rain) < 1.0e-3f;
}

const char* weatherNameFor(const Sky::WeatherState& weather)
{
    if (weatherMatches(weather, Sky::WeatherState::Clear()))
        return "clear";
    if (weatherMatches(weather, Sky::WeatherState::PartlyCloudy()))
        return "partly";
    if (weatherMatches(weather, Sky::WeatherState::Overcast()))
        return "overcast";
    if (weatherMatches(weather, Sky::WeatherState::Storm()))
        return "storm";
    return "custom";
}

json skyToJson(const SkySceneDesc& sky)
{
    json s;
    s["timeOfDay"]     = sky.timeOfDay;
    s["dayOfYear"]     = sky.dayOfYear;
    s["latitude"]      = sky.latitude;
    s["timeScale"]     = sky.timeScale;
    s["weather"]       = sky.weather.empty() ? std::string("partly") : sky.weather;
    s["cloudCoverage"] = sky.cloudCoverage;
    s["turbidity"]     = sky.turbidity;
    s["windSpeed"]     = sky.windSpeed;
    s["windDir"]       = json::array({ sky.windDir[0], sky.windDir[1] });
    s["rain"]          = sky.rain;

    s["cloudAltitude"]     = sky.cloudAltitude;
    s["cloudAmount"]       = sky.cloudAmount;
    s["cloudWindSpeedMps"] = sky.cloudWindSpeedMps;
    return s;
}

json fogToJson(const FogSceneDesc& fog)
{
    json f;
    f["auto"]             = fog.autoFromWeather;
    f["distanceScale"]    = fog.distanceScale;
    f["heightScale"]      = fog.heightScale;
    f["valleyScale"]      = fog.valleyScale;
    f["distanceDensity"]  = fog.distanceDensity;
    f["heightDensity"]    = fog.heightDensity;
    f["valleyDensity"]    = fog.valleyDensity;
    f["heightFalloff"]    = fog.heightFalloff;
    f["valleyHeight"]     = fog.valleyHeight;
    f["color"]            = json::array({ fog.color[0], fog.color[1], fog.color[2] });
    return f;
}

json waterToJson(const WaterSceneDesc& water)
{
    json w;
    if (water.hasLevel)
        w["level"] = water.level;
    w["levelFraction"]  = water.levelFraction;
    w["chunkCells"]     = water.chunkCells;
    w["flowDir"]        = json::array({ water.flowDir[0], water.flowDir[1] });
    w["flowStrength"]   = water.flowStrength;
    w["steepness"]      = water.steepness;
    w["amplitudeScale"] = water.amplitudeScale;
    w["speedScale"]     = water.speedScale;
    w["flowSpeed"]      = water.flowSpeed;
    w["foam"]           = water.foam;
    w["foamWidthScale"] = water.foamWidthScale;
    if (std::fabs(water.detail - 1.0f) > 1.0e-4f)
        w["detail"] = water.detail;
    json lod = json::array();
    const int lodCount = water.lodDistanceCount < 1 ? 1 : (water.lodDistanceCount > 8 ? 8 : water.lodDistanceCount);
    for (int i = 0; i < lodCount; ++i)
        lod.push_back(water.lodDistances[i]);
    w["lodDistances"] = std::move(lod);
    if (water.waveCount > 0)
    {
        json waves = json::array();
        const int n = water.waveCount > 4 ? 4 : water.waveCount;
        for (int i = 0; i < n; ++i)
        {
            json wave;
            wave["angle"]     = water.waves[i].angleFromFlow;
            wave["frequency"] = water.waves[i].frequency;
            wave["amplitude"] = water.waves[i].amplitude;
            wave["speed"]     = water.waves[i].speed;
            waves.push_back(std::move(wave));
        }
        w["waves"] = std::move(waves);
    }
    return w;
}

json exposureToJson(const ExposureSceneDesc& exposure)
{
    json e;
    e["auto"]        = exposure.autoExposure;
    e["evBias"]      = exposure.evBias;
    e["maxEv"]       = exposure.maxEv;
    e["adaptBright"] = exposure.adaptBright;
    e["adaptDark"]   = exposure.adaptDark;
    return e;
}

json stealthToJson(const StealthSceneDesc& stealth)
{
    json s;
    s["awareness"] = stealth.awareness;
    if (stealth.hasHearStand)
        s["hearStand"] = stealth.hearStand;
    if (stealth.hasHearWalk)
        s["hearWalk"] = stealth.hearWalk;
    if (stealth.hasHearSprint)
        s["hearSprint"] = stealth.hearSprint;
    if (stealth.hasHearCrouchWalk)
        s["hearCrouchWalk"] = stealth.hearCrouchWalk;
    if (stealth.hasHearCrouchStill)
        s["hearCrouchStill"] = stealth.hearCrouchStill;
    if (stealth.hasSettings && stealth.settingsMask != 0)
    {
        std::string text;
        if (AI::writeAwarenessSettings(stealth.settings, stealth.settingsMask, text))
        {
            const json parsed = json::parse(text, nullptr, false);
            if (!parsed.is_discarded() && parsed.is_object())
                s["settings"] = parsed;
        }
    }
    return s;
}

void readStealthHear(const json& s, const char* key, bool& has, float& value)
{
    if (!s.contains(key))
        return;
    float number = 0.0f;
    if (!jsonFloatOr(s, key, number, 0.0f, "SceneFile"))
        return;
    has   = true;
    value = number < 0.0f ? 0.0f : number;
}

bool readColor(const json& j, float out[4], std::string* err, const char* field)
{
    if (!j.is_array() || j.size() < 3)
    {
        if (err)
            *err = std::string("expected array[3|4] for ") + field;
        return false;
    }
    float a = 1.0f;
    if (!jsonElemFloat(j[0], out[0]) || !jsonElemFloat(j[1], out[1]) || !jsonElemFloat(j[2], out[2])
        || (j.size() >= 4 && !jsonElemFloat(j[3], a)))
    {
        if (err)
            *err = std::string("expected array[3|4] for ") + field;
        return false;
    }
    out[3] = (j.size() >= 4) ? a : 1.0f;
    return true;
}

void clampGrassScene(GrassSceneDesc& desc)
{
    Terrain::GrassParams params;
    params.enabled          = desc.enabled;
    params.heightMetres     = desc.heightMetres;
    params.flexibility      = desc.flexibility;
    params.densityScale     = desc.densityScale;
    params.windBaseYaw      = desc.windBaseYaw;
    params.windDeflection   = desc.windDeflection;
    params.windTipMetres    = desc.windTipMetres;
    params.windSpatialFreq  = desc.windSpatialFreq;
    params.windTemporalFreq = desc.windTemporalFreq;
    params.faceEdge         = desc.faceEdge;
    params.faceYaw          = desc.faceYaw;
    params.faceLean         = desc.faceLean;
    params.faceSteep        = desc.faceSteep;
    params.seed             = desc.seed;
    Terrain::clampGrassParams(params);
    desc.enabled          = params.enabled;
    desc.heightMetres     = params.heightMetres;
    desc.flexibility      = params.flexibility;
    desc.densityScale     = params.densityScale;
    desc.windBaseYaw      = params.windBaseYaw;
    desc.windDeflection   = params.windDeflection;
    desc.windTipMetres    = params.windTipMetres;
    desc.windSpatialFreq  = params.windSpatialFreq;
    desc.windTemporalFreq = params.windTemporalFreq;
    desc.faceEdge         = params.faceEdge;
    desc.faceYaw          = params.faceYaw;
    desc.faceLean         = params.faceLean;
    desc.faceSteep        = params.faceSteep;
    desc.seed             = params.seed;
}

json windKindToJson(const Terrain::TreeWindParams& kind)
{
    json out;
    out["influence"]       = kind.influence;
    out["stiffness"]       = kind.stiffness;
    out["mainBendMetres"]  = kind.mainBendMetres;
    out["branchAmp"]       = kind.branchAmp;
    out["edgeAmp"]         = kind.edgeAmp;
    out["flutterFreq"]     = kind.flutterFreq;
    out["detailFadeStart"] = kind.detailFadeStart;
    out["detailFadeEnd"]   = kind.detailFadeEnd;
    out["bendFadeStart"]   = kind.bendFadeStart;
    out["bendFadeEnd"]     = kind.bendFadeEnd;
    out["maxSwayMetres"]   = kind.maxSwayMetres;
    return out;
}

void readWindKind(const json& object, Terrain::TreeWindParams& kind, const Terrain::TreeWindParams& fallback, const char* where)
{
    if (!object.is_object())
        return;
    jsonFloatOr(object, "influence", kind.influence, fallback.influence, where);
    jsonFloatOr(object, "stiffness", kind.stiffness, fallback.stiffness, where);
    jsonFloatOr(object, "mainBendMetres", kind.mainBendMetres, fallback.mainBendMetres, where);
    jsonFloatOr(object, "branchAmp", kind.branchAmp, fallback.branchAmp, where);
    jsonFloatOr(object, "edgeAmp", kind.edgeAmp, fallback.edgeAmp, where);
    jsonFloatOr(object, "flutterFreq", kind.flutterFreq, fallback.flutterFreq, where);
    jsonFloatOr(object, "detailFadeStart", kind.detailFadeStart, fallback.detailFadeStart, where);
    jsonFloatOr(object, "detailFadeEnd", kind.detailFadeEnd, fallback.detailFadeEnd, where);
    jsonFloatOr(object, "bendFadeStart", kind.bendFadeStart, fallback.bendFadeStart, where);
    jsonFloatOr(object, "bendFadeEnd", kind.bendFadeEnd, fallback.bendFadeEnd, where);
    jsonFloatOr(object, "maxSwayMetres", kind.maxSwayMetres, fallback.maxSwayMetres, where);
}

bool jsonElemFloat(const json& value, float& out)
{
    if (const auto* number = value.get_ptr<const json::number_float_t*>())
    {
        if (!std::isfinite(static_cast<double>(*number)))
            return false;
        out = static_cast<float>(*number);
        return true;
    }
    if (const auto* number = value.get_ptr<const json::number_integer_t*>())
    {
        out = static_cast<float>(*number);
        return true;
    }
    if (const auto* number = value.get_ptr<const json::number_unsigned_t*>())
    {
        out = static_cast<float>(*number);
        return true;
    }
    return false;
}

float jsonAt(const json& value)
{
    float out = 0.0f;
    jsonElemFloat(value, out);
    return out;
}

int jsonAtInt(const json& value)
{
    if (const auto* number = value.get_ptr<const json::number_integer_t*>())
        return static_cast<int>(*number);
    if (const auto* number = value.get_ptr<const json::number_unsigned_t*>())
        return static_cast<int>(*number);
    float out = 0.0f;
    if (jsonElemFloat(value, out))
        return static_cast<int>(out);
    return 0;
}

bool jsonAtBool(const json& value)
{
    if (const auto* flag = value.get_ptr<const json::boolean_t*>())
        return *flag;
    return false;
}

std::string jsonAtString(const json& value)
{
    if (const auto* text = value.get_ptr<const json::string_t*>())
        return *text;
    return {};
}

float jsonF(const json& object, const char* key, float fallback)
{
    float out = fallback;
    jsonFloatOr(object, key, out, fallback, "SceneFile");
    return out;
}

int jsonI(const json& object, const char* key, int fallback)
{
    int out = fallback;
    jsonIntOr(object, key, out, fallback, "SceneFile");
    return out;
}

bool jsonB(const json& object, const char* key, bool fallback)
{
    bool out = fallback;
    jsonBoolOr(object, key, out, fallback, "SceneFile");
    return out;
}

std::string jsonS(const json& object, const char* key, const std::string& fallback)
{
    std::string out;
    jsonStringOr(object, key, out, fallback, "SceneFile");
    return out;
}

uint32_t jsonU(const json& object, const char* key, uint32_t fallback)
{
    double number = static_cast<double>(fallback);
    if (!jsonDoubleOr(object, key, number, number, "SceneFile"))
        return fallback;
    if (!(number >= 0.0) || number > 4294967295.0)
        return fallback;
    return static_cast<uint32_t>(number);
}

const json& jsonObjectField(const json& object, const char* key, const json& empty)
{
    if (object.contains(key) && object[key].is_object())
        return object[key];
    return empty;
}

void copyWindFieldToGrass(const Terrain::WindSettings& wind, GrassSceneDesc& grass)
{
    grass.windBaseYaw      = wind.headingRad;
    grass.windDeflection   = wind.deflection;
    grass.windSpatialFreq  = wind.spatialFreq;
    grass.windTemporalFreq = wind.temporalFreq;
    grass.seed             = wind.seed;
}

} // namespace

bool saveSceneToJson(const std::filesystem::path& path, const SceneFileData& scene, std::string* errorOut)
{
    json root;
    root["version"] = scene.version > 0 ? scene.version : 1;
    root["name"]    = scene.name.empty() ? "untitled" : scene.name;
    if (!scene.quest.empty())
        root["quest"] = scene.quest;
    if (!scene.buildings.empty())
    {
        json list = json::array();
        for (const SceneBuildingDesc& b : scene.buildings)
            list.push_back(json{ { "id", b.id }, { "position", json::array({ b.position.x, b.position.y, b.position.z }) }, { "yaw", b.yaw } });
        root["buildings"] = std::move(list);
    }
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
            if (!scene.terrain.source.empty())
            {
                t["source"]       = scene.terrain.source;
                t["worldSize"]    = scene.terrain.worldSize;
                t["importHeight"] = scene.terrain.importHeight;
            }
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
            json f;
            f["version"]           = 1;
            f["seed"]              = scene.terrain.foliage.seed;
            f["dirtTreesPerM2"]    = scene.terrain.foliage.dirtTreesPerM2;
            f["dirtFlowersPerM2"]  = scene.terrain.foliage.dirtFlowersPerM2;
            f["grassTreesPerM2"]   = scene.terrain.foliage.grassTreesPerM2;
            f["grassFlowersPerM2"] = scene.terrain.foliage.grassFlowersPerM2;
            f["rockPerM2"]         = scene.terrain.foliage.rockPerM2;
            f["grassPerM2"]        = scene.terrain.foliage.grassPerM2;
            f["stale"]             = scene.terrain.foliage.stale;
            f["shedScale"] = clampShedScale(scene.terrain.foliage.shedScale);
            f["treeModel"]         = scene.terrain.foliage.treeModel;
            f["flowerModel"]       = scene.terrain.foliage.flowerModel;
            f["rockModel"]         = scene.terrain.foliage.rockModel;
            f["grassModel"]        = scene.terrain.foliage.grassModel;
            t["foliage"]           = std::move(f);
            GrassSceneDesc grass = scene.terrain.grass;
            clampGrassScene(grass);
            json grassJson;
            grassJson["enabled"]          = grass.enabled;
            grassJson["height"]           = grass.heightMetres;
            grassJson["flexibility"]      = grass.flexibility;
            grassJson["densityScale"]     = grass.densityScale;
            grassJson["windBaseYaw"]      = grass.windBaseYaw;
            grassJson["windDeflection"]   = grass.windDeflection;
            grassJson["windTipMetres"]    = grass.windTipMetres;
            grassJson["windSpatialFreq"]  = grass.windSpatialFreq;
            grassJson["windTemporalFreq"] = grass.windTemporalFreq;
            grassJson["faceEdge"]         = grass.faceEdge;
            grassJson["faceYaw"]          = grass.faceYaw;
            grassJson["faceLean"]         = grass.faceLean;
            grassJson["faceSteep"]        = grass.faceSteep;
            grassJson["seed"]             = grass.seed;
            t["grass"] = std::move(grassJson);
            if (scene.terrain.wind.present)
            {
                Terrain::WindSettings wind = scene.terrain.wind.settings;
                Terrain::clampWindSettings(wind);
                json windJson;
                windJson["strength"]             = wind.strength;
                windJson["heading"]              = wind.headingRad;
                windJson["deflection"]           = wind.deflection;
                windJson["spatialFreq"]          = wind.spatialFreq;
                windJson["temporalFreq"]         = wind.temporalFreq;
                windJson["gustStrength"]         = wind.gustStrength;
                windJson["gustFreq"]             = wind.gustFreq;
                windJson["grassFollowsStrength"] = wind.grassFollowsStrength;
                windJson["seed"]                 = wind.seed;
                json kinds;
                kinds["tree"]      = windKindToJson(wind.kind[static_cast<int>(Terrain::WindKind::Tree)]);
                kinds["flower"]    = windKindToJson(wind.kind[static_cast<int>(Terrain::WindKind::Flower)]);
                kinds["rock"]      = windKindToJson(wind.kind[static_cast<int>(Terrain::WindKind::Rock)]);
                kinds["grassTuft"] = windKindToJson(wind.kind[static_cast<int>(Terrain::WindKind::GrassTuft)]);
                windJson["kinds"]  = std::move(kinds);
                t["wind"]          = std::move(windJson);

                json mirrored = t["grass"];
                mirrored["windBaseYaw"]      = wind.headingRad;
                mirrored["windDeflection"]   = wind.deflection;
                mirrored["windSpatialFreq"]  = wind.spatialFreq;
                mirrored["windTemporalFreq"] = wind.temporalFreq;
                mirrored["seed"]             = wind.seed;
                t["grass"] = std::move(mirrored);
            }
            root["terrain"] = std::move(t);
        }
        if (scene.sky.present)
            root["sky"] = skyToJson(scene.sky);
        if (scene.fog.present)
            root["fog"] = fogToJson(scene.fog);
        if (scene.water.present)
            root["water"] = waterToJson(scene.water);
        if (scene.exposure.present)
            root["exposure"] = exposureToJson(scene.exposure);
        if (scene.stealth.present)
            root["stealth"] = stealthToJson(scene.stealth);
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
        if (!o.modelPath.empty())
            jo["model"] = o.modelPath;

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
            if (!isGlobalLightType(o.type))
                light["castShadow"] = o.lightCastShadow;
            if (o.type == SceneObjectType::SpotLight && o.searchlight)
            {
                light["searchlight"] = true;
                light["sweep"]       = o.sweepDeg;
            }
            jo["light"]           = std::move(light);
        }
        if (o.hasCloud || o.type == SceneObjectType::CloudVolume)
        {
            json cloud;
            cloud["shape"]           = (o.cloudShape == 0) ? "box" : "ellipsoid";
            cloud["density"]         = o.cloudDensity;
            cloud["coverage"]        = o.cloudCoverage;
            cloud["softness"]        = o.cloudSoftness;
            cloud["absorption"]      = o.cloudAbsorption;
            cloud["scattering"]      = o.cloudScattering;
            cloud["anisotropy"]      = o.cloudAnisotropy;
            cloud["noiseScale"]      = o.cloudNoiseScale;
            cloud["detailScale"]     = o.cloudDetailScale;
            cloud["detailStrength"]  = o.cloudDetailStrength;
            cloud["heightFalloff"]   = o.cloudHeightFalloff;
            cloud["silverLining"]    = o.cloudSilverLining;
            cloud["windSpeed"]       = o.cloudWindSpeed;
            cloud["albedo"]          = json::array({ o.cloudAlbedo[0], o.cloudAlbedo[1], o.cloudAlbedo[2] });
            cloud["windDir"]         = json::array({ o.cloudWindDir[0], o.cloudWindDir[1], o.cloudWindDir[2] });
            cloud["enabled"]         = o.cloudEnabled;
            jo["cloud"]              = std::move(cloud);
        }
        if (o.hasStream || o.type == SceneObjectType::Stream)
        {
            json stream;
            stream["width"]     = o.streamWidth;
            stream["flowSpeed"] = o.streamFlowSpeed;
            json points = json::array();
            for (const Math::Vector2f& p : o.streamPoints)
                points.push_back(json::array({ p.x, p.y }));
            stream["points"] = std::move(points);
            jo["stream"]     = std::move(stream);
        }
        if (o.hasSpawner || o.type == SceneObjectType::Spawner)
        {
            json spawner;
            spawner["mode"]          = toString(o.spawner.mode);
            spawner["unit"]          = toString(o.spawner.unit);
            spawner["gate"]          = toString(o.spawner.gate);
            spawner["count"]         = o.spawner.count;
            spawner["radius"]        = o.spawner.radius;
            spawner["respawnDelay"]  = o.spawner.respawnDelay;
            spawner["waveInterval"]  = o.spawner.waveInterval;
            spawner["maxWaves"]      = o.spawner.maxWaves;
            spawner["enabled"]       = o.spawner.enabled;
            spawner["seed"]          = o.spawner.seed;
            jo["spawner"]            = std::move(spawner);
        }

        if (o.conceal)
            jo["conceal"] = true;
        if (o.goal)
            jo["goal"] = true;
        if (!o.circuit.empty())
            jo["circuit"] = o.circuit;
        if (!o.zone.empty())
            jo["zone"] = o.zone;
        if (!o.pickup.empty())
        {
            jo["pickup"] = o.pickup;
            if (o.pickupCount != 1)
                jo["pickupCount"] = o.pickupCount;
        }
        if (!o.dialogue.empty())
            jo["dialogue"] = o.dialogue;
        if (!o.movie.empty())
            jo["movie"] = o.movie;
        if (o.movieRadius != 2.0f)
            jo["movieRadius"] = o.movieRadius;
        if (o.movieLoop)
            jo["movieLoop"] = true;
        if (!o.squad.empty())
            jo["squad"] = o.squad;
        if (!o.block)
            jo["block"] = false;
        if (!o.patrol.empty())
        {
            json points = json::array();
            for (const Math::Vector2f& p : o.patrol)
                points.push_back(json::array({ p.x, p.y }));
            jo["patrol"] = std::move(points);
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

    outScene.version = jsonI(root, "version", 1);
    outScene.name    = jsonS(root, "name", std::string("untitled"));
    jsonStringOr(root, "quest", outScene.quest, std::string(), "SceneFile");
    if (root.contains("buildings") && root["buildings"].is_array())
    {
        for (const json& jb : root["buildings"])
        {
            SceneBuildingDesc b;
            if (!jb.is_object() || !jsonStringOr(jb, "id", b.id, std::string(), "SceneFile") || b.id.empty())
                continue;
            jsonFloatOr(jb, "yaw", b.yaw, 0.0f, "SceneFile");
            if (jb.contains("position"))
                readVec3(jb["position"], b.position, nullptr, "buildings.position");
            outScene.buildings.push_back(std::move(b));
        }
    }
    const std::string modeStr = jsonS(root, "mode", std::string("3d"));
    if (!tryParseSceneMode(modeStr, outScene.mode))
        outScene.mode = SceneMode::Scene3D;
    if (outScene.mode != SceneMode::Scene2D)
    {
        outScene.environment     = jsonS(root, "environment", std::string("env/studio_gradient.hdr"));
        outScene.iblIntensity    = jsonF(root, "iblIntensity", 1.0f);
        outScene.iblRotationRadY = jsonF(root, "iblRotationRadY", 0.0f);
        if (root.contains("terrain") && root["terrain"].is_object())
        {
            const json& t = root["terrain"];
            const int bindLayout = jsonI(t, "bindLayout", TerrainSceneDesc::kBindLayoutV1);
            if (bindLayout != TerrainSceneDesc::kBindLayoutV1)
            {
                DE_LOG_ERROR("SceneFile: terrain bindLayout {} unknown", bindLayout);
            }
            else
            {
                TerrainSceneDesc desc{};
                desc.bindLayout     = bindLayout;
                desc.chunkCells     = jsonI(t, "chunkCells", 16);
                desc.heightBlendK   = jsonF(t, "heightBlendK", 0.5f);
                desc.heightBlendT   = jsonF(t, "heightBlendT", 0.1f);
                desc.triplanarSlope = jsonF(t, "triplanarSlope", 0.45f);
                desc.heightFile     = jsonS(t, "heightFile", std::string());
                desc.splatFile      = jsonS(t, "splatFile", std::string());
                desc.source         = jsonS(t, "source", std::string());
                desc.worldSize      = jsonF(t, "worldSize", 1024.0f);
                desc.importHeight   = jsonF(t, "importHeight", 480.0f);
                if (t.contains("grid") && t["grid"].is_object())
                {
                    const json& g = t["grid"];
                    TerrainGridSceneDesc grid{};
                    grid.tilesX       = jsonU(g, "tilesX", 4u);
                    grid.tilesZ       = jsonU(g, "tilesZ", 4u);
                    grid.tileCells    = jsonU(g, "tileCells", 512u);
                    grid.cellSize     = jsonF(g, "cellSize", 1.0f);
                    grid.heightScale  = jsonF(g, "heightScale", 80.0f);
                    grid.seed         = jsonU(g, "seed", 1337u);
                    grid.coarseFile   = jsonS(g, "coarseFile", std::string());
                    grid.tileDir      = jsonS(g, "tileDir", std::string());
                    grid.seaLevel     = jsonF(g, "seaLevel", 0.0f);
                    grid.residentRing = jsonI(g, "residentRing", 5);
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
                if (t.contains("foliage") && t["foliage"].is_object())
                {
                    const json& f = t["foliage"];
                    desc.foliage.present            = true;
                    desc.foliage.seed               = jsonU(f, "seed", 1337u);
                    desc.foliage.dirtTreesPerM2     = Math::Clamp(jsonF(f, "dirtTreesPerM2", 0.002f), 0.0f, 1.0f);
                    desc.foliage.dirtFlowersPerM2   = Math::Clamp(jsonF(f, "dirtFlowersPerM2", 0.006f), 0.0f, 2.0f);
                    desc.foliage.grassTreesPerM2    = Math::Clamp(jsonF(f, "grassTreesPerM2", 0.003f), 0.0f, 1.0f);
                    desc.foliage.grassFlowersPerM2  = Math::Clamp(jsonF(f, "grassFlowersPerM2", 0.008f), 0.0f, 2.0f);
                    desc.foliage.rockPerM2          = Math::Clamp(jsonF(f, "rockPerM2", 0.004f), 0.0f, 1.0f);
                    desc.foliage.grassPerM2         = Math::Clamp(jsonF(f, "grassPerM2", 0.0f), 0.0f, 1.0f);
                    desc.foliage.stale              = jsonB(f, "stale", false);
                    {
                        float shed = 1.0f;
                        jsonFloatOr(f, "shedScale", shed, 1.0f, "SceneFile");
                        desc.foliage.shedScale = clampShedScale(shed);
                    }
                    desc.foliage.treeModel          = jsonS(f, "treeModel", std::string("models/BirchTree/scene.gltf"));
                    desc.foliage.flowerModel        = jsonS(f, "flowerModel", std::string("models/Dandelion/dandelion_01_2k.gltf"));
                    desc.foliage.rockModel          = jsonS(f, "rockModel", std::string("models/RockMoss/rock_moss_set_01_2k.gltf"));
                    desc.foliage.grassModel         = jsonS(f, "grassModel", std::string("models/Grass/grass_medium_01_2k.gltf"));
                }
                if (t.contains("grass") && t["grass"].is_object())
                {
                    const json& g = t["grass"];
                    const GrassSceneDesc fallback{};
                    desc.grass.enabled          = jsonB(g, "enabled", fallback.enabled);
                    desc.grass.heightMetres     = jsonF(g, "height", fallback.heightMetres);
                    desc.grass.flexibility      = jsonF(g, "flexibility", fallback.flexibility);
                    desc.grass.densityScale     = jsonF(g, "densityScale", fallback.densityScale);
                    desc.grass.windBaseYaw      = jsonF(g, "windBaseYaw", fallback.windBaseYaw);
                    desc.grass.windDeflection   = jsonF(g, "windDeflection", fallback.windDeflection);
                    desc.grass.windTipMetres    = jsonF(g, "windTipMetres", fallback.windTipMetres);
                    desc.grass.windSpatialFreq  = jsonF(g, "windSpatialFreq", fallback.windSpatialFreq);
                    desc.grass.windTemporalFreq = jsonF(g, "windTemporalFreq", fallback.windTemporalFreq);
                    desc.grass.faceEdge         = jsonF(g, "faceEdge", fallback.faceEdge);
                    desc.grass.faceYaw          = jsonF(g, "faceYaw", fallback.faceYaw);
                    desc.grass.faceLean         = jsonF(g, "faceLean", fallback.faceLean);
                    desc.grass.faceSteep        = jsonF(g, "faceSteep", fallback.faceSteep);
                    desc.grass.seed             = jsonU(g, "seed", fallback.seed);
                    clampGrassScene(desc.grass);
                }
                if (t.contains("wind") && t["wind"].is_object())
                {
                    const json& w = t["wind"];
                    desc.wind.present = true;
                    Terrain::WindSettings& settings = desc.wind.settings;
                    const Terrain::WindSettings fallback = Terrain::defaultWindSettings();
                    settings = fallback;
                    jsonFloatOr(w, "strength", settings.strength, fallback.strength, "SceneFile");
                    jsonFloatOr(w, "heading", settings.headingRad, fallback.headingRad, "SceneFile");
                    jsonFloatOr(w, "deflection", settings.deflection, fallback.deflection, "SceneFile");
                    jsonFloatOr(w, "spatialFreq", settings.spatialFreq, fallback.spatialFreq, "SceneFile");
                    jsonFloatOr(w, "temporalFreq", settings.temporalFreq, fallback.temporalFreq, "SceneFile");
                    jsonFloatOr(w, "gustStrength", settings.gustStrength, fallback.gustStrength, "SceneFile");
                    jsonFloatOr(w, "gustFreq", settings.gustFreq, fallback.gustFreq, "SceneFile");
                    jsonBoolOr(w, "grassFollowsStrength", settings.grassFollowsStrength, fallback.grassFollowsStrength, "SceneFile");
                    int seedInt = static_cast<int>(fallback.seed);
                    if (jsonIntOr(w, "seed", seedInt, seedInt, "SceneFile") && seedInt >= 0)
                        settings.seed = static_cast<uint32_t>(seedInt);
                    if (w.contains("kinds") && w["kinds"].is_object())
                    {
                        const json& kinds = w["kinds"];
                        const json emptyKind = json::object();
                        readWindKind(jsonObjectField(kinds, "tree", emptyKind), settings.kind[static_cast<int>(Terrain::WindKind::Tree)],
                            fallback.kind[static_cast<int>(Terrain::WindKind::Tree)], "SceneFile");
                        readWindKind(jsonObjectField(kinds, "flower", emptyKind), settings.kind[static_cast<int>(Terrain::WindKind::Flower)],
                            fallback.kind[static_cast<int>(Terrain::WindKind::Flower)], "SceneFile");
                        readWindKind(jsonObjectField(kinds, "rock", emptyKind), settings.kind[static_cast<int>(Terrain::WindKind::Rock)],
                            fallback.kind[static_cast<int>(Terrain::WindKind::Rock)], "SceneFile");
                        readWindKind(jsonObjectField(kinds, "grassTuft", emptyKind), settings.kind[static_cast<int>(Terrain::WindKind::GrassTuft)],
                            fallback.kind[static_cast<int>(Terrain::WindKind::GrassTuft)], "SceneFile");
                    }
                    Terrain::clampWindSettings(settings);
                }
                else
                {
                    desc.wind.present           = false;
                    desc.wind.settings          = Terrain::defaultWindSettings();
                    desc.wind.settings.headingRad   = desc.grass.windBaseYaw;
                    desc.wind.settings.deflection   = desc.grass.windDeflection;
                    desc.wind.settings.spatialFreq  = desc.grass.windSpatialFreq;
                    desc.wind.settings.temporalFreq = desc.grass.windTemporalFreq;
                    desc.wind.settings.seed         = desc.grass.seed;
                    Terrain::clampWindSettings(desc.wind.settings);
                }
                copyWindFieldToGrass(desc.wind.settings, desc.grass);
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
                        desc.layers[i].albedo = jsonS(layer, "albedo", std::string());
                        desc.layers[i].normal = jsonS(layer, "normal", std::string());
                        desc.layers[i].orm    = jsonS(layer, "orm", std::string());
                        desc.layers[i].tiling = jsonF(layer, "tiling", 8.0f);
                        if (layer.contains("tint"))
                            readColor(layer["tint"], desc.layers[i].tint, nullptr, "terrain.layers.tint");
                    }
                }
                outScene.hasTerrain = true;
                outScene.terrain    = std::move(desc);
            }
        }
        if (root.contains("sky") && root["sky"].is_object())
        {
            const json& s = root["sky"];
            SkySceneDesc sky{};
            sky.present       = true;
            sky.weather       = jsonS(s, "weather", std::string("partly"));
            const Sky::WeatherState preset = weatherPreset(sky.weather);
            sky.timeOfDay     = jsonF(s, "timeOfDay", 16.2f);
            sky.dayOfYear     = jsonF(s, "dayOfYear", 172.0f);
            sky.latitude      = jsonF(s, "latitude", 47.6f);
            sky.timeScale     = jsonF(s, "timeScale", 0.0f);
            sky.cloudCoverage = jsonF(s, "cloudCoverage", preset.cloudCoverage);
            sky.turbidity     = jsonF(s, "turbidity", preset.turbidity);
            sky.windSpeed     = jsonF(s, "windSpeed", preset.windSpeed);
            sky.windDir[0]    = preset.windDir.x;
            sky.windDir[1]    = preset.windDir.y;
            sky.rain          = jsonF(s, "rain", preset.rain);
            if (s.contains("windDir") && s["windDir"].is_array() && s["windDir"].size() >= 2)
            {
                sky.windDir[0] = jsonAt(s["windDir"][0]);
                sky.windDir[1] = jsonAt(s["windDir"][1]);
            }

            Sky::CloudLayerDesc deck{};
            deck.altitude     = jsonF(s, "cloudAltitude", 2000.0f);
            deck.amount       = jsonF(s, "cloudAmount", 1.0f);
            deck.windSpeedMps = jsonF(s, "cloudWindSpeedMps", 8.0f);
            Sky::sanitizeCloudLayer(deck);
            sky.cloudAltitude     = deck.altitude;
            sky.cloudAmount       = deck.amount;
            sky.cloudWindSpeedMps = deck.windSpeedMps;

            outScene.sky = std::move(sky);
        }
        if (root.contains("fog") && root["fog"].is_object())
        {
            const json& f = root["fog"];
            FogSceneDesc fog{};
            fog.present         = true;
            fog.autoFromWeather = jsonB(f, "auto", true);
            fog.distanceScale   = jsonF(f, "distanceScale", 1.0f);
            fog.heightScale     = jsonF(f, "heightScale", 1.0f);
            fog.valleyScale     = jsonF(f, "valleyScale", 1.0f);
            fog.distanceDensity = jsonF(f, "distanceDensity", 0.004f);
            fog.heightDensity   = jsonF(f, "heightDensity", 0.0f);
            fog.valleyDensity   = jsonF(f, "valleyDensity", 0.012f);
            fog.heightFalloff   = jsonF(f, "heightFalloff", 0.06f);
            fog.valleyHeight    = jsonF(f, "valleyHeight", 14.0f);
            if (f.contains("color") && f["color"].is_array() && f["color"].size() >= 3)
            {
                fog.color[0] = jsonAt(f["color"][0]);
                fog.color[1] = jsonAt(f["color"][1]);
                fog.color[2] = jsonAt(f["color"][2]);
            }
            outScene.fog = std::move(fog);
        }
        if (root.contains("water") && root["water"].is_object())
        {
            const json& w = root["water"];
            WaterSceneDesc water{};
            water.present        = true;
            water.hasLevel       = w.contains("level") && w["level"].is_number();
            water.level          = jsonF(w, "level", 0.0f);
            water.levelFraction  = jsonF(w, "levelFraction", 0.38f);
            water.chunkCells     = jsonI(w, "chunkCells", 16);
            water.flowStrength   = jsonF(w, "flowStrength", 0.85f);
            water.steepness      = jsonF(w, "steepness", 0.55f);
            water.amplitudeScale = jsonF(w, "amplitudeScale", 1.0f);
            water.speedScale     = jsonF(w, "speedScale", 1.0f);
            water.flowSpeed      = jsonF(w, "flowSpeed", 0.4f);
            water.foam           = jsonF(w, "foam", 1.0f);
            water.foamWidthScale = Math::Clamp(jsonF(w, "foamWidthScale", 1.0f), 0.5f, 2.0f);
            water.detail         = Math::Clamp(jsonF(w, "detail", 1.0f), 0.0f, 1.0f);
            if (w.contains("flowDir") && w["flowDir"].is_array() && w["flowDir"].size() >= 2)
            {
                water.flowDir[0] = jsonAt(w["flowDir"][0]);
                water.flowDir[1] = jsonAt(w["flowDir"][1]);
            }
            if (w.contains("lodDistances") && w["lodDistances"].is_array())
            {
                const json& lod = w["lodDistances"];
                const int n = lod.size() > 8 ? 8 : static_cast<int>(lod.size());
                water.lodDistanceCount = n > 0 ? n : 1;
                for (int i = 0; i < n; ++i)
                {
                    if (lod[i].is_number())
                        water.lodDistances[i] = jsonAt(lod[i]);
                }
            }
            if (w.contains("waves") && w["waves"].is_array())
            {
                const json& waves = w["waves"];
                const int n = waves.size() > 4 ? 4 : static_cast<int>(waves.size());
                water.waveCount = n;
                for (int i = 0; i < n; ++i)
                {
                    if (!waves[i].is_object())
                        continue;
                    const json& wave = waves[i];
                    water.waves[i].angleFromFlow = jsonF(wave, "angle", 0.0f);
                    water.waves[i].frequency     = jsonF(wave, "frequency", 1.0f);
                    water.waves[i].amplitude     = jsonF(wave, "amplitude", 0.1f);
                    water.waves[i].speed         = jsonF(wave, "speed", 1.0f);
                }
            }
            outScene.water = std::move(water);
        }
        if (root.contains("exposure") && root["exposure"].is_object())
        {
            const json& e = root["exposure"];
            ExposureSceneDesc exposure{};
            exposure.present      = true;
            exposure.autoExposure = jsonB(e, "auto", false);
            exposure.evBias       = jsonF(e, "evBias", 0.0f);
            exposure.maxEv        = jsonF(e, "maxEv", 1.5f);
            exposure.adaptBright  = jsonF(e, "adaptBright", 6.0f);
            exposure.adaptDark    = jsonF(e, "adaptDark", 1.0f);
            outScene.exposure     = std::move(exposure);
        }
        if (root.contains("stealth"))
        {
            const json& s = root["stealth"];
            if (s.is_boolean())
            {
                if (jsonAtBool(s))
                {
                    StealthSceneDesc stealth{};
                    stealth.present   = true;
                    stealth.awareness = true;
                    outScene.stealth  = stealth;
                }
            }
            else if (s.is_object())
            {
                StealthSceneDesc stealth{};
                stealth.present   = true;
                stealth.awareness = s.contains("awareness") && s["awareness"].is_boolean() && jsonAtBool(s["awareness"]);
                readStealthHear(s, "hearStand", stealth.hasHearStand, stealth.hearStand);
                readStealthHear(s, "hearWalk", stealth.hasHearWalk, stealth.hearWalk);
                readStealthHear(s, "hearSprint", stealth.hasHearSprint, stealth.hearSprint);
                readStealthHear(s, "hearCrouchWalk", stealth.hasHearCrouchWalk, stealth.hearCrouchWalk);
                readStealthHear(s, "hearCrouchStill", stealth.hasHearCrouchStill, stealth.hearCrouchStill);
                if (s.contains("settings") && s["settings"].is_object())
                {
                    AI::AwarenessSettings settings{};
                    uint64_t mask = 0;
                    if (AI::readAwarenessSettings(s["settings"].dump(), settings, &mask))
                    {
                        stealth.hasSettings  = true;
                        stealth.settings     = settings;
                        stealth.settingsMask = mask;
                    }
                }
                outScene.stealth = stealth;
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

    int streamCount = 0;
    bool streamCapWarned = false;
    for (const json& jo : root["objects"])
    {
        if (!jo.is_object())
            continue;

        SceneObjectData o{};
        const std::string typeStr = jsonS(jo, "type", std::string("cube"));
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
            o.particleName    = jsonS(p, "name", std::string("Emitter"));
            o.maxParticles    = jsonU(p, "maxParticles", 512u);
            o.emissionRate    = jsonF(p, "emissionRate", 40.0f);
            o.duration        = jsonF(p, "duration", 0.0f);
            o.looping         = jsonB(p, "looping", true);
            o.spreadDegrees   = jsonF(p, "spreadDegrees", 25.0f);
            o.shape           = jsonI(p, "shape", 0);
            o.additiveBlend   = jsonB(p, "additiveBlend", true);
            o.simulationSpeed = jsonF(p, "simulationSpeed", 1.0f);
            o.renderMode      = jsonI(p, "renderMode", 0);
            o.ribbonCount     = jsonU(p, "ribbonCount", 1u);
            o.ribbonUvScale   = jsonF(p, "ribbonUvScale", 1.0f);
            if (p.contains("lifetime") && p["lifetime"].is_array() && p["lifetime"].size() >= 2)
            {
                o.lifetimeMin = jsonAt(p["lifetime"][0]);
                o.lifetimeMax = jsonAt(p["lifetime"][1]);
            }
            if (p.contains("startSpeed") && p["startSpeed"].is_array() && p["startSpeed"].size() >= 2)
            {
                o.startSpeedMin = jsonAt(p["startSpeed"][0]);
                o.startSpeedMax = jsonAt(p["startSpeed"][1]);
            }
            if (p.contains("startSize") && p["startSize"].is_array() && p["startSize"].size() >= 2)
            {
                o.startSizeMin = jsonAt(p["startSize"][0]);
                o.startSizeMax = jsonAt(p["startSize"][1]);
            }
            if (p.contains("endSize") && p["endSize"].is_array() && p["endSize"].size() >= 2)
            {
                o.endSizeMin = jsonAt(p["endSize"][0]);
                o.endSizeMax = jsonAt(p["endSize"][1]);
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
            o.lightIntensity    = jsonF(light, "intensity", o.lightIntensity);
            o.lightRange        = jsonF(light, "range", o.lightRange);
            o.lightInnerDeg     = jsonF(light, "inner", o.lightInnerDeg);
            o.lightOuterDeg     = jsonF(light, "outer", o.lightOuterDeg);
            o.lightSourceRadius = jsonF(light, "sourceRadius", o.lightSourceRadius);
            o.lightEnabled      = jsonB(light, "enabled", o.lightEnabled);
            // A missing or mistyped key stays false. get<bool>() is not safe on the IO thread.
            if (!isGlobalLightType(o.type) && light.contains("castShadow") && light["castShadow"].is_boolean())
                o.lightCastShadow = jsonAtBool(light["castShadow"]);
            if (o.type == SceneObjectType::SpotLight && light.contains("searchlight") && light["searchlight"].is_boolean())
                o.searchlight = jsonAtBool(light["searchlight"]);
            if (light.contains("sweep") && light["sweep"].is_number())
                o.sweepDeg = std::clamp(jsonAt(light["sweep"]), 0.0f, 340.0f);
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
            o.emissive = jsonAt(jo["emissive"]);
        if (jo.contains("emissiveMesh") && jo["emissiveMesh"].is_number_integer())
            o.emissiveMeshIndex = jsonAtInt(jo["emissiveMesh"]);
        if (jo.contains("model") && jo["model"].is_string())
            o.modelPath = jsonAtString(jo["model"]);

        if (jo.contains("cloud") && jo["cloud"].is_object())
        {
            const json& cloud = jo["cloud"];
            o.hasCloud = true;
            if (cloud.contains("shape"))
            {
                if (cloud["shape"].is_string())
                {
                    const std::string s = jsonAtString(cloud["shape"]);
                    if (s == "box" || s == "aabb")
                        o.cloudShape = 0;
                    else
                        o.cloudShape = 1;
                }
                else if (cloud["shape"].is_number_integer())
                    o.cloudShape = jsonAtInt(cloud["shape"]) == 0 ? 0 : 1;
            }
            o.cloudDensity         = jsonF(cloud, "density", o.cloudDensity);
            o.cloudCoverage        = jsonF(cloud, "coverage", o.cloudCoverage);
            o.cloudSoftness        = jsonF(cloud, "softness", o.cloudSoftness);
            o.cloudAbsorption      = jsonF(cloud, "absorption", o.cloudAbsorption);
            o.cloudScattering      = jsonF(cloud, "scattering", o.cloudScattering);
            o.cloudAnisotropy      = jsonF(cloud, "anisotropy", o.cloudAnisotropy);
            o.cloudNoiseScale      = jsonF(cloud, "noiseScale", o.cloudNoiseScale);
            o.cloudDetailScale     = jsonF(cloud, "detailScale", o.cloudDetailScale);
            o.cloudDetailStrength  = jsonF(cloud, "detailStrength", o.cloudDetailStrength);
            o.cloudHeightFalloff   = jsonF(cloud, "heightFalloff", o.cloudHeightFalloff);
            o.cloudSilverLining    = jsonF(cloud, "silverLining", o.cloudSilverLining);
            o.cloudWindSpeed       = jsonF(cloud, "windSpeed", o.cloudWindSpeed);
            o.cloudEnabled         = jsonB(cloud, "enabled", o.cloudEnabled);
            if (cloud.contains("albedo") && cloud["albedo"].is_array() && cloud["albedo"].size() >= 3)
            {
                o.cloudAlbedo[0] = jsonAt(cloud["albedo"][0]);
                o.cloudAlbedo[1] = jsonAt(cloud["albedo"][1]);
                o.cloudAlbedo[2] = jsonAt(cloud["albedo"][2]);
            }
            if (cloud.contains("windDir") && cloud["windDir"].is_array() && cloud["windDir"].size() >= 3)
            {
                o.cloudWindDir[0] = jsonAt(cloud["windDir"][0]);
                o.cloudWindDir[1] = jsonAt(cloud["windDir"][1]);
                o.cloudWindDir[2] = jsonAt(cloud["windDir"][2]);
            }
        }
        else if (o.type == SceneObjectType::CloudVolume)
        {
            o.hasCloud = true;
        }

        jsonBoolOr(jo, "conceal", o.conceal, false, "SceneFile");
        jsonBoolOr(jo, "goal", o.goal, false, "SceneFile");
        jsonStringOr(jo, "circuit", o.circuit, std::string(), "SceneFile");
        jsonStringOr(jo, "zone", o.zone, std::string(), "SceneFile");
        jsonStringOr(jo, "squad", o.squad, std::string(), "SceneFile");
        jsonBoolOr(jo, "block", o.block, true, "SceneFile");
        jsonStringOr(jo, "pickup", o.pickup, std::string(), "SceneFile");
        jsonIntOr(jo, "pickupCount", o.pickupCount, 1, "SceneFile");
        jsonStringOr(jo, "dialogue", o.dialogue, std::string(), "SceneFile");
        jsonStringOr(jo, "movie", o.movie, std::string(), "SceneFile");
        jsonFloatOr(jo, "movieRadius", o.movieRadius, 2.0f, "SceneFile");
        if (!(o.movieRadius >= 0.0f))
            o.movieRadius = 0.0f;
        jsonBoolOr(jo, "movieLoop", o.movieLoop, false, "SceneFile");
        if (jo.contains("patrol") && jo["patrol"].is_array())
        {
            for (const json& p : jo["patrol"])
            {
                if (p.is_array() && p.size() >= 2 && p[0].is_number() && p[1].is_number())
                    o.patrol.emplace_back(jsonAt(p[0]), jsonAt(p[1]));
            }
        }

        if (o.type == SceneObjectType::Stream)
        {
            if (streamCount >= 16)
            {
                if (!streamCapWarned)
                {
                    DE_LOG_WARN(LogCategory::Render, "SceneFile: more than 16 streams; extra streams skipped");
                    streamCapWarned = true;
                }
                continue;
            }
            if (!jo.contains("stream") || !jo["stream"].is_object())
            {
                DE_LOG_ERROR(LogCategory::Render, "SceneFile: stream missing stream block — skipped");
                continue;
            }
            const json& stream = jo["stream"];
            if (!stream.contains("points") || !stream["points"].is_array())
            {
                DE_LOG_ERROR(LogCategory::Render, "SceneFile: stream missing points — skipped");
                continue;
            }
            const json& pts = stream["points"];
            if (pts.size() < 2 || pts.size() > 256)
            {
                DE_LOG_ERROR(LogCategory::Render, "SceneFile: stream point count {} is out of range — skipped", pts.size());
                continue;
            }
            bool pointsOk = true;
            o.streamPoints.clear();
            o.streamPoints.reserve(pts.size());
            for (const json& p : pts)
            {
                if (!p.is_array() || p.size() < 2 || !p[0].is_number() || !p[1].is_number())
                {
                    pointsOk = false;
                    break;
                }
                o.streamPoints.emplace_back(jsonAt(p[0]), jsonAt(p[1]));
            }
            if (!pointsOk || o.streamPoints.size() < 2)
            {
                DE_LOG_ERROR(LogCategory::Render, "SceneFile: stream points are not [x,z] pairs — skipped");
                continue;
            }
            o.hasStream        = true;
            o.streamWidth      = Math::Clamp(jsonF(stream, "width", 3.5f), 1.5f, 12.0f);
            o.streamFlowSpeed  = jsonF(stream, "flowSpeed", 1.6f);
            o.position.x       = o.streamPoints.front().x;
            o.position.z       = o.streamPoints.front().y;
            ++streamCount;
        }

        if (o.type == SceneObjectType::Spawner || (jo.contains("spawner") && jo["spawner"].is_object()))
        {
            o.hasSpawner = true;
            if (jo.contains("spawner") && jo["spawner"].is_object())
                readSpawnerBlock(jo["spawner"], o.spawner);
            else
                clampSpawnerDesc(o.spawner);
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

    const fs::path authoring = authoringContentRoot();
    if (!authoring.empty())
    {
        const fs::path  scene = authoring / "scenes" / name;
        std::error_code ec;
        if (fs::exists(scene, ec) && !ec)
        {
            const fs::path canonical = fs::weakly_canonical(scene, ec);
            return ec ? scene : canonical;
        }
        ec.clear();
        if (fs::is_directory(authoring, ec) && !ec)
            return scene;
    }

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

void applySceneAtmosphere(Sky::Environment& env, const SceneFileData& scene)
{
    if (scene.sky.present)
    {
        const SkySceneDesc& sky = scene.sky;
        env.timeOfDay = sky.timeOfDay;
        env.dayOfYear = sky.dayOfYear;
        env.latitude  = sky.latitude;
        env.timeScale = sky.timeScale;
        env.weather.cloudCoverage = sky.cloudCoverage;
        env.weather.turbidity     = sky.turbidity;
        env.weather.windSpeed     = sky.windSpeed;
        env.weather.windDir       = Math::Vector2f(sky.windDir[0], sky.windDir[1]);
        env.weather.rain          = sky.rain;

        env.cloudLayer.altitude     = sky.cloudAltitude;
        env.cloudLayer.amount       = sky.cloudAmount;
        env.cloudLayer.windSpeedMps = sky.cloudWindSpeedMps;
        env.resetCloudWind();
    }
    else
    {
        const Sky::Environment fresh{};
        env.timeOfDay  = fresh.timeOfDay;
        env.dayOfYear  = fresh.dayOfYear;
        env.latitude   = fresh.latitude;
        env.timeScale  = fresh.timeScale;
        env.weather    = fresh.weather;
        env.cloudLayer = fresh.cloudLayer;
        env.resetCloudWind();
    }

    if (scene.fog.present)
    {
        const FogSceneDesc& fog = scene.fog;
        env.fogAuto            = fog.autoFromWeather;
        env.fogDistanceScale   = fog.distanceScale;
        env.heightFogScale     = fog.heightScale;
        env.volumetricFogScale = fog.valleyScale;
        env.setHeightFogFalloff(fog.heightFalloff);
        env.setVolumetricFogHeight(fog.valleyHeight);
    }
    else
        env.resetFogTune();

    env.evaluate();
    if (scene.fog.present && !scene.fog.autoFromWeather)
    {
        const FogSceneDesc& fog = scene.fog;
        env.setFogDensity(fog.distanceDensity);
        env.setHeightFogDensity(fog.heightDensity);
        env.setVolumetricFogDensity(fog.valleyDensity);
        env.setFogColor(Math::Vector3f(fog.color[0], fog.color[1], fog.color[2]));
    }
}

void captureSceneAtmosphere(const Sky::Environment& env, SceneFileData& scene)
{
    SkySceneDesc sky{};
    sky.present       = true;
    sky.timeOfDay     = env.timeOfDay;
    sky.dayOfYear     = env.dayOfYear;
    sky.latitude      = env.latitude;
    sky.timeScale     = env.timeScale;
    sky.weather       = weatherNameFor(env.weather);
    sky.cloudCoverage = env.weather.cloudCoverage;
    sky.turbidity     = env.weather.turbidity;
    sky.windSpeed     = env.weather.windSpeed;
    sky.windDir[0]    = env.weather.windDir.x;
    sky.windDir[1]    = env.weather.windDir.y;
    sky.rain          = env.weather.rain;

    sky.cloudAltitude     = env.cloudLayer.altitude;
    sky.cloudAmount       = env.cloudLayer.amount;
    sky.cloudWindSpeedMps = env.cloudLayer.windSpeedMps;

    scene.sky = std::move(sky);

    FogSceneDesc fog{};
    fog.present         = true;
    fog.autoFromWeather = env.fogAuto;
    fog.distanceScale   = env.fogDistanceScale;
    fog.heightScale     = env.heightFogScale;
    fog.valleyScale     = env.volumetricFogScale;
    fog.distanceDensity = env.fogDensity();
    fog.heightDensity   = env.heightFogDensity();
    fog.valleyDensity   = env.volumetricFogDensity();
    fog.heightFalloff   = env.heightFogFalloff();
    fog.valleyHeight    = env.volumetricFogHeight();
    const Math::Vector3f color = env.fogColor();
    fog.color[0] = color.x;
    fog.color[1] = color.y;
    fog.color[2] = color.z;
    scene.fog = std::move(fog);
}

WaterParams sceneWaterParams(const WaterSceneDesc& water, float waterLevel)
{
    WaterParams params = defaultWaterParams(waterLevel);
    params.flowDir        = Math::Vector2f(water.flowDir[0], water.flowDir[1]);
    params.flowStrength   = water.flowStrength;
    params.steepness      = water.steepness;
    params.amplitudeScale = water.amplitudeScale;
    params.speedScale     = water.speedScale;
    params.flowSpeed      = water.flowSpeed;
    params.foam           = water.foam;
    params.foamWidthScale = water.foamWidthScale;
    params.detailAmount   = water.detail;
    const int n = water.waveCount > 4 ? 4 : water.waveCount;
    for (int i = 0; i < n; ++i)
    {
        params.waves[i].angleFromFlow = water.waves[i].angleFromFlow;
        params.waves[i].frequency     = water.waves[i].frequency;
        params.waves[i].amplitude     = water.waves[i].amplitude;
        params.waves[i].speed         = water.waves[i].speed;
    }
    // The old 28/14/7/3.5 m octave set is replaced in memory. Level, flow, and scales stay.
    if (n > 0 && isLegacyHarmonicPreset(params))
    {
        const WaterParams fresh = defaultWaterParams(waterLevel);
        for (int i = 0; i < kWaterWaveCount; ++i)
            params.waves[i] = fresh.waves[i];
        static bool logged = false;
        if (!logged)
        {
            logged = true;
            DE_LOG_INFO(LogCategory::Render, "Water: legacy harmonic preset replaced");
        }
    }
    return params;
}

namespace
{

struct SceneLoadRecord
{
    std::filesystem::path path;
    uint64_t              generation = 0;
    std::atomic<bool>     finished{false};
    std::atomic<bool>     superseded{false};
    JobStatus             status = JobStatus::Failed;
    SceneFileData         scene;
    std::string           error;
};

struct SceneLoadTable
{
    std::mutex mutex;
    uint64_t   current = 0;
    std::unordered_map<uint64_t, std::shared_ptr<SceneLoadRecord>> records;
};

SceneLoadTable& sceneLoads()
{
    static SceneLoadTable table;
    return table;
}

void sweepFinished(SceneLoadTable& table, uint64_t keep)
{
    for (auto it = table.records.begin(); it != table.records.end();)
    {
        if (it->first != keep && it->second && it->second->finished.load(std::memory_order_acquire))
            it = table.records.erase(it);
        else
            ++it;
    }
}

JobStatus runSceneLoad(void* user)
{
    auto* raw = static_cast<SceneLoadRecord*>(user);
    // The table drops its shared_ptr as soon as finished is set. This copy keeps the
    // record alive until the function returns, so the release store is not a free.
    std::shared_ptr<SceneLoadRecord> keep;
    {
        SceneLoadTable& table = sceneLoads();
        std::lock_guard<std::mutex> lock(table.mutex);
        for (const auto& entry : table.records)
        {
            if (entry.second && entry.second.get() == raw)
            {
                keep = entry.second;
                break;
            }
        }
    }
    if (!keep)
        return JobStatus::Failed;

    SceneLoadRecord* rec = keep.get();
    SceneFileData scene;
    std::string error;
    const bool ok = loadSceneFromJson(rec->path, scene, &error);
    JobStatus status = JobStatus::Failed;
    if (rec->superseded.load(std::memory_order_acquire))
        status = JobStatus::Cancelled;
    else if (ok)
    {
        status     = JobStatus::Ok;
        rec->scene = std::move(scene);
    }
    else
    {
        status     = JobStatus::Failed;
        rec->error = std::move(error);
        rec->scene = std::move(scene);
    }
    rec->status = status;
    rec->finished.store(true, std::memory_order_release);
    return status;
}

} // namespace

bool enqueueSceneLoad(JobSystem& jobs, const AssetManager& assets, const SceneLoadRequest& request)
{
    // Generation 0 is not a slot. Poll of an unknown id fails instead of succeeding.
    if (request.generation == 0 || request.path.empty())
        return false;
    if (!acceptJobPath(assets, request.path))
        return false;

    auto rec = std::make_shared<SceneLoadRecord>();
    rec->path       = request.path;
    rec->generation = request.generation;

    SceneLoadTable& table = sceneLoads();
    {
        std::lock_guard<std::mutex> lock(table.mutex);
        const auto existing = table.records.find(request.generation);
        // The worker still owns this generation. Do not replace the record.
        if (existing != table.records.end() && existing->second && !existing->second->finished.load(std::memory_order_acquire))
            return false;
        table.records[request.generation] = rec;
    }

    JobDesc desc{};
    desc.name       = "SceneLoad";
    desc.lane       = JobLane::Io;
    desc.stream     = JobStream::Scene;
    desc.reads      = AccessFileRead;
    desc.generation = request.generation;
    desc.fn         = &runSceneLoad;
    desc.user       = rec.get();
    if (!jobs.enqueue(desc))
    {
        std::lock_guard<std::mutex> lock(table.mutex);
        const auto it = table.records.find(request.generation);
        if (it != table.records.end() && it->second.get() == rec.get() && !rec->finished.load(std::memory_order_acquire))
            table.records.erase(it);
        return false;
    }

    std::lock_guard<std::mutex> lock(table.mutex);
    table.current = request.generation;
    for (auto& entry : table.records)
    {
        if (entry.first != request.generation && entry.second && !entry.second->finished.load(std::memory_order_acquire))
            entry.second->superseded.store(true, std::memory_order_release);
    }
    sweepFinished(table, request.generation);
    return true;
}

bool pollSceneLoad(JobSystem& jobs, uint64_t generation, SceneLoadResult& out)
{
    out = SceneLoadResult{};
    if (generation == 0)
    {
        out.status = JobStatus::Failed;
        out.error  = "unknown generation";
        return false;
    }

    SceneLoadTable& table = sceneLoads();
    std::shared_ptr<SceneLoadRecord> rec;
    {
        std::lock_guard<std::mutex> lock(table.mutex);
        sweepFinished(table, generation);
        const auto it = table.records.find(generation);
        if (it == table.records.end() || !it->second)
        {
            out.status = JobStatus::Failed;
            out.error  = "unknown generation";
            return false;
        }
        rec = it->second;
    }

    if (!rec->finished.load(std::memory_order_acquire))
    {
        out.status = JobStatus::NotReady;
        return false;
    }

    if (rec->status == JobStatus::Budget)
    {
        rec->finished.store(false, std::memory_order_release);
        JobDesc desc{};
        desc.name       = "SceneLoad";
        desc.lane       = JobLane::Io;
        desc.stream     = JobStream::Scene;
        desc.reads      = AccessFileRead;
        desc.generation = generation;
        desc.fn         = &runSceneLoad;
        desc.user       = rec.get();
        if (!jobs.enqueue(desc))
        {
            rec->status = JobStatus::Failed;
            rec->error  = "scheduler stopped";
            rec->finished.store(true, std::memory_order_release);
            out.status = JobStatus::Failed;
            out.error  = rec->error;
            return false;
        }
        out.status = JobStatus::NotReady;
        return false;
    }

    if (rec->status != JobStatus::Ok && rec->status != JobStatus::Failed && rec->status != JobStatus::Cancelled)
    {
        out.status = JobStatus::Failed;
        out.error  = "job returned NotReady";
        return false;
    }

    out.status = rec->status;
    out.scene  = std::move(rec->scene);
    out.error  = std::move(rec->error);
    {
        std::lock_guard<std::mutex> lock(table.mutex);
        const auto it = table.records.find(generation);
        if (it != table.records.end() && it->second.get() == rec.get())
            table.records.erase(it);
    }
    return true;
}

} // namespace Dark
