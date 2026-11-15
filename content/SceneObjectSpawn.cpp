#include "Content/SceneObjectSpawn.h"

#include "Content/ContentDb.h"
#include "Core/Json.h"
#include "Core/Log.h"
#include "Math/MathDefines.h"

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

        json colorJson(const float color[4])
        {
            return json::array({ color[0], color[1], color[2], color[3] });
        }

        json vec3Json(const Math::Vector3f& value)
        {
            return json::array({ value.x, value.y, value.z });
        }

        void readColor4(const json& value, float out[4])
        {
            if (!value.is_array())
                return;
            const int count = static_cast<int>(value.size());
            for (int i = 0; i < 4 && i < count; ++i)
                out[i] = numberAt(value[i], out[i]);
        }

        void readVec3(const json& value, Math::Vector3f& out)
        {
            if (!value.is_array() || value.size() < 2)
                return;
            out.x = numberAt(value[0], out.x);
            out.y = numberAt(value[1], out.y);
            if (value.size() >= 3)
                out.z = numberAt(value[2], out.z);
        }

        void readRange(const json& value, FloatRange& out)
        {
            if (!value.is_array() || value.size() < 2)
                return;
            out.min = numberAt(value[0], out.min);
            out.max = numberAt(value[1], out.max);
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

        int storedChildCollision(int collision)
        {
            if (collision == static_cast<int>(ParticleCollisionReaction::SpawnSecondary))
                return static_cast<int>(ParticleCollisionReaction::Stop);
            return collision;
        }

        ParticleCollisionReaction reactionFromInt(int value, ParticleCollisionReaction fallback)
        {
            switch (value)
            {
            case static_cast<int>(ParticleCollisionReaction::None):           return ParticleCollisionReaction::None;
            case static_cast<int>(ParticleCollisionReaction::Bounce):         return ParticleCollisionReaction::Bounce;
            case static_cast<int>(ParticleCollisionReaction::Stop):           return ParticleCollisionReaction::Stop;
            case static_cast<int>(ParticleCollisionReaction::SpawnSecondary): return ParticleCollisionReaction::SpawnSecondary;
            default:                                                          return fallback;
            }
        }

        json particleJson(const ParticleEmitterDesc& desc)
        {
            json particle;
            particle["name"]            = desc.name;
            particle["maxParticles"]    = desc.maxParticles;
            particle["emissionRate"]    = desc.emissionRate;
            particle["duration"]        = desc.duration;
            particle["looping"]         = desc.looping;
            particle["lifetime"]        = json::array({ desc.lifetime.min, desc.lifetime.max });
            particle["startSpeed"]      = json::array({ desc.startSpeed.min, desc.startSpeed.max });
            particle["startSize"]       = json::array({ desc.startSize.min, desc.startSize.max });
            particle["endSize"]         = json::array({ desc.endSize.min, desc.endSize.max });
            particle["startColor"]      = colorJson(desc.startColor);
            particle["endColor"]        = colorJson(desc.endColor);
            particle["gravity"]         = vec3Json(desc.gravity);
            particle["direction"]       = vec3Json(desc.direction);
            particle["spreadDegrees"]   = desc.spreadDegrees;
            particle["shape"]           = static_cast<int>(desc.shape);
            particle["shapeSize"]       = vec3Json(desc.shapeSize);
            particle["additiveBlend"]   = desc.additiveBlend;
            particle["simulationSpeed"] = desc.simulationSpeed;
            particle["renderMode"]      = static_cast<int>(desc.renderMode);
            particle["ribbonCount"]     = desc.ribbonCount;
            particle["ribbonUvScale"]   = desc.ribbonUvScale;
            particle["simMode"]         = static_cast<int>(desc.simMode);
            particle["collision"]       = static_cast<int>(desc.collision);
            particle["restitution"]     = desc.restitution;
            particle["friction"]        = desc.friction;
            particle["collisionThickness"]   = desc.collisionThickness;
            particle["collisionBias"]        = desc.collisionBias;
            particle["restLifetime"]         = desc.restLifetime;
            particle["maxSecondaryPerFrame"] = desc.maxSecondaryPerFrame;
            if (desc.hasSecondary)
            {
                json secondary;
                secondary["maxParticles"]  = desc.secondary.maxParticles;
                secondary["lifetime"]      = json::array({ desc.secondary.lifetime.min, desc.secondary.lifetime.max });
                secondary["startSize"]     = json::array({ desc.secondary.startSize.min, desc.secondary.startSize.max });
                secondary["restLifetime"]  = desc.secondary.restLifetime;
                secondary["additiveBlend"] = desc.secondary.additiveBlend;
                secondary["collision"]     = storedChildCollision(static_cast<int>(desc.secondary.collision));
                particle["secondary"]      = std::move(secondary);
            }
            return particle;
        }

        void readParticle(const json& particle, ParticleEmitterDesc& desc)
        {
            jsonStringOr(particle, "name", desc.name, desc.name, "SceneObject");
            int maxParticles = static_cast<int>(desc.maxParticles);
            if (jsonIntOr(particle, "maxParticles", maxParticles, maxParticles, "SceneObject") && maxParticles > 0)
                desc.maxParticles = static_cast<uint32_t>(maxParticles);
            jsonFloatOr(particle, "emissionRate", desc.emissionRate, desc.emissionRate, "SceneObject");
            jsonFloatOr(particle, "duration", desc.duration, desc.duration, "SceneObject");
            jsonBoolOr(particle, "looping", desc.looping, desc.looping, "SceneObject");
            jsonFloatOr(particle, "spreadDegrees", desc.spreadDegrees, desc.spreadDegrees, "SceneObject");
            int shape = static_cast<int>(desc.shape);
            if (jsonIntOr(particle, "shape", shape, shape, "SceneObject"))
                desc.shape = static_cast<ParticleEmitterDesc::Shape>(shape);
            jsonBoolOr(particle, "additiveBlend", desc.additiveBlend, desc.additiveBlend, "SceneObject");
            jsonFloatOr(particle, "simulationSpeed", desc.simulationSpeed, desc.simulationSpeed, "SceneObject");
            int renderMode = static_cast<int>(desc.renderMode);
            if (jsonIntOr(particle, "renderMode", renderMode, renderMode, "SceneObject"))
                desc.renderMode = static_cast<ParticleEmitterDesc::RenderMode>(renderMode);
            int ribbonCount = static_cast<int>(desc.ribbonCount);
            if (jsonIntOr(particle, "ribbonCount", ribbonCount, ribbonCount, "SceneObject") && ribbonCount > 0)
                desc.ribbonCount = static_cast<uint32_t>(ribbonCount);
            jsonFloatOr(particle, "ribbonUvScale", desc.ribbonUvScale, desc.ribbonUvScale, "SceneObject");
            int simMode = static_cast<int>(desc.simMode);
            if (jsonIntOr(particle, "simMode", simMode, simMode, "SceneObject"))
                desc.simMode = simMode == static_cast<int>(ParticleSimMode::Gpu) ? ParticleSimMode::Gpu : ParticleSimMode::Cpu;
            int collision = static_cast<int>(desc.collision);
            if (jsonIntOr(particle, "collision", collision, collision, "SceneObject"))
                desc.collision = reactionFromInt(collision, ParticleCollisionReaction::None);
            jsonFloatOr(particle, "restitution", desc.restitution, desc.restitution, "SceneObject");
            jsonFloatOr(particle, "friction", desc.friction, desc.friction, "SceneObject");
            jsonFloatOr(particle, "collisionThickness", desc.collisionThickness, desc.collisionThickness, "SceneObject");
            jsonFloatOr(particle, "collisionBias", desc.collisionBias, desc.collisionBias, "SceneObject");
            jsonFloatOr(particle, "restLifetime", desc.restLifetime, desc.restLifetime, "SceneObject");
            int secondaryCap = static_cast<int>(desc.maxSecondaryPerFrame);
            if (jsonIntOr(particle, "maxSecondaryPerFrame", secondaryCap, secondaryCap, "SceneObject") && secondaryCap >= 0)
                desc.maxSecondaryPerFrame = static_cast<uint32_t>(secondaryCap);
            if (particle.contains("lifetime"))
                readRange(particle["lifetime"], desc.lifetime);
            if (particle.contains("startSpeed"))
                readRange(particle["startSpeed"], desc.startSpeed);
            if (particle.contains("startSize"))
                readRange(particle["startSize"], desc.startSize);
            if (particle.contains("endSize"))
                readRange(particle["endSize"], desc.endSize);
            if (particle.contains("startColor"))
                readColor4(particle["startColor"], desc.startColor);
            if (particle.contains("endColor"))
                readColor4(particle["endColor"], desc.endColor);
            if (particle.contains("gravity"))
                readVec3(particle["gravity"], desc.gravity);
            if (particle.contains("direction"))
                readVec3(particle["direction"], desc.direction);
            if (particle.contains("shapeSize"))
                readVec3(particle["shapeSize"], desc.shapeSize);
            if (particle.contains("secondary") && particle["secondary"].is_object())
            {
                const json& secondary = particle["secondary"];
                desc.hasSecondary = true;
                int secondaryMax = static_cast<int>(desc.secondary.maxParticles);
                if (jsonIntOr(secondary, "maxParticles", secondaryMax, secondaryMax, "SceneObject") && secondaryMax > 0)
                    desc.secondary.maxParticles = static_cast<uint32_t>(secondaryMax);
                jsonFloatOr(secondary, "restLifetime", desc.secondary.restLifetime, desc.secondary.restLifetime, "SceneObject");
                jsonBoolOr(secondary, "additiveBlend", desc.secondary.additiveBlend, desc.secondary.additiveBlend, "SceneObject");
                int secondaryCollision = static_cast<int>(desc.secondary.collision);
                if (jsonIntOr(secondary, "collision", secondaryCollision, secondaryCollision, "SceneObject"))
                    desc.secondary.collision = reactionFromInt(storedChildCollision(secondaryCollision), ParticleCollisionReaction::Stop);
                if (secondary.contains("lifetime"))
                    readRange(secondary["lifetime"], desc.secondary.lifetime);
                if (secondary.contains("startSize"))
                    readRange(secondary["startSize"], desc.secondary.startSize);
            }
        }

        std::string bareUnit(std::string_view id)
        {
            const auto colon = id.rfind(':');
            if (colon == std::string_view::npos)
                return std::string(id);
            return std::string(id.substr(colon + 1));
        }

        void readSpawnerJson(const json& block, SpawnerDesc& desc)
        {
            std::string text;
            SpawnerMode mode{};
            if (jsonStringOr(block, "mode", text, std::string(), "SceneObject") && tryParseSpawnerMode(text, mode))
                desc.mode = mode;
            if (jsonStringOr(block, "unit", text, std::string(), "SceneObject") && !text.empty())
            {
                desc.unitArchetype = text;
                SpawnerUnit unit{};
                if (tryParseSpawnerUnit(bareUnit(text), unit))
                    desc.unit = unit;
            }
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
            std::string unitName = toString(desc.unit);
            if (!desc.unitArchetype.empty())
            {
                SpawnerUnit parsed{};
                if (!tryParseSpawnerUnit(bareUnit(desc.unitArchetype), parsed))
                    unitName = desc.unitArchetype;
            }
            spawner["unit"]         = std::move(unitName);
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
        writeComponent(object, "ParticleEmitter", particleJson(desc));
    }

    void sceneReadParticle(const SceneObjectData& object, ParticleEmitterDesc& desc)
    {
        desc = ParticleEmitterDesc{};
        readParticle(mergedComponent(object, "ParticleEmitter"), desc);
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
        const std::string kind = sceneLightKind(object);
        float value = fallback;
        const std::string_view field = key ? key : "";
        if (field == "intensity")
        {
            if (kind == "spot")
                value = 2513.0f;
            else if (kind == "point")
                value = 1885.0f;
            else if (kind == "directional")
                value = Math::Pi;
            else if (kind == "ambient")
                value = 1.0f;
        }
        else if (field == "range")
        {
            if (kind == "spot")
                value = 16.0f;
            else if (kind == "point")
                value = 8.0f;
        }
        else if (field == "inner" && (kind == "spot" || kind == "point"))
            value = 12.0f;
        else if (field == "outer" && (kind == "spot" || kind == "point"))
            value = 25.0f;
        else if (field == "sourceRadius" && (kind == "spot" || kind == "point"))
            value = 0.05f;
        const char* component = lightComponent(kind);
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
        const std::string id = sceneArchetypeId(object);
        if (const Archetype* archetype = findArchetype(id))
        {
            for (const ArchetypeComponent& entry : archetype->components)
            {
                if (entry.name != "Mesh")
                    continue;
                const json parsed = json::parse(entry.paramsJson.empty() ? "{}" : entry.paramsJson, nullptr, false);
                if (parsed.is_discarded())
                    return {};
                std::string primitive;
                jsonStringOr(parsed, "primitive", primitive, std::string(), "SceneObject");
                return primitive;
            }
        }
        const std::string_view bare = bareArchetypeId(id);
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
        return sceneHasBehavior(object, "prop.solid") || bare == "cube" || bare == "sphere";
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
