#include "Content/SceneObjectSpawn.h"

#include "Content/ContentDb.h"
#include "Core/Log.h"

#include "third_party/nlohmann/json.hpp"

namespace Dark
{
    std::string sceneArchetypeId(const SceneObjectData& object)
    {
        return object.archetype;
    }

    namespace
    {
        using json = nlohmann::json;

        json colorJson(const float color[4])
        {
            return json::array({ color[0], color[1], color[2], color[3] });
        }

        json vec3Json(const Math::Vector3f& value)
        {
            return json::array({ value.x, value.y, value.z });
        }

        json vec3Json3(const float value[3])
        {
            return json::array({ value[0], value[1], value[2] });
        }

        std::string_view bareArchetypeId(std::string_view id)
        {
            const auto colon = id.rfind(':');
            if (colon == std::string_view::npos)
                return id;
            return id.substr(colon + 1);
        }

        json particleJson(const SceneObjectData& object)
        {
            json particle;
            particle["name"]            = object.particleName;
            particle["maxParticles"]    = object.maxParticles;
            particle["emissionRate"]    = object.emissionRate;
            particle["duration"]        = object.duration;
            particle["looping"]         = object.looping;
            particle["lifetime"]        = json::array({ object.lifetimeMin, object.lifetimeMax });
            particle["startSpeed"]      = json::array({ object.startSpeedMin, object.startSpeedMax });
            particle["startSize"]       = json::array({ object.startSizeMin, object.startSizeMax });
            particle["endSize"]         = json::array({ object.endSizeMin, object.endSizeMax });
            particle["startColor"]      = colorJson(object.startColor);
            particle["endColor"]        = colorJson(object.endColor);
            particle["gravity"]         = vec3Json(object.gravity);
            particle["direction"]       = vec3Json(object.direction);
            particle["spreadDegrees"]   = object.spreadDegrees;
            particle["shape"]           = object.shape;
            particle["shapeSize"]       = vec3Json(object.shapeSize);
            particle["additiveBlend"]   = object.additiveBlend;
            particle["simulationSpeed"] = object.simulationSpeed;
            particle["renderMode"]      = object.renderMode;
            particle["ribbonCount"]     = object.ribbonCount;
            particle["ribbonUvScale"]   = object.ribbonUvScale;
            particle["simMode"]         = object.simMode;
            particle["collision"]       = object.collision;
            particle["restitution"]     = object.restitution;
            particle["friction"]        = object.friction;
            particle["collisionThickness"] = object.collisionThickness;
            particle["collisionBias"]   = object.collisionBias;
            particle["restLifetime"]    = object.restLifetime;
            particle["maxSecondaryPerFrame"] = object.maxSecondaryPerFrame;
            if (object.hasSecondary)
            {
                json secondary;
                secondary["maxParticles"]  = object.secondaryMaxParticles;
                secondary["lifetime"]      = json::array({ object.secondaryLifetimeMin, object.secondaryLifetimeMax });
                secondary["startSize"]     = json::array({ object.secondaryStartSizeMin, object.secondaryStartSizeMax });
                secondary["restLifetime"]  = object.secondaryRestLifetime;
                secondary["additiveBlend"] = object.secondaryAdditiveBlend;
                secondary["collision"]     = object.secondaryCollision;
                particle["secondary"]      = std::move(secondary);
            }
            return particle;
        }

        json lightJson(const SceneObjectData& object, const char* kind)
        {
            json light;
            if (kind && kind[0] != '\0')
                light["kind"] = kind;
            light["intensity"]      = object.lightIntensity;
            light["range"]          = object.lightRange;
            light["inner"]          = object.lightInnerDeg;
            light["outer"]          = object.lightOuterDeg;
            light["sourceRadius"]   = object.lightSourceRadius;
            light["enabled"]        = object.lightEnabled;
            light["castShadow"]     = object.lightCastShadow;
            light["useObjectColor"] = true;
            if (object.searchlight)
            {
                light["searchlight"] = true;
                light["sweep"]       = object.sweepDeg;
            }
            if (!object.circuit.empty())
                light["circuit"] = object.circuit;
            return light;
        }
    } // namespace

    std::string sceneComponentOverrides(const SceneObjectData& object)
    {
        json overlay = json::object();
        std::string kind = object.lightKind;
        if (kind.empty() && object.hasLight)
            kind = "point";
        const bool particle = object.hasParticle || sceneHasComponent(object, "ParticleEmitter");
        if (object.hasLight && (kind == "point" || kind == "spot"))
            overlay["LocalLight"] = lightJson(object, kind == "spot" ? "spot" : "point");
        if (object.hasLight && kind == "ambient")
        {
            json ambient;
            ambient["intensity"] = object.lightIntensity;
            ambient["enabled"]   = object.lightEnabled;
            overlay["AmbientLight"] = std::move(ambient);
        }
        if (object.hasLight && kind == "directional")
        {
            json sun;
            sun["intensity"] = object.lightIntensity;
            sun["enabled"]   = object.lightEnabled;
            overlay["DirectionalLight"] = std::move(sun);
        }
        if (object.hasParticle || particle)
            overlay["ParticleEmitter"] = particleJson(object);
        if (object.hasCloud || sceneHasComponent(object, "CloudVolume"))
        {
            json cloud;
            cloud["shape"]           = (object.cloudShape == 0) ? "box" : "ellipsoid";
            cloud["density"]         = object.cloudDensity;
            cloud["coverage"]        = object.cloudCoverage;
            cloud["softness"]        = object.cloudSoftness;
            cloud["absorption"]      = object.cloudAbsorption;
            cloud["scattering"]      = object.cloudScattering;
            cloud["anisotropy"]      = object.cloudAnisotropy;
            cloud["noiseScale"]      = object.cloudNoiseScale;
            cloud["detailScale"]     = object.cloudDetailScale;
            cloud["detailStrength"]  = object.cloudDetailStrength;
            cloud["heightFalloff"]   = object.cloudHeightFalloff;
            cloud["silverLining"]    = object.cloudSilverLining;
            cloud["windSpeed"]       = object.cloudWindSpeed;
            cloud["albedo"]          = vec3Json3(object.cloudAlbedo);
            cloud["windDir"]         = vec3Json3(object.cloudWindDir);
            cloud["enabled"]         = object.cloudEnabled;
            overlay["CloudVolume"]   = std::move(cloud);
        }
        if (object.hasStream || sceneHasComponent(object, "Stream"))
        {
            json stream;
            stream["width"]     = object.streamWidth;
            stream["flowSpeed"] = object.streamFlowSpeed;
            json points = json::array();
            for (const Math::Vector2f& xz : object.streamPoints)
                points.push_back(json::array({ xz.x, xz.y }));
            stream["points"] = std::move(points);
            overlay["Stream"] = std::move(stream);
        }
        if (object.hasSpawner || sceneHasComponent(object, "Spawner"))
        {
            json spawner;
            spawner["mode"] = toString(object.spawner.mode);
            if (!object.spawner.unitArchetype.empty())
                spawner["unit"] = object.spawner.unitArchetype;
            else
                spawner["unit"] = toString(object.spawner.unit);
            spawner["gate"]         = toString(object.spawner.gate);
            spawner["count"]        = object.spawner.count;
            spawner["radius"]       = object.spawner.radius;
            spawner["respawnDelay"] = object.spawner.respawnDelay;
            spawner["waveInterval"] = object.spawner.waveInterval;
            spawner["maxWaves"]     = object.spawner.maxWaves;
            spawner["enabled"]      = object.spawner.enabled;
            spawner["seed"]         = object.spawner.seed;
            overlay["Spawner"]      = std::move(spawner);
        }
        if (sceneHasComponent(object, "Movie") || !object.movie.empty())
        {
            json movie;
            movie["path"]   = object.movie;
            movie["radius"] = object.movieRadius;
            movie["loop"]   = object.movieLoop;
            overlay["Movie"] = std::move(movie);
        }
        if (!object.modelPath.empty())
        {
            json model;
            model["gltf"] = object.modelPath;
            overlay["Model"] = std::move(model);
        }
        if (object.emissive != 0.0f)
        {
            json mesh;
            mesh["emissive"] = object.emissive;
            overlay["Mesh"] = std::move(mesh);
        }
        return overlay.dump();
    }

    void assignSceneParticle(SceneObjectData& object, const ParticleEmitterDesc& desc)
    {
        object.hasParticle     = true;
        object.particleName    = desc.name;
        object.maxParticles    = desc.maxParticles;
        object.emissionRate    = desc.emissionRate;
        object.duration        = desc.duration;
        object.looping         = desc.looping;
        object.lifetimeMin     = desc.lifetime.min;
        object.lifetimeMax     = desc.lifetime.max;
        object.startSpeedMin   = desc.startSpeed.min;
        object.startSpeedMax   = desc.startSpeed.max;
        object.startSizeMin    = desc.startSize.min;
        object.startSizeMax    = desc.startSize.max;
        object.endSizeMin      = desc.endSize.min;
        object.endSizeMax      = desc.endSize.max;
        for (int i = 0; i < 4; ++i)
        {
            object.startColor[i] = desc.startColor[i];
            object.endColor[i]   = desc.endColor[i];
        }
        object.gravity         = desc.gravity;
        object.direction       = desc.direction;
        object.spreadDegrees   = desc.spreadDegrees;
        object.shape           = static_cast<int>(desc.shape);
        object.shapeSize       = desc.shapeSize;
        object.additiveBlend   = desc.additiveBlend;
        object.simulationSpeed = desc.simulationSpeed;
        object.renderMode      = static_cast<int>(desc.renderMode);
        object.ribbonCount     = desc.ribbonCount;
        object.ribbonUvScale   = desc.ribbonUvScale;
        object.simMode         = static_cast<int>(desc.simMode);
        object.collision       = static_cast<int>(desc.collision);
        object.restitution     = desc.restitution;
        object.friction        = desc.friction;
        object.collisionThickness   = desc.collisionThickness;
        object.collisionBias        = desc.collisionBias;
        object.restLifetime         = desc.restLifetime;
        object.maxSecondaryPerFrame = desc.maxSecondaryPerFrame;
        object.hasSecondary         = desc.hasSecondary;
        object.secondaryMaxParticles  = desc.secondary.maxParticles;
        object.secondaryLifetimeMin   = desc.secondary.lifetime.min;
        object.secondaryLifetimeMax   = desc.secondary.lifetime.max;
        object.secondaryStartSizeMin  = desc.secondary.startSize.min;
        object.secondaryStartSizeMax  = desc.secondary.startSize.max;
        object.secondaryRestLifetime  = desc.secondary.restLifetime;
        object.secondaryAdditiveBlend = desc.secondary.additiveBlend;
        object.secondaryCollision     = static_cast<int>(desc.secondary.collision);
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

    using json = nlohmann::json;

    std::string jsonStringField(const json& object, const char* key)
    {
        if (!object.is_object() || !object.contains(key) || !object[key].is_string())
            return {};
        const auto* text = object[key].get_ptr<const json::string_t*>();
        return text ? *text : std::string{};
    }

    std::string sceneLightKind(const SceneObjectData& object)
    {
        if (!object.lightKind.empty())
            return object.lightKind;
        if (const Archetype* archetype = findArchetype(sceneArchetypeId(object)))
        {
            for (const ArchetypeComponent& entry : archetype->components)
            {
                if (entry.name == "AmbientLight")
                    return "ambient";
                if (entry.name == "DirectionalLight")
                    return "directional";
                if (entry.name != "LocalLight")
                    continue;
                const json parsed = json::parse(entry.paramsJson.empty() ? "{}" : entry.paramsJson, nullptr, false);
                if (!parsed.is_discarded() && jsonStringField(parsed, "kind") == "spot")
                    return "spot";
                return "point";
            }
        }
        return {};
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
                return jsonStringField(parsed, "primitive");
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
        if (object.placeholder)
        {
            fail.message = "placeholder has no archetype spawn";
            return {};
        }
        json overlay = json::parse(sceneComponentOverrides(object), nullptr, false);
        if (overlay.is_discarded() || !overlay.is_object())
            overlay = json::object();
        if (!object.overridesJson.empty())
        {
            const json extra = json::parse(object.overridesJson, nullptr, false);
            if (!extra.is_discarded() && extra.is_object())
            {
                for (auto it = extra.begin(); it != extra.end(); ++it)
                    overlay[it.key()] = it.value();
            }
        }
        const std::string text = overlay.empty() ? std::string{} : overlay.dump();
        ArchetypeSpawnPose pose;
        pose.position = object.position;
        pose.rotation = object.rotation;
        pose.scale    = object.scale;
        pose.color[0] = object.color[0];
        pose.color[1] = object.color[1];
        pose.color[2] = object.color[2];
        pose.color[3] = object.color[3];
        return instantiateArchetype(host, sceneArchetypeId(object), pose, text, err);
    }

} // namespace Dark
