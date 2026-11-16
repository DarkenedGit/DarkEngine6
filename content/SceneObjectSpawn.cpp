#include "Content/SceneObjectSpawn.h"

#include "Content/ContentDb.h"
#include "Core/Json.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "Math/MathDefines.h"
#include "Media/MovieCue.h"
#include "Particles/ParticleEmitterJson.h"
#include "Water/WaterStream.h"

#include "third_party/nlohmann/json.hpp"

#include <string>
#include <utility>
#include <vector>

namespace Dark
{
    std::string sceneArchetypeId(const SceneObjectData& object)
    {
        return object.archetype;
    }

    namespace
    {
        using json = nlohmann::json;

        bool isPlacementKey(const std::string& key)
        {
            return key == "conceal" || key == "goal" || key == "circuit" || key == "zone" || key == "pickup" || key == "pickupCount" || key == "dialogue"
                || key == "squad" || key == "block" || key == "patrol" || key == "emissiveMesh";
        }

        bool bagIsRaw(const json& bag)
        {
            if (!bag.is_object())
                return false;
            static constexpr const char* kRaw[] = { "archetype", "position", "rotation", "scale", "color", "type", "overrides" };
            for (const char* key : kRaw)
            {
                if (bag.contains(key))
                    return true;
            }
            return false;
        }

        json loadBag(const SceneObjectData& object)
        {
            if (object.overridesJson.empty())
                return json::object();
            json parsed = json::parse(object.overridesJson, nullptr, false);
            if (parsed.is_discarded() || !parsed.is_object())
                return json::object();
            return parsed;
        }

        void storeBag(SceneObjectData& object, json& bag)
        {
            if (!bag.is_object() || bag.empty())
                object.overridesJson.clear();
            else
                object.overridesJson = bag.dump();
        }

        json* componentHost(json& bag)
        {
            if (!bag.is_object())
                return nullptr;
            if (!bagIsRaw(bag))
                return &bag;
            if (!bag.contains("overrides") || !bag["overrides"].is_object())
                bag["overrides"] = json::object();
            return &bag["overrides"];
        }

        const json* componentNode(const json& bag, std::string_view name)
        {
            if (!bag.is_object())
                return nullptr;
            const json* host = &bag;
            if (bagIsRaw(bag))
            {
                if (!bag.contains("overrides") || !bag["overrides"].is_object())
                    return nullptr;
                host = &bag["overrides"];
            }
            const std::string key(name);
            if (!host->contains(key) || !(*host)[key].is_object())
                return nullptr;
            return &(*host)[key];
        }

        void eraseComponent(json& bag, std::string_view name)
        {
            json* host = componentHost(bag);
            if (!host)
                return;
            host->erase(std::string(name));
            if (bagIsRaw(bag) && bag.contains("overrides") && bag["overrides"].is_object() && bag["overrides"].empty())
                bag.erase("overrides");
        }

        json mergedComponent(const SceneObjectData& object, std::string_view name)
        {
            json base = json::object();
            if (const Archetype* archetype = findArchetype(sceneArchetypeId(object)))
            {
                for (const ArchetypeComponent& entry : archetype->components)
                {
                    if (entry.name != name)
                        continue;
                    json parsed = json::parse(entry.paramsJson.empty() ? "{}" : entry.paramsJson, nullptr, false);
                    if (!parsed.is_discarded() && parsed.is_object())
                        base = std::move(parsed);
                    break;
                }
            }
            const json bag = loadBag(object);
            if (const json* over = componentNode(bag, name))
            {
                for (auto it = over->begin(); it != over->end(); ++it)
                    base[it.key()] = it.value();
            }
            return base;
        }

        void writeComponent(SceneObjectData& object, std::string_view name, json value)
        {
            json bag = loadBag(object);
            json* host = componentHost(bag);
            if (!host)
                return;
            (*host)[std::string(name)] = std::move(value);
            storeBag(object, bag);
        }

        float numberAt(const json& value, float fallback)
        {
            if (const auto* number = value.get_ptr<const json::number_float_t*>())
                return static_cast<float>(*number);
            if (const auto* number = value.get_ptr<const json::number_integer_t*>())
                return static_cast<float>(*number);
            if (const auto* number = value.get_ptr<const json::number_unsigned_t*>())
                return static_cast<float>(*number);
            return fallback;
        }

        int intAt(const json& value, int fallback)
        {
            if (const auto* number = value.get_ptr<const json::number_integer_t*>())
                return static_cast<int>(*number);
            if (const auto* number = value.get_ptr<const json::number_unsigned_t*>())
                return static_cast<int>(*number);
            return static_cast<int>(numberAt(value, static_cast<float>(fallback)));
        }

        bool boolAt(const json& value, bool fallback)
        {
            if (const auto* flag = value.get_ptr<const json::boolean_t*>())
                return *flag;
            return fallback;
        }

        const char* lightComponent(const std::string& kind)
        {
            if (kind == "ambient")
                return "AmbientLight";
            if (kind == "directional")
                return "DirectionalLight";
            if (kind == "point" || kind == "spot")
                return "LocalLight";
            return nullptr;
        }

        std::string kindFromLocal(const json& light)
        {
            std::string kind;
            jsonStringOr(light, "kind", kind, std::string(), "SceneObject");
            return kind == "spot" ? std::string("spot") : std::string("point");
        }

        void readSpawnerJson(const json& block, SpawnerDesc& desc)
        {
            std::string text;
            SpawnerMode mode{};
            if (jsonStringOr(block, "mode", text, std::string(), "SceneObject") && tryParseSpawnerMode(text, mode))
                desc.mode = mode;
            if (jsonStringOr(block, "unit", text, std::string(), "SceneObject") && !text.empty())
                desc.unitArchetype = text;
            SpawnerWaveGate gate{};
            if (jsonStringOr(block, "gate", text, std::string(), "SceneObject") && tryParseSpawnerWaveGate(text, gate))
                desc.gate = gate;
            jsonIntOr(block, "count", desc.count, desc.count, "SceneObject");
            jsonFloatOr(block, "radius", desc.radius, desc.radius, "SceneObject");
            jsonFloatOr(block, "respawnDelay", desc.respawnDelay, desc.respawnDelay, "SceneObject");
            jsonFloatOr(block, "waveInterval", desc.waveInterval, desc.waveInterval, "SceneObject");
            jsonIntOr(block, "maxWaves", desc.maxWaves, desc.maxWaves, "SceneObject");
            double seed = static_cast<double>(desc.seed);
            if (jsonDoubleOr(block, "seed", seed, seed, "SceneObject") && seed >= 1.0 && seed <= 4294967295.0)
                desc.seed = static_cast<uint32_t>(seed);
            jsonBoolOr(block, "enabled", desc.enabled, desc.enabled, "SceneObject");
            clampSpawnerDesc(desc);
        }

        json spawnerJson(const SpawnerDesc& desc)
        {
            json spawner;
            spawner["mode"] = toString(desc.mode);
            spawner["unit"]         = desc.unitArchetype.empty() ? std::string("hunter") : desc.unitArchetype;
            spawner["gate"]         = toString(desc.gate);
            spawner["count"]        = desc.count;
            spawner["radius"]       = desc.radius;
            spawner["respawnDelay"] = desc.respawnDelay;
            spawner["waveInterval"] = desc.waveInterval;
            spawner["maxWaves"]     = desc.maxWaves;
            spawner["enabled"]      = desc.enabled;
            spawner["seed"]         = desc.seed;
            return spawner;
        }

        bool readPoints(const json& points, std::vector<Math::Vector2f>& out)
        {
            if (!points.is_array() || points.size() < 2 || points.size() > 256)
                return false;
            out.clear();
            out.reserve(points.size());
            for (const json& point : points)
            {
                if (!point.is_array() || point.size() < 2 || !point[0].is_number() || !point[1].is_number())
                    return false;
                out.emplace_back(numberAt(point[0], 0.0f), numberAt(point[1], 0.0f));
            }
            return out.size() >= 2;
        }

        std::string_view bareArchetypeId(std::string_view id)
        {
            const auto colon = id.rfind(':');
            if (colon == std::string_view::npos)
                return id;
            return id.substr(colon + 1);
        }

        std::string bagString(const json& bag, const char* key)
        {
            std::string text;
            jsonStringOr(bag, key, text, std::string(), "SceneObject");
            return text;
        }

        double doubleAt(const json& value)
        {
            if (const auto* number = value.get_ptr<const json::number_float_t*>())
                return *number;
            if (const auto* number = value.get_ptr<const json::number_integer_t*>())
                return static_cast<double>(*number);
            if (const auto* number = value.get_ptr<const json::number_unsigned_t*>())
                return static_cast<double>(*number);
            return 0.0;
        }

        bool jsonSame(const json& a, const json& b)
        {
            if (a.is_number() && b.is_number())
            {
                const double da = doubleAt(a);
                const double db = doubleAt(b);
                if (da == db)
                    return true;
                // Archetype decimals and live floats meet at float precision (0.05).
                if (a.is_number_float() || b.is_number_float())
                    return static_cast<float>(da) == static_cast<float>(db);
                return false;
            }
            if (a.is_array() && b.is_array())
            {
                if (a.size() != b.size())
                    return false;
                for (size_t i = 0; i < a.size(); ++i)
                {
                    if (!jsonSame(a[i], b[i]))
                        return false;
                }
                return true;
            }
            if (a.is_object() && b.is_object())
            {
                if (a.size() != b.size())
                    return false;
                for (auto it = a.begin(); it != a.end(); ++it)
                {
                    if (!b.contains(it.key()) || !jsonSame(it.value(), b[it.key()]))
                        return false;
                }
                return true;
            }
            if (a.is_boolean() && b.is_boolean())
                return boolAt(a, false) == boolAt(b, true);
            if (a.is_string() && b.is_string())
            {
                const auto* left  = a.get_ptr<const json::string_t*>();
                const auto* right = b.get_ptr<const json::string_t*>();
                return left && right && *left == *right;
            }
            return a.is_null() && b.is_null();
        }

        json parseObject(std::string_view text)
        {
            if (text.empty())
                return json::object();
            json parsed = json::parse(std::string(text), nullptr, false);
            if (parsed.is_discarded() || !parsed.is_object())
                return json::object();
            return parsed;
        }

        // Component defaults, then archetype params. An omitted archetype key keeps the default.
        json diffBaseline(const SceneObjectData& object, std::string_view component, json defaults)
        {
            if (!defaults.is_object())
                defaults = json::object();
            const json archetype = parseObject(sceneArchetypeComponentJson(object, component));
            for (auto it = archetype.begin(); it != archetype.end(); ++it)
                defaults[it.key()] = it.value();
            return defaults;
        }

        json pointListJson(const std::vector<Math::Vector2f>& points)
        {
            json encoded = json::array();
            for (const Math::Vector2f& point : points)
                encoded.push_back(json::array({ point.x, point.y }));
            return encoded;
        }

    } // namespace

    bool sceneIsPlaceholder(const SceneObjectData& object)
    {
        return object.archetype.empty() || !contentHasArchetype(object.archetype);
    }

    bool sceneIsDataDriven(const SceneObjectData& object)
    {
        return !object.archetype.empty() && contentHasArchetype(object.archetype);
    }

    std::string sceneMergedComponent(const SceneObjectData& object, std::string_view component)
    {
        const json merged = mergedComponent(object, component);
        return merged.empty() ? std::string("{}") : merged.dump();
    }

    std::string sceneArchetypeComponentJson(const SceneObjectData& object, std::string_view component)
    {
        if (const Archetype* archetype = findArchetype(sceneArchetypeId(object)))
        {
            for (const ArchetypeComponent& entry : archetype->components)
            {
                if (entry.name == component)
                    return entry.paramsJson.empty() ? std::string("{}") : entry.paramsJson;
            }
        }
        return "{}";
    }

    bool sceneCarriesComponent(const SceneObjectData& object, std::string_view component)
    {
        if (sceneHasComponent(object, component))
            return true;
        const json bag = loadBag(object);
        return componentNode(bag, component) != nullptr;
    }

    void sceneSetComponentJson(SceneObjectData& object, std::string_view component, std::string_view jsonText)
    {
        json value = json::parse(jsonText.empty() ? "{}" : std::string(jsonText), nullptr, false);
        if (value.is_discarded() || !value.is_object())
            value = json::object();
        writeComponent(object, component, std::move(value));
    }

    std::string sceneComponentOverrides(const SceneObjectData& object)
    {
        const json bag = loadBag(object);
        const json* source = &bag;
        if (bagIsRaw(bag))
        {
            if (!bag.contains("overrides") || !bag["overrides"].is_object())
                return {};
            source = &bag["overrides"];
        }
        json overlay = json::object();
        for (auto it = source->begin(); it != source->end(); ++it)
        {
            if (!it.value().is_object() || isPlacementKey(it.key()))
                continue;
            overlay[it.key()] = it.value();
        }
        return overlay.empty() ? std::string{} : overlay.dump();
    }

    void assignSceneParticle(SceneObjectData& object, const ParticleEmitterDesc& desc)
    {
        writeComponent(object, "ParticleEmitter", particleToJson(desc));
    }

    void sceneReadParticle(const SceneObjectData& object, ParticleEmitterDesc& desc)
    {
        desc = ParticleEmitterDesc{};
        particleFromJson(mergedComponent(object, "ParticleEmitter"), desc, "SceneObject");
    }

    bool sceneHasComponent(const SceneObjectData& object, std::string_view component)
    {
        const std::string id = sceneArchetypeId(object);
        if (const Archetype* archetype = findArchetype(id))
        {
            for (const ArchetypeComponent& entry : archetype->components)
            {
                if (entry.name == component)
                    return true;
            }
            return false;
        }
        return false;
    }

    bool sceneHasBehavior(const SceneObjectData& object, std::string_view behavior)
    {
        const std::string id = sceneArchetypeId(object);
        if (const Archetype* archetype = findArchetype(id))
        {
            for (const ArchetypeBehavior& entry : archetype->behaviors)
            {
                if (entry.name == behavior)
                    return true;
            }
            return false;
        }
        return false;
    }

    std::string sceneLightKind(const SceneObjectData& object)
    {
        const json bag = loadBag(object);
        if (componentNode(bag, "LocalLight"))
            return kindFromLocal(mergedComponent(object, "LocalLight"));
        if (componentNode(bag, "AmbientLight"))
            return "ambient";
        if (componentNode(bag, "DirectionalLight"))
            return "directional";
        if (const Archetype* archetype = findArchetype(sceneArchetypeId(object)))
        {
            for (const ArchetypeComponent& entry : archetype->components)
            {
                if (entry.name == "AmbientLight")
                    return "ambient";
                if (entry.name == "DirectionalLight")
                    return "directional";
                if (entry.name == "LocalLight")
                    return kindFromLocal(mergedComponent(object, "LocalLight"));
            }
        }
        return {};
    }

    bool sceneHasLight(const SceneObjectData& object)
    {
        return !sceneLightKind(object).empty();
    }

    float sceneLightFloat(const SceneObjectData& object, const char* key, float fallback)
    {
        const char* component = lightComponent(sceneLightKind(object));
        float value = fallback;
        if (!component)
            return value;
        jsonFloatOr(mergedComponent(object, component), key, value, value, "SceneObject");
        return value;
    }

    bool sceneLightBool(const SceneObjectData& object, const char* key, bool fallback)
    {
        const std::string kind = sceneLightKind(object);
        const std::string_view field = key ? key : "";
        if (field == "castShadow" && (kind == "ambient" || kind == "directional"))
            return false;
        bool value = fallback;
        if (field == "enabled" && !kind.empty())
            value = true;
        const char* component = lightComponent(kind);
        if (!component)
            return value;
        jsonBoolOr(mergedComponent(object, component), key, value, value, "SceneObject");
        return value;
    }

    bool sceneSearchlight(const SceneObjectData& object)
    {
        return sceneLightKind(object) == "spot" && sceneLightBool(object, "searchlight", false);
    }

    float sceneSweepDeg(const SceneObjectData& object)
    {
        float sweep = 60.0f;
        jsonFloatOr(mergedComponent(object, "LocalLight"), "sweep", sweep, 60.0f, "SceneObject");
        if (!(sweep >= 0.0f))
            sweep = 0.0f;
        if (sweep > 340.0f)
            sweep = 340.0f;
        return sweep;
    }

    void sceneWriteLight(SceneObjectData& object, const SceneLightFields& light)
    {
        json bag = loadBag(object);
        json* host = componentHost(bag);
        if (!host)
            return;
        host->erase("LocalLight");
        host->erase("AmbientLight");
        host->erase("DirectionalLight");
        const std::string kind = light.kind.empty() ? std::string("point") : light.kind;
        if (kind == "ambient" || kind == "directional")
        {
            json global;
            global["intensity"] = light.intensity;
            global["enabled"]   = light.enabled;
            (*host)[kind == "ambient" ? "AmbientLight" : "DirectionalLight"] = std::move(global);
        }
        else
        {
            json local;
            local["kind"]           = kind == "spot" ? "spot" : "point";
            local["intensity"]      = light.intensity;
            local["range"]          = light.range;
            local["inner"]          = light.innerDeg;
            local["outer"]          = light.outerDeg;
            local["sourceRadius"]   = light.sourceRadius;
            local["enabled"]        = light.enabled;
            local["castShadow"]     = light.castShadow;
            local["useObjectColor"] = true;
            if (kind == "spot" && light.searchlight)
            {
                local["searchlight"] = true;
                local["sweep"]       = light.sweepDeg;
            }
            if (!light.circuit.empty())
                local["circuit"] = light.circuit;
            (*host)["LocalLight"] = std::move(local);
        }
        storeBag(object, bag);
    }

    std::string sceneModelPath(const SceneObjectData& object)
    {
        std::string path;
        jsonStringOr(mergedComponent(object, "Model"), "gltf", path, std::string(), "SceneObject");
        return path;
    }

    std::string archetypeModelPath(std::string_view archetypeId)
    {
        SceneObjectData probe;
        probe.archetype = std::string(archetypeId);
        return sceneModelPath(probe);
    }

    void sceneWriteModelPath(SceneObjectData& object, std::string path)
    {
        if (path.empty())
        {
            json bag = loadBag(object);
            eraseComponent(bag, "Model");
            storeBag(object, bag);
            return;
        }
        json model;
        model["gltf"] = std::move(path);
        writeComponent(object, "Model", std::move(model));
    }

    float sceneEmissive(const SceneObjectData& object)
    {
        float emissive = 0.0f;
        jsonFloatOr(mergedComponent(object, "Mesh"), "emissive", emissive, 0.0f, "SceneObject");
        return emissive;
    }

    void sceneWriteEmissive(SceneObjectData& object, float emissive)
    {
        json bag = loadBag(object);
        json* host = componentHost(bag);
        if (!host)
            return;
        json mesh = json::object();
        if (host->contains("Mesh") && (*host)["Mesh"].is_object())
            mesh = (*host)["Mesh"];
        if (emissive == 0.0f)
            mesh.erase("emissive");
        else
            mesh["emissive"] = emissive;
        if (mesh.empty())
            host->erase("Mesh");
        else
            (*host)["Mesh"] = std::move(mesh);
        if (bagIsRaw(bag) && bag.contains("overrides") && bag["overrides"].is_object() && bag["overrides"].empty())
            bag.erase("overrides");
        storeBag(object, bag);
    }

    std::string sceneMoviePath(const SceneObjectData& object)
    {
        std::string path;
        jsonStringOr(mergedComponent(object, "Movie"), "path", path, std::string(), "SceneObject");
        return path;
    }

    float sceneMovieRadius(const SceneObjectData& object)
    {
        float radius = 2.0f;
        jsonFloatOr(mergedComponent(object, "Movie"), "radius", radius, 2.0f, "SceneObject");
        if (!(radius >= 0.0f))
            radius = 0.0f;
        return radius;
    }

    bool sceneMovieLoop(const SceneObjectData& object)
    {
        bool loop = false;
        jsonBoolOr(mergedComponent(object, "Movie"), "loop", loop, false, "SceneObject");
        return loop;
    }

    void sceneWriteMovie(SceneObjectData& object, std::string path, float radius, bool loop)
    {
        if (!(radius >= 0.0f))
            radius = 0.0f;
        if (path.empty() && radius == 2.0f && !loop)
        {
            json bag = loadBag(object);
            eraseComponent(bag, "Movie");
            storeBag(object, bag);
            return;
        }
        json movie;
        movie["path"]   = std::move(path);
        movie["radius"] = radius;
        movie["loop"]   = loop;
        writeComponent(object, "Movie", std::move(movie));
    }

    bool sceneReadStream(const SceneObjectData& object, float& width, float& flowSpeed, std::vector<Math::Vector2f>& points)
    {
        width     = 3.5f;
        flowSpeed = 1.6f;
        points.clear();
        if (!sceneCarriesComponent(object, "Stream"))
            return false;
        const json stream = mergedComponent(object, "Stream");
        jsonFloatOr(stream, "width", width, 3.5f, "SceneObject");
        jsonFloatOr(stream, "flowSpeed", flowSpeed, 1.6f, "SceneObject");
        if (!(width >= 1.5f))
            width = 1.5f;
        if (width > 12.0f)
            width = 12.0f;
        if (!stream.contains("points") || !readPoints(stream["points"], points))
            return false;
        return true;
    }

    void sceneWriteStream(SceneObjectData& object, float width, float flowSpeed, const std::vector<Math::Vector2f>& points)
    {
        if (!(width >= 1.5f))
            width = 1.5f;
        if (width > 12.0f)
            width = 12.0f;
        json stream;
        stream["width"]     = width;
        stream["flowSpeed"] = flowSpeed;
        json encoded = json::array();
        for (const Math::Vector2f& point : points)
            encoded.push_back(json::array({ point.x, point.y }));
        stream["points"] = std::move(encoded);
        writeComponent(object, "Stream", std::move(stream));
    }

    bool sceneReadSpawner(const SceneObjectData& object, SpawnerDesc& desc)
    {
        if (!sceneCarriesComponent(object, "Spawner"))
            return false;
        desc = SpawnerDesc{};
        readSpawnerJson(mergedComponent(object, "Spawner"), desc);
        return true;
    }

    void sceneWriteSpawner(SceneObjectData& object, const SpawnerDesc& desc)
    {
        SpawnerDesc clamped = desc;
        clampSpawnerDesc(clamped);
        writeComponent(object, "Spawner", spawnerJson(clamped));
    }

    void sceneWriteComponentDiff(SceneObjectData& object, std::string_view component, std::string_view liveJson, std::string_view archetypeJson)
    {
        if (component.empty())
            return;
        const json live = json::parse(liveJson.empty() ? std::string("{}") : std::string(liveJson), nullptr, false);
        if (live.is_discarded() || !live.is_object())
        {
            DE_LOG_ERROR(LogCategory::Core, "SceneObject: \"{}\" diff is not an object", std::string(component));
            return;
        }
        const json archetype = parseObject(archetypeJson);
        json bag = loadBag(object);
        json* host = componentHost(bag);
        if (!host)
            return;
        const std::string key(component);
        json result = json::object();
        if (host->contains(key) && (*host)[key].is_object())
            result = (*host)[key];
        for (auto it = live.begin(); it != live.end(); ++it)
        {
            if (it.value().is_null())
            {
                result.erase(it.key());
                continue;
            }
            if (archetype.contains(it.key()) && jsonSame(archetype[it.key()], it.value()))
                result.erase(it.key());
            else
                result[it.key()] = it.value();
        }
        if (result.empty())
            host->erase(key);
        else
            (*host)[key] = std::move(result);
        if (bagIsRaw(bag) && bag.contains("overrides") && bag["overrides"].is_object() && bag["overrides"].empty())
            bag.erase("overrides");
        storeBag(object, bag);
    }

    void sceneWriteLightDiff(SceneObjectData& object, const SceneLightFields& light)
    {
        const std::string kind = light.kind.empty() ? std::string("point") : light.kind;
        json live = json::object();
        json defaults = json::object();
        const char* component = "LocalLight";
        if (kind == "ambient")
        {
            component = "AmbientLight";
            const AmbientLightComponent defaultsComp{};
            defaults["intensity"] = defaultsComp.intensity;
            defaults["enabled"]   = defaultsComp.enabled;
            live["intensity"]     = light.intensity;
            live["enabled"]       = light.enabled;
        }
        else if (kind == "directional")
        {
            component = "DirectionalLight";
            const DirectionalLightComponent defaultsComp{};
            defaults["intensity"] = defaultsComp.intensity;
            defaults["enabled"]   = defaultsComp.enabled;
            live["intensity"]     = light.intensity;
            live["enabled"]       = light.enabled;
        }
        else
        {
            const LocalLightComponent defaultsComp{};
            defaults["kind"]         = "point";
            defaults["intensity"]    = defaultsComp.intensity;
            defaults["range"]        = defaultsComp.range;
            defaults["inner"]        = defaultsComp.innerConeDeg;
            defaults["outer"]        = defaultsComp.outerConeDeg;
            defaults["sourceRadius"] = defaultsComp.sourceRadius;
            defaults["enabled"]      = defaultsComp.enabled;
            defaults["castShadow"]   = defaultsComp.castShadow;
            live["kind"]             = kind == "spot" ? "spot" : "point";
            live["intensity"]        = light.intensity;
            live["range"]            = light.range;
            live["inner"]            = light.innerDeg;
            live["outer"]            = light.outerDeg;
            live["sourceRadius"]     = light.sourceRadius;
            live["enabled"]          = light.enabled;
            live["castShadow"]       = light.castShadow;
        }
        sceneWriteComponentDiff(object, component, live.dump(), diffBaseline(object, component, std::move(defaults)).dump());
    }

    void sceneWriteParticleDiff(SceneObjectData& object, const ParticleEmitterDesc& desc)
    {
        json live = particleToJson(desc);
        if (!desc.hasSecondary)
            live["secondary"] = nullptr;
        const json defaults = particleToJson(ParticleEmitterDesc{});
        sceneWriteComponentDiff(object, "ParticleEmitter", live.dump(), diffBaseline(object, "ParticleEmitter", defaults).dump());
    }

    void sceneWriteStreamDiff(SceneObjectData& object, float width, float flowSpeed, const std::vector<Math::Vector2f>& points)
    {
        if (!(width >= 1.5f))
            width = 1.5f;
        if (width > 12.0f)
            width = 12.0f;
        json live = json::object();
        live["width"]     = width;
        live["flowSpeed"] = flowSpeed;
        live["points"]    = pointListJson(points);
        const StreamComponent defaultsComp{};
        json defaults = json::object();
        defaults["width"]     = defaultsComp.width;
        defaults["flowSpeed"] = defaultsComp.flowSpeed;
        defaults["points"]    = json::array();
        sceneWriteComponentDiff(object, "Stream", live.dump(), diffBaseline(object, "Stream", std::move(defaults)).dump());
    }

    void sceneWriteSpawnerDiff(SceneObjectData& object, const SpawnerDesc& desc)
    {
        SpawnerDesc clamped = desc;
        clampSpawnerDesc(clamped);
        const json live     = spawnerJson(clamped);
        const json defaults = spawnerJson(SpawnerDesc{});
        sceneWriteComponentDiff(object, "Spawner", live.dump(), diffBaseline(object, "Spawner", defaults).dump());
    }

    void sceneWriteMovieDiff(SceneObjectData& object, std::string path, float radius, bool loop)
    {
        if (!(radius >= 0.0f))
            radius = 0.0f;
        json live = json::object();
        live["path"]   = std::move(path);
        live["radius"] = radius;
        live["loop"]   = loop;
        const MovieCueComponent defaultsComp{};
        json defaults = json::object();
        defaults["path"]   = defaultsComp.path;
        defaults["radius"] = defaultsComp.radius;
        defaults["loop"]   = defaultsComp.loop;
        sceneWriteComponentDiff(object, "Movie", live.dump(), diffBaseline(object, "Movie", std::move(defaults)).dump());
    }

    void sceneWriteModelDiff(SceneObjectData& object, std::string path)
    {
        // An empty path clears the gltf override. anim and any other Model key stay.
        json live = json::object();
        if (path.empty())
            live["gltf"] = nullptr;
        else
            live["gltf"] = std::move(path);
        sceneWriteComponentDiff(object, "Model", live.dump(), diffBaseline(object, "Model", json::object()).dump());
    }

    void sceneWritePlaceholderTransform(SceneObjectData& object)
    {
        json bag = loadBag(object);
        if (!bag.is_object())
            bag = json::object();
        bag["position"] = json::array({ object.position.x, object.position.y, object.position.z });
        bag["rotation"] = json::array({ object.rotation.w, object.rotation.x, object.rotation.y, object.rotation.z });
        bag["scale"]    = json::array({ object.scale.x, object.scale.y, object.scale.z });
        storeBag(object, bag);
    }

    void sceneWriteEmissiveDiff(SceneObjectData& object, float emissive)
    {
        json live = json::object();
        live["emissive"] = emissive;
        const MeshComponent defaultsComp{};
        json defaults = json::object();
        defaults["emissive"] = defaultsComp.emissive;
        sceneWriteComponentDiff(object, "Mesh", live.dump(), diffBaseline(object, "Mesh", std::move(defaults)).dump());
    }

    bool sceneConceal(const SceneObjectData& object)
    {
        bool value = false;
        jsonBoolOr(loadBag(object), "conceal", value, false, "SceneObject");
        return value;
    }

    bool sceneGoal(const SceneObjectData& object)
    {
        bool value = false;
        jsonBoolOr(loadBag(object), "goal", value, false, "SceneObject");
        return value;
    }

    bool sceneBlock(const SceneObjectData& object)
    {
        bool value = true;
        jsonBoolOr(loadBag(object), "block", value, true, "SceneObject");
        return value;
    }

    std::string sceneZone(const SceneObjectData& object)
    {
        return bagString(loadBag(object), "zone");
    }

    std::string sceneCircuit(const SceneObjectData& object)
    {
        std::string fromLight;
        jsonStringOr(mergedComponent(object, "LocalLight"), "circuit", fromLight, std::string(), "SceneObject");
        if (!fromLight.empty())
            return fromLight;
        return bagString(loadBag(object), "circuit");
    }

    std::string scenePickup(const SceneObjectData& object)
    {
        return bagString(loadBag(object), "pickup");
    }

    int scenePickupCount(const SceneObjectData& object)
    {
        int count = 1;
        jsonIntOr(loadBag(object), "pickupCount", count, 1, "SceneObject");
        return count > 0 ? count : 1;
    }

    std::string sceneDialogue(const SceneObjectData& object)
    {
        return bagString(loadBag(object), "dialogue");
    }

    std::string sceneSquad(const SceneObjectData& object)
    {
        return bagString(loadBag(object), "squad");
    }

    std::vector<Math::Vector2f> scenePatrol(const SceneObjectData& object)
    {
        std::vector<Math::Vector2f> points;
        const json bag = loadBag(object);
        if (!bag.contains("patrol") || !bag["patrol"].is_array())
            return points;
        for (const json& point : bag["patrol"])
        {
            if (!point.is_array() || point.size() < 2 || !point[0].is_number() || !point[1].is_number())
                continue;
            points.emplace_back(numberAt(point[0], 0.0f), numberAt(point[1], 0.0f));
        }
        return points;
    }

    int sceneEmissiveMeshIndex(const SceneObjectData& object)
    {
        int index = -1;
        jsonIntOr(loadBag(object), "emissiveMesh", index, -1, "SceneObject");
        return index;
    }

    void sceneSetBool(SceneObjectData& object, const char* key, bool value)
    {
        if (!key || key[0] == '\0')
            return;
        json bag = loadBag(object);
        bag[key] = value;
        storeBag(object, bag);
    }

    void sceneSetString(SceneObjectData& object, const char* key, std::string value)
    {
        if (!key || key[0] == '\0')
            return;
        json bag = loadBag(object);
        if (value.empty())
            bag.erase(key);
        else
            bag[key] = std::move(value);
        storeBag(object, bag);
    }

    void sceneSetInt(SceneObjectData& object, const char* key, int value)
    {
        if (!key || key[0] == '\0')
            return;
        json bag = loadBag(object);
        bag[key] = value;
        storeBag(object, bag);
    }

    void sceneSetPatrol(SceneObjectData& object, const std::vector<Math::Vector2f>& points)
    {
        json bag = loadBag(object);
        if (points.empty())
        {
            bag.erase("patrol");
            storeBag(object, bag);
            return;
        }
        json encoded = json::array();
        for (const Math::Vector2f& point : points)
            encoded.push_back(json::array({ point.x, point.y }));
        bag["patrol"] = std::move(encoded);
        storeBag(object, bag);
    }

    void sceneSetLocalLightBool(SceneObjectData& object, const char* key, bool value)
    {
        if (!key || key[0] == '\0')
            return;
        json light = json::object();
        const json bag = loadBag(object);
        if (const json* over = componentNode(bag, "LocalLight"))
            light = *over;
        light[key] = value;
        writeComponent(object, "LocalLight", std::move(light));
    }

    void sceneSetLocalLightFloat(SceneObjectData& object, const char* key, float value)
    {
        if (!key || key[0] == '\0')
            return;
        json light = json::object();
        const json bag = loadBag(object);
        if (const json* over = componentNode(bag, "LocalLight"))
            light = *over;
        light[key] = value;
        writeComponent(object, "LocalLight", std::move(light));
    }

    void sceneSaveParts(const SceneObjectData& object, bool& raw, std::string& rawJson, std::string& placementJson, std::string& componentOverrides)
    {
        raw = false;
        rawJson.clear();
        placementJson.clear();
        componentOverrides.clear();
        const json bag = loadBag(object);
        if (bagIsRaw(bag))
        {
            raw     = true;
            rawJson = object.overridesJson;
            return;
        }
        json placement = json::object();
        json components = json::object();
        std::string pickup;
        int pickupCount = 1;
        bool hasPickupCount = false;
        for (auto it = bag.begin(); it != bag.end(); ++it)
        {
            const std::string& key = it.key();
            const json& value = it.value();
            if (key == "conceal" || key == "goal")
            {
                if (boolAt(value, false))
                    placement[key] = true;
                continue;
            }
            if (key == "block")
            {
                if (value.is_boolean() && !boolAt(value, true))
                    placement[key] = false;
                continue;
            }
            if (key == "pickup")
            {
                if (const auto* text = value.get_ptr<const json::string_t*>())
                    pickup = *text;
                continue;
            }
            if (key == "pickupCount")
            {
                hasPickupCount = value.is_number();
                pickupCount    = intAt(value, 1);
                continue;
            }
            if (key == "circuit" || key == "zone" || key == "dialogue" || key == "squad")
            {
                if (const auto* text = value.get_ptr<const json::string_t*>(); text && !text->empty())
                    placement[key] = *text;
                continue;
            }
            if (key == "patrol")
            {
                if (value.is_array() && !value.empty())
                    placement[key] = value;
                continue;
            }
            if (key == "emissiveMesh")
            {
                const int index = intAt(value, -1);
                if (value.is_number() && index >= 0)
                    placement[key] = index;
                continue;
            }
            if (value.is_object())
                components[key] = value;
            else
                placement[key] = value;
        }
        if (!pickup.empty())
        {
            placement["pickup"] = std::move(pickup);
            if (hasPickupCount && pickupCount != 1)
                placement["pickupCount"] = pickupCount;
        }
        if (!placement.empty())
            placementJson = placement.dump();
        if (!components.empty())
            componentOverrides = components.dump();
    }

    std::string sceneMeshPrimitive(const SceneObjectData& object)
    {
        std::string primitive;
        jsonStringOr(mergedComponent(object, "Mesh"), "primitive", primitive, std::string(), "SceneObject");
        if (!primitive.empty())
            return primitive;
        if (contentDatabaseLoaded())
            return {};
        const std::string_view bare = bareArchetypeId(sceneArchetypeId(object));
        if (bare == "sphere")
            return "sphere";
        if (bare == "cube" || bare == "platform")
            return "cube";
        return {};
    }

    bool sceneWalkObstacle(const SceneObjectData& object, std::string& primitive)
    {
        primitive.clear();
        const std::string id = sceneArchetypeId(object);
        const std::string_view bare = bareArchetypeId(id);
        if (bare == "platform" || sceneHasComponent(object, "Platform") || sceneHasBehavior(object, "platform.marker"))
            return false;
        primitive = sceneMeshPrimitive(object);
        if (primitive != "cube" && primitive != "sphere")
            return false;
        if (sceneHasBehavior(object, "prop.solid"))
            return true;
        return !contentDatabaseLoaded() && (bare == "cube" || bare == "sphere");
    }

    Entity instantiateSceneObject(const ArchetypeSpawnHost& host, const SceneObjectData& object, ContentError* err)
    {
        ContentError local;
        ContentError& fail = err ? *err : local;
        if (sceneIsPlaceholder(object))
        {
            fail.message = "placeholder has no archetype spawn";
            return {};
        }
        ArchetypeSpawnPose pose;
        pose.position = object.position;
        pose.rotation = object.rotation;
        pose.scale    = object.scale;
        pose.color[0] = object.color[0];
        pose.color[1] = object.color[1];
        pose.color[2] = object.color[2];
        pose.color[3] = object.color[3];
        return instantiateArchetype(host, sceneArchetypeId(object), pose, sceneComponentOverrides(object), err);
    }

} // namespace Dark
