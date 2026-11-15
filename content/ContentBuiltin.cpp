#include "Content/ArchetypeSpawn.h"

#include "AI/AiSystem.h"
#include "Animation/AnimGraphComponent.h"
#include "Content/ArchetypeParams.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/Coin.h"
#include "Gameplay/Platform.h"
#include "Media/MovieCue.h"
#include "Particles/ParticleComponents.h"
#include "Physics/PhysicsComponent.h"
#include "Physics/PhysicsFile.h"
#include "Play/CharacterAttach.h"
#include "Play/Spawner.h"
#include "Scene/EntityMaster.h"
#include "Scene/SceneSpawn.h"
#include "Scene/SceneTypes.h"
#include "Sky/CloudVolume.h"
#include "Water/WaterMarker.h"
#include "Water/WaterStream.h"

#include "third_party/nlohmann/json.hpp"

#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>

namespace Dark
{
    namespace
    {
        using json = nlohmann::json;

        ArchetypeParamsComponent& paramsOf(World& world, Entity entity)
        {
            if (ArchetypeParamsComponent* existing = world.get<ArchetypeParamsComponent>(entity))
                return *existing;
            return world.emplace<ArchetypeParamsComponent>(entity);
        }

        bool readObject(std::string_view jsonText, json& out, ContentError& err, const char* what)
        {
            if (jsonText.empty())
            {
                out = json::object();
                return true;
            }
            out = json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
            if (out.is_discarded() || !out.is_object())
            {
                err.message = std::string(what) + " params are not a JSON object";
                return false;
            }
            return true;
        }

        float jsonFloat(const json& object, const char* key, float fallback)
        {
            const auto it = object.find(key);
            if (it != object.end() && it->is_number())
                return it->get<float>();
            return fallback;
        }

        int jsonInt(const json& object, const char* key, int fallback)
        {
            const auto it = object.find(key);
            if (it != object.end() && it->is_number())
                return it->get<int>();
            return fallback;
        }

        bool jsonBool(const json& object, const char* key, bool fallback)
        {
            const auto it = object.find(key);
            if (it != object.end() && it->is_boolean())
                return it->get<bool>();
            return fallback;
        }

        void jsonColor(const json& object, const char* key, float out[4])
        {
            const auto it = object.find(key);
            if (it == object.end() || !it->is_array())
                return;
            for (size_t i = 0; i < 4 && i < it->size(); ++i)
            {
                if ((*it)[i].is_number())
                    out[i] = (*it)[i].get<float>();
            }
        }

        void jsonVec3(const json& object, const char* key, Math::Vector3f& out)
        {
            const auto it = object.find(key);
            if (it == object.end() || !it->is_array() || it->size() < 3)
                return;
            if ((*it)[0].is_number())
                out.x = (*it)[0].get<float>();
            if ((*it)[1].is_number())
                out.y = (*it)[1].get<float>();
            if ((*it)[2].is_number())
                out.z = (*it)[2].get<float>();
        }

        void jsonRange(const json& object, const char* key, float& minValue, float& maxValue)
        {
            const auto it = object.find(key);
            if (it == object.end() || !it->is_array() || it->size() < 2)
                return;
            if ((*it)[0].is_number())
                minValue = (*it)[0].get<float>();
            if ((*it)[1].is_number())
                maxValue = (*it)[1].get<float>();
        }

        bool readModel(World& world, Entity entity, std::string_view jsonText, ContentError& err)
        {
            json object;
            if (!readObject(jsonText, object, err, "Model"))
                return false;
            ArchetypeParamsComponent& params = paramsOf(world, entity);
            if (object.contains("gltf") && object["gltf"].is_string())
                params.modelGltf = object["gltf"].get<std::string>();
            if (object.contains("anim") && object["anim"].is_string())
                params.modelAnim = object["anim"].get<std::string>();
            return true;
        }

        bool readPhysics(World& world, Entity entity, std::string_view jsonText, ContentError& err)
        {
            json object;
            if (!readObject(jsonText, object, err, "Physics"))
                return false;
            if (object.contains("file") && object["file"].is_string())
                paramsOf(world, entity).physicsFile = object["file"].get<std::string>();
            return true;
        }

        bool readCombat(World& world, Entity entity, std::string_view jsonText, ContentError& err)
        {
            json object;
            if (!readObject(jsonText, object, err, "Combat"))
                return false;
            if (object.contains("attacks") && object["attacks"].is_string())
                paramsOf(world, entity).combatAttacks = object["attacks"].get<std::string>();
            return true;
        }

        bool readHealth(World& world, Entity entity, std::string_view jsonText, ContentError& err)
        {
            json object;
            if (!readObject(jsonText, object, err, "Health"))
                return false;
            const auto it = object.find("max");
            if (it != object.end() && it->is_number())
                paramsOf(world, entity).healthMax = it->get<float>();
            return true;
        }

        bool readDialogue(World& world, Entity entity, std::string_view jsonText, ContentError& err)
        {
            json object;
            if (!readObject(jsonText, object, err, "Dialogue"))
                return false;
            if (object.contains("graph") && object["graph"].is_string())
                paramsOf(world, entity).dialogue = object["graph"].get<std::string>();
            return true;
        }

        bool readMesh(World& world, Entity entity, std::string_view jsonText, ContentError& err)
        {
            json object;
            if (!readObject(jsonText, object, err, "Mesh"))
                return false;
            ArchetypeParamsComponent& params = paramsOf(world, entity);
            params.meshJson = std::string(jsonText);
            if (object.contains("primitive") && object["primitive"].is_string())
                params.meshPrimitive = object["primitive"].get<std::string>();
            params.emissive = jsonFloat(object, "emissive", params.emissive);
            return true;
        }

        bool readLocalLight(World& world, Entity entity, std::string_view jsonText, ContentError& err)
        {
            json object;
            if (!readObject(jsonText, object, err, "LocalLight"))
                return false;
            ArchetypeParamsComponent& params = paramsOf(world, entity);
            params.localLightJson = std::string(jsonText);
            params.lightUsesObjectColor = jsonBool(object, "useObjectColor", params.lightUsesObjectColor);
            if (object.contains("circuit") && object["circuit"].is_string())
                params.circuit = object["circuit"].get<std::string>();
            return true;
        }

        bool readStored(std::string ArchetypeParamsComponent::* member, const char* what, World& world, Entity entity, std::string_view jsonText, ContentError& err)
        {
            json object;
            if (!readObject(jsonText, object, err, what))
                return false;
            paramsOf(world, entity).*member = std::string(jsonText);
            return true;
        }

        bool readAmbient(World& world, Entity entity, std::string_view jsonText, ContentError& err)
        {
            return readStored(&ArchetypeParamsComponent::ambientJson, "AmbientLight", world, entity, jsonText, err);
        }

        bool readDirectional(World& world, Entity entity, std::string_view jsonText, ContentError& err)
        {
            return readStored(&ArchetypeParamsComponent::directionalJson, "DirectionalLight", world, entity, jsonText, err);
        }

        bool readParticle(World& world, Entity entity, std::string_view jsonText, ContentError& err)
        {
            return readStored(&ArchetypeParamsComponent::particleJson, "ParticleEmitter", world, entity, jsonText, err);
        }

        bool readCloud(World& world, Entity entity, std::string_view jsonText, ContentError& err)
        {
            return readStored(&ArchetypeParamsComponent::cloudJson, "CloudVolume", world, entity, jsonText, err);
        }

        bool readStream(World& world, Entity entity, std::string_view jsonText, ContentError& err)
        {
            return readStored(&ArchetypeParamsComponent::streamJson, "Stream", world, entity, jsonText, err);
        }

        bool readSpawner(World& world, Entity entity, std::string_view jsonText, ContentError& err)
        {
            return readStored(&ArchetypeParamsComponent::spawnerJson, "Spawner", world, entity, jsonText, err);
        }

        bool readMovie(World& world, Entity entity, std::string_view jsonText, ContentError& err)
        {
            return readStored(&ArchetypeParamsComponent::movieJson, "Movie", world, entity, jsonText, err);
        }

        bool readPickup(World& world, Entity entity, std::string_view jsonText, ContentError& err)
        {
            json object;
            if (!readObject(jsonText, object, err, "Pickup"))
                return false;
            ArchetypeParamsComponent& params = paramsOf(world, entity);
            if (object.contains("item") && object["item"].is_string())
                params.pickupItem = object["item"].get<std::string>();
            params.pickupCount = jsonInt(object, "count", params.pickupCount);
            return true;
        }

        bool readWater(World& world, Entity entity, std::string_view jsonText, ContentError& err)
        {
            json object;
            if (!readObject(jsonText, object, err, "Water"))
                return false;
            paramsOf(world, entity).hasWater = true;
            return true;
        }

        bool readNoted(World&, Entity, std::string_view jsonText, ContentError& err)
        {
            json object;
            return readObject(jsonText, object, err, "component");
        }

        bool characterReady(const ArchetypeSpawnHost& host, ContentError& err)
        {
            if (host.world && host.assets && host.pins && host.renderer && host.audio && host.ground)
                return true;
            err.message = "behavior needs world, assets, renderer, audio, and ground";
            return false;
        }

        const ArchetypeParamsComponent* paramsOrNull(const ArchetypeSpawnHost& host, Entity entity)
        {
            if (!host.world)
                return nullptr;
            return host.world->get<ArchetypeParamsComponent>(entity);
        }

        void attachListedModel(const ArchetypeSpawnHost& host, Entity entity, const CharacterHost& character)
        {
            const ArchetypeParamsComponent* params = host.world->get<ArchetypeParamsComponent>(entity);
            if (params && !params->modelGltf.empty())
            {
                const ModelAttach mode = host.modelAnimatedOnly ? ModelAttach::AnimatedOnly : ModelAttach::AllowStatic;
                const ContentModelResult visual = attachContentModel(character, entity, params->modelGltf.c_str(), mode);
                const bool skinnedOk = visual.attached && (!host.modelAnimatedOnly || visual.skinned);
                if (skinnedOk)
                    return;
                if (host.modelAnimatedOnly)
                    return;
                if (host.ensureMesh)
                    host.ensureMesh(host.user, entity);
                return;
            }
            if (host.modelAnimatedOnly)
                return;
            if (host.ensureMesh)
                host.ensureMesh(host.user, entity);
        }

        bool attachKind(const ArchetypeSpawnHost& host, Entity entity, NpcKind kind, ContentError& err)
        {
            if (!characterReady(host, err))
                return false;
            if (host.ai && !host.ai->attachHunter(*host.world, entity, *host.pins, *host.assets))
            {
                err.message = "ai brain failed to attach";
                return false;
            }
            const CharacterHost character{ *host.world, *host.renderer, *host.assets, *host.pins, *host.audio, *host.ground };
            const ArchetypeParamsComponent* params = host.world->get<ArchetypeParamsComponent>(entity);
            const float hp = (params && params->healthMax > 0.0f) ? params->healthMax : (kind == NpcKind::Wolf ? 60.0f : 100.0f);
            ensureHumanoidBody(*host.world, entity, hp, Math::Vector3f{ 0.35f, 0.7f, 0.35f });
            attachListedModel(host, entity, character);
            configureNpc(character, entity, kind);
            return true;
        }

        bool attachAnimal(const ArchetypeSpawnHost& host, Entity entity, std::string_view, ContentError& err)
        {
            return attachKind(host, entity, NpcKind::Wolf, err);
        }

        bool attachHunter(const ArchetypeSpawnHost& host, Entity entity, std::string_view, ContentError& err)
        {
            return attachKind(host, entity, NpcKind::Hunter, err);
        }

        bool attachModelBehavior(const ArchetypeSpawnHost& host, Entity entity, std::string_view, ContentError& err)
        {
            if (!characterReady(host, err))
                return false;
            const CharacterHost character{ *host.world, *host.renderer, *host.assets, *host.pins, *host.audio, *host.ground };
            attachListedModel(host, entity, character);
            if (host.modelAnimatedOnly)
            {
                const bool skinned = host.world->has<AnimGraphComponent>(entity) && host.world->has<ModelComponent>(entity);
                if (!skinned)
                {
                    err.message = "model is not skinned";
                    return false;
                }
            }
            return true;
        }

        bool attachPlayer(const ArchetypeSpawnHost& host, Entity entity, std::string_view, ContentError& err)
        {
            if (!characterReady(host, err))
                return false;
            const CharacterHost character{ *host.world, *host.renderer, *host.assets, *host.pins, *host.audio, *host.ground };
            const ArchetypeParamsComponent* params = host.world->get<ArchetypeParamsComponent>(entity);
            const float hp = (params && params->healthMax > 0.0f) ? params->healthMax : 200.0f;
            ensureHumanoidBody(*host.world, entity, hp, Math::Vector3f{ 0.35f, 0.7f, 0.35f });
            attachListedModel(host, entity, character);
            ensureLocalPlayerRig(character, entity, host.onWeaponHit, host.weaponHitUser);
            return true;
        }

        void loadPhysicsCard(World& world, Entity entity, const std::string& file)
        {
            if (file.empty() || world.has<PhysicsComponent>(entity))
                return;
            const std::filesystem::path path = resolveContentFile(file);
            if (path.empty())
                return;
            PhysicsComponent loaded;
            if (!Physics::loadPhysicsSettingsFile(path, loaded))
            {
                DE_LOG_WARN(LogCategory::Collision, "Content: physics '{}' failed", file);
                return;
            }
            world.emplace<PhysicsComponent>(entity, std::move(loaded));
        }

        void paintEmissive(World& world, Entity entity, float emissive)
        {
            if (emissive == 0.0f)
                return;
            if (MeshComponent* mesh = world.get<MeshComponent>(entity))
                mesh->emissive = emissive;
        }

        bool attachProp(const ArchetypeSpawnHost& host, Entity entity, std::string_view, ContentError&)
        {
            if (!host.world)
                return true;
            const ArchetypeParamsComponent* params = paramsOrNull(host, entity);
            const char* primitive = (params && !params->meshPrimitive.empty()) ? params->meshPrimitive.c_str() : "cube";
            if (host.ensurePrimitive)
                host.ensurePrimitive(host.user, entity, primitive);
            else if (host.ensureMesh)
                host.ensureMesh(host.user, entity);
            if (params)
            {
                loadPhysicsCard(*host.world, entity, params->physicsFile);
                paintEmissive(*host.world, entity, params->emissive);
            }
            return true;
        }

        void fillSpawner(const json& object, SpawnerDesc& desc)
        {
            if (object.contains("mode") && object["mode"].is_string())
            {
                SpawnerMode mode{};
                if (tryParseSpawnerMode(object["mode"].get<std::string>(), mode))
                    desc.mode = mode;
            }
            if (object.contains("unit") && object["unit"].is_string())
            {
                const std::string text = object["unit"].get<std::string>();
                if (!text.empty())
                {
                    desc.unitArchetype = text;
                    std::string_view bare = text;
                    const auto colon = text.rfind(':');
                    if (colon != std::string::npos)
                        bare = std::string_view(text).substr(colon + 1);
                    SpawnerUnit unit{};
                    if (tryParseSpawnerUnit(bare, unit))
                        desc.unit = unit;
                }
            }
            if (object.contains("gate") && object["gate"].is_string())
            {
                SpawnerWaveGate gate{};
                if (tryParseSpawnerWaveGate(object["gate"].get<std::string>(), gate))
                    desc.gate = gate;
            }
            desc.count        = jsonInt(object, "count", desc.count);
            desc.radius       = jsonFloat(object, "radius", desc.radius);
            desc.respawnDelay = jsonFloat(object, "respawnDelay", desc.respawnDelay);
            desc.waveInterval = jsonFloat(object, "waveInterval", desc.waveInterval);
            desc.maxWaves     = jsonInt(object, "maxWaves", desc.maxWaves);
            desc.enabled      = jsonBool(object, "enabled", desc.enabled);
            const auto seed = object.find("seed");
            if (seed != object.end() && seed->is_number())
            {
                const double value = seed->get<double>();
                if (value >= 1.0 && value <= 4294967295.0)
                    desc.seed = static_cast<uint32_t>(value);
            }
            clampSpawnerDesc(desc);
        }

        bool attachSpawnerBehavior(const ArchetypeSpawnHost& host, Entity entity, std::string_view paramsJson, ContentError& err)
        {
            if (!host.world)
            {
                err.message = "spawner needs a world";
                return false;
            }
            const ArchetypeParamsComponent* params = paramsOrNull(host, entity);
            const std::string_view text = (params && !params->spawnerJson.empty()) ? std::string_view(params->spawnerJson) : paramsJson;
            json object;
            if (!readObject(text, object, err, "spawner"))
                return false;
            SpawnerComponent spawner;
            fillSpawner(object, spawner.desc);
            if (spawner.desc.unitArchetype.empty() && object.empty())
            {
                json behavior;
                if (!readObject(paramsJson, behavior, err, "spawner"))
                    return false;
                fillSpawner(behavior, spawner.desc);
            }
            clampSpawnerDesc(spawner.desc);
            host.world->emplace<SpawnerComponent>(entity, std::move(spawner));
            if (host.ensureMesh)
                host.ensureMesh(host.user, entity);
            if (host.ensureMarker)
                host.ensureMarker(host.user, entity);
            return true;
        }

        void fillLocalLight(const json& object, const ArchetypeParamsComponent* params, LocalLightComponent& light)
        {
            const bool spot = object.contains("kind") && object["kind"].is_string() && object["kind"].get<std::string>() == "spot";
            light.type = spot ? LocalLightType::Spot : LocalLightType::Point;
            if (spot)
            {
                light.intensity = 2513.0f;
                light.range     = 16.0f;
            }
            light.intensity    = jsonFloat(object, "intensity", light.intensity);
            light.range        = jsonFloat(object, "range", light.range);
            light.innerConeDeg = jsonFloat(object, "inner", light.innerConeDeg);
            light.outerConeDeg = jsonFloat(object, "outer", light.outerConeDeg);
            light.sourceRadius = jsonFloat(object, "sourceRadius", light.sourceRadius);
            light.enabled      = jsonBool(object, "enabled", light.enabled);
            light.castShadow   = jsonBool(object, "castShadow", light.castShadow);
            if (params)
                light.color = Math::Vector3f{ params->color[0], params->color[1], params->color[2] };
        }

        bool attachLocalLight(const ArchetypeSpawnHost& host, Entity entity, std::string_view, ContentError& err)
        {
            if (!host.world)
            {
                err.message = "light needs a world";
                return false;
            }
            const ArchetypeParamsComponent* params = paramsOrNull(host, entity);
            json object;
            const std::string_view text = params ? std::string_view(params->localLightJson) : std::string_view{};
            if (!readObject(text, object, err, "LocalLight"))
                return false;
            LocalLightComponent light{};
            fillLocalLight(object, params, light);
            host.world->emplace<LocalLightComponent>(entity, light);

            const bool searchlight = jsonBool(object, "searchlight", false);
            const float sweep = jsonFloat(object, "sweep", 60.0f);
            const std::string circuit = (object.contains("circuit") && object["circuit"].is_string())
                ? object["circuit"].get<std::string>()
                : (params ? params->circuit : std::string{});
            if (searchlight || !circuit.empty())
            {
                SceneObjectData probe;
                probe.lightKind      = light.type == LocalLightType::Spot ? "spot" : "point";
                probe.hasLight       = true;
                probe.searchlight    = searchlight && light.type == LocalLightType::Spot;
                probe.sweepDeg       = sweep;
                probe.circuit        = circuit;
                probe.lightRange     = light.range;
                probe.lightOuterDeg  = light.outerConeDeg;
                probe.lightIntensity = light.intensity;
                if (const TransformComponent* xf = host.world->get<TransformComponent>(entity))
                {
                    probe.position = xf->position;
                    probe.rotation = xf->rotation;
                }
                applySceneActor(*host.world, entity, probe, nullptr, nullptr);
            }
            return true;
        }

        bool attachAmbient(const ArchetypeSpawnHost& host, Entity entity, std::string_view, ContentError& err)
        {
            if (!host.world)
            {
                err.message = "light needs a world";
                return false;
            }
            const ArchetypeParamsComponent* params = paramsOrNull(host, entity);
            json object;
            if (!readObject(params ? std::string_view(params->ambientJson) : std::string_view{}, object, err, "AmbientLight"))
                return false;
            AmbientLightComponent ambient{};
            ambient.intensity = jsonFloat(object, "intensity", 1.0f);
            ambient.enabled   = jsonBool(object, "enabled", true);
            if (params)
                ambient.color = Math::Vector3f{ params->color[0], params->color[1], params->color[2] };
            host.world->emplace<AmbientLightComponent>(entity, ambient);
            return true;
        }

        bool attachDirectional(const ArchetypeSpawnHost& host, Entity entity, std::string_view, ContentError& err)
        {
            if (!host.world)
            {
                err.message = "light needs a world";
                return false;
            }
            const ArchetypeParamsComponent* params = paramsOrNull(host, entity);
            json object;
            if (!readObject(params ? std::string_view(params->directionalJson) : std::string_view{}, object, err, "DirectionalLight"))
                return false;
            DirectionalLightComponent sun{};
            sun.intensity = jsonFloat(object, "intensity", Math::Pi);
            sun.enabled   = jsonBool(object, "enabled", true);
            if (params)
                sun.color = Math::Vector3f{ params->color[0], params->color[1], params->color[2] };
            host.world->emplace<DirectionalLightComponent>(entity, sun);
            return true;
        }

        void fillParticle(const json& object, ParticleEmitterDesc& desc)
        {
            if (object.contains("name") && object["name"].is_string())
                desc.name = object["name"].get<std::string>();
            desc.maxParticles    = static_cast<uint32_t>(jsonInt(object, "maxParticles", static_cast<int>(desc.maxParticles)));
            desc.emissionRate    = jsonFloat(object, "emissionRate", desc.emissionRate);
            desc.duration        = jsonFloat(object, "duration", desc.duration);
            desc.looping         = jsonBool(object, "looping", desc.looping);
            jsonRange(object, "lifetime", desc.lifetime.min, desc.lifetime.max);
            jsonRange(object, "startSpeed", desc.startSpeed.min, desc.startSpeed.max);
            jsonRange(object, "startSize", desc.startSize.min, desc.startSize.max);
            jsonRange(object, "endSize", desc.endSize.min, desc.endSize.max);
            jsonColor(object, "startColor", desc.startColor);
            jsonColor(object, "endColor", desc.endColor);
            jsonVec3(object, "gravity", desc.gravity);
            jsonVec3(object, "direction", desc.direction);
            desc.spreadDegrees   = jsonFloat(object, "spreadDegrees", desc.spreadDegrees);
            desc.shape           = static_cast<ParticleEmitterDesc::Shape>(jsonInt(object, "shape", static_cast<int>(desc.shape)));
            jsonVec3(object, "shapeSize", desc.shapeSize);
            desc.additiveBlend   = jsonBool(object, "additiveBlend", desc.additiveBlend);
            desc.simulationSpeed = jsonFloat(object, "simulationSpeed", desc.simulationSpeed);
            desc.renderMode      = static_cast<ParticleEmitterDesc::RenderMode>(jsonInt(object, "renderMode", static_cast<int>(desc.renderMode)));
            desc.ribbonCount     = static_cast<uint32_t>(jsonInt(object, "ribbonCount", static_cast<int>(desc.ribbonCount)));
            desc.ribbonUvScale   = jsonFloat(object, "ribbonUvScale", desc.ribbonUvScale);
            desc.simMode         = static_cast<ParticleSimMode>(jsonInt(object, "simMode", static_cast<int>(desc.simMode)));
            desc.collision       = static_cast<ParticleCollisionReaction>(jsonInt(object, "collision", static_cast<int>(desc.collision)));
            desc.restitution     = jsonFloat(object, "restitution", desc.restitution);
            desc.friction        = jsonFloat(object, "friction", desc.friction);
            desc.collisionThickness   = jsonFloat(object, "collisionThickness", desc.collisionThickness);
            desc.collisionBias        = jsonFloat(object, "collisionBias", desc.collisionBias);
            desc.restLifetime         = jsonFloat(object, "restLifetime", desc.restLifetime);
            desc.maxSecondaryPerFrame = static_cast<uint32_t>(jsonInt(object, "maxSecondaryPerFrame", static_cast<int>(desc.maxSecondaryPerFrame)));
            const auto secondary = object.find("secondary");
            if (secondary != object.end() && secondary->is_object())
            {
                desc.hasSecondary = true;
                desc.secondary.maxParticles = static_cast<uint32_t>(jsonInt(*secondary, "maxParticles", static_cast<int>(desc.secondary.maxParticles)));
                jsonRange(*secondary, "lifetime", desc.secondary.lifetime.min, desc.secondary.lifetime.max);
                jsonRange(*secondary, "startSize", desc.secondary.startSize.min, desc.secondary.startSize.max);
                desc.secondary.restLifetime  = jsonFloat(*secondary, "restLifetime", desc.secondary.restLifetime);
                desc.secondary.additiveBlend = jsonBool(*secondary, "additiveBlend", desc.secondary.additiveBlend);
                desc.secondary.collision     = static_cast<ParticleCollisionReaction>(jsonInt(*secondary, "collision", static_cast<int>(desc.secondary.collision)));
            }
        }

        bool attachParticle(const ArchetypeSpawnHost& host, Entity entity, std::string_view, ContentError& err)
        {
            if (!host.world)
            {
                err.message = "particle needs a world";
                return false;
            }
            const ArchetypeParamsComponent* params = paramsOrNull(host, entity);
            json object;
            if (!readObject(params ? std::string_view(params->particleJson) : std::string_view{}, object, err, "ParticleEmitter"))
                return false;
            ParticleEmitterDesc desc{};
            fillParticle(object, desc);
            ParticleEmitterComponent emitter;
            emitter.desc    = desc;
            emitter.playing = true;
            host.world->emplace<ParticleEmitterComponent>(entity, std::move(emitter));
            const bool ribbon = desc.renderMode == ParticleEmitterDesc::RenderMode::Ribbon;
            if (host.bindParticleMaterial)
                host.bindParticleMaterial(host.user, entity, ribbon);
            if (ParticleEmitterComponent* spawned = host.world->get<ParticleEmitterComponent>(entity))
            {
                ensureParticleRuntime(*spawned);
                if (const TransformComponent* xf = host.world->get<TransformComponent>(entity); xf && spawned->runtime)
                    spawned->runtime->setTransform(xf->position, xf->rotation);
            }
            if (LocalLightComponent* light = host.world->get<LocalLightComponent>(entity))
            {
                const bool keepObjectColor = params && params->lightUsesObjectColor;
                if (!keepObjectColor)
                    light->color = Math::Vector3f{ desc.startColor[0], desc.startColor[1], desc.startColor[2] };
            }
            return true;
        }

        void fillCloud(const json& object, CloudVolumeDesc& desc)
        {
            if (object.contains("shape"))
            {
                if (object["shape"].is_string())
                {
                    CloudShape shape{};
                    if (tryParseCloudShape(object["shape"].get<std::string>(), shape))
                        desc.shape = shape;
                }
                else if (object["shape"].is_number() && object["shape"].get<int>() == 0)
                    desc.shape = CloudShape::Box;
            }
            desc.density        = jsonFloat(object, "density", desc.density);
            desc.coverage       = jsonFloat(object, "coverage", desc.coverage);
            desc.softness       = jsonFloat(object, "softness", desc.softness);
            desc.absorption     = jsonFloat(object, "absorption", desc.absorption);
            desc.scattering     = jsonFloat(object, "scattering", desc.scattering);
            desc.anisotropy     = jsonFloat(object, "anisotropy", desc.anisotropy);
            desc.noiseScale     = jsonFloat(object, "noiseScale", desc.noiseScale);
            desc.detailScale    = jsonFloat(object, "detailScale", desc.detailScale);
            desc.detailStrength = jsonFloat(object, "detailStrength", desc.detailStrength);
            desc.heightFalloff  = jsonFloat(object, "heightFalloff", desc.heightFalloff);
            desc.silverLining   = jsonFloat(object, "silverLining", desc.silverLining);
            desc.windSpeed      = jsonFloat(object, "windSpeed", desc.windSpeed);
            desc.enabled        = jsonBool(object, "enabled", desc.enabled);
            jsonVec3(object, "albedo", desc.albedo);
            jsonVec3(object, "windDir", desc.windDir);
        }

        bool attachCloud(const ArchetypeSpawnHost& host, Entity entity, std::string_view, ContentError& err)
        {
            if (!host.world)
            {
                err.message = "cloud needs a world";
                return false;
            }
            const ArchetypeParamsComponent* params = paramsOrNull(host, entity);
            json object;
            if (!readObject(params ? std::string_view(params->cloudJson) : std::string_view{}, object, err, "CloudVolume"))
                return false;
            CloudVolumeComponent cloud{};
            fillCloud(object, cloud.desc);
            host.world->emplace<CloudVolumeComponent>(entity, cloud);
            return true;
        }

        bool attachStream(const ArchetypeSpawnHost& host, Entity entity, std::string_view, ContentError& err)
        {
            if (!host.world)
            {
                err.message = "stream needs a world";
                return false;
            }
            const ArchetypeParamsComponent* params = paramsOrNull(host, entity);
            json object;
            if (!readObject(params ? std::string_view(params->streamJson) : std::string_view{}, object, err, "Stream"))
                return false;
            StreamComponent stream;
            stream.width     = jsonFloat(object, "width", stream.width);
            stream.flowSpeed = jsonFloat(object, "flowSpeed", stream.flowSpeed);
            const auto points = object.find("points");
            if (points != object.end() && points->is_array())
            {
                for (const json& point : *points)
                {
                    if (!point.is_array() || point.size() < 2 || !point[0].is_number() || !point[1].is_number())
                        continue;
                    stream.points.emplace_back(point[0].get<float>(), point[1].get<float>());
                }
            }
            if (stream.points.size() < 2)
            {
                const TransformComponent* xf = host.world->get<TransformComponent>(entity);
                const float x = xf ? xf->position.x : 0.0f;
                const float z = xf ? xf->position.z : 0.0f;
                stream.points.clear();
                stream.points.emplace_back(x, z);
                stream.points.emplace_back(x, z - 2.0f);
            }
            stream.snapToPoints = true;
            host.world->emplace<StreamComponent>(entity, std::move(stream));
            return true;
        }

        bool attachMovie(const ArchetypeSpawnHost& host, Entity entity, std::string_view, ContentError& err)
        {
            if (!host.world)
            {
                err.message = "movie needs a world";
                return false;
            }
            const ArchetypeParamsComponent* params = paramsOrNull(host, entity);
            json object;
            if (!readObject(params ? std::string_view(params->movieJson) : std::string_view{}, object, err, "Movie"))
                return false;
            SceneObjectData cue;
            if (object.contains("path") && object["path"].is_string())
                cue.movie = object["path"].get<std::string>();
            cue.movieRadius = jsonFloat(object, "radius", 2.0f);
            cue.movieLoop   = jsonBool(object, "loop", false);
            applyMovieCue(*host.world, entity, cue);
            if (host.ensureMesh)
                host.ensureMesh(host.user, entity);
            if (host.ensureMarker)
                host.ensureMarker(host.user, entity);
            return true;
        }

        bool attachWater(const ArchetypeSpawnHost& host, Entity entity, std::string_view, ContentError& err)
        {
            if (!host.world)
            {
                err.message = "water needs a world";
                return false;
            }
            host.world->emplace<WaterMarkerComponent>(entity);
            return true;
        }

        bool attachPickup(const ArchetypeSpawnHost& host, Entity entity, std::string_view, ContentError& err)
        {
            if (!host.world)
            {
                err.message = "pickup needs a world";
                return false;
            }
            const ArchetypeParamsComponent* params = paramsOrNull(host, entity);
            if (params && !params->pickupItem.empty())
                return true;
            CoinComponent coin{};
            if (const TransformComponent* xf = host.world->get<TransformComponent>(entity))
                coin.pos = Math::Vector2f{ xf->position.x, xf->position.y };
            host.world->emplace<CoinComponent>(entity, coin);
            if (host.ensureMesh)
                host.ensureMesh(host.user, entity);
            return true;
        }

        bool attachPlatform(const ArchetypeSpawnHost& host, Entity entity, std::string_view, ContentError& err)
        {
            if (!host.world)
            {
                err.message = "platform needs a world";
                return false;
            }
            host.world->emplace<PlatformMarkerComponent>(entity);
            return true;
        }

        bool attachSpawnPoint(const ArchetypeSpawnHost& host, Entity entity, std::string_view, ContentError& err)
        {
            if (!host.world)
            {
                err.message = "spawn needs a world";
                return false;
            }
            host.world->emplace<SpawnPointComponent>(entity);
            if (host.ensureMesh)
                host.ensureMesh(host.user, entity);
            return true;
        }

        void destroySpawn(World& world, Entity entity)
        {
            if (entity.valid() && world.alive(entity))
                world.destroyEntity(entity);
        }

        std::string mergeComponentJson(std::string_view baseText, const std::string& overlayText, ContentError& err)
        {
            json base = json::parse(baseText.empty() ? "{}" : std::string(baseText), nullptr, false);
            json over = json::parse(overlayText, nullptr, false);
            if (base.is_discarded() || !base.is_object())
                base = json::object();
            if (over.is_discarded() || !over.is_object())
            {
                err.message = "component override is not a JSON object";
                return {};
            }
            for (auto it = over.begin(); it != over.end(); ++it)
                base[it.key()] = it.value();
            return base.dump();
        }
    } // namespace

    void registerBuiltinContent()
    {
        registerComponent(ComponentInfo{ "Model", 1, &readModel, nullptr });
        registerComponent(ComponentInfo{ "Physics", 1, &readPhysics, nullptr });
        registerComponent(ComponentInfo{ "Combat", 1, &readCombat, nullptr });
        registerComponent(ComponentInfo{ "Health", 1, &readHealth, nullptr });
        registerComponent(ComponentInfo{ "Dialogue", 1, &readDialogue, nullptr });
        registerComponent(ComponentInfo{ "Mesh", 1, &readMesh, nullptr });
        registerComponent(ComponentInfo{ "LocalLight", 1, &readLocalLight, nullptr });
        registerComponent(ComponentInfo{ "AmbientLight", 1, &readAmbient, nullptr });
        registerComponent(ComponentInfo{ "DirectionalLight", 1, &readDirectional, nullptr });
        registerComponent(ComponentInfo{ "ParticleEmitter", 1, &readParticle, nullptr });
        registerComponent(ComponentInfo{ "CloudVolume", 1, &readCloud, nullptr });
        registerComponent(ComponentInfo{ "Stream", 1, &readStream, nullptr });
        registerComponent(ComponentInfo{ "Spawner", 1, &readSpawner, nullptr });
        registerComponent(ComponentInfo{ "Movie", 1, &readMovie, nullptr });
        registerComponent(ComponentInfo{ "Pickup", 1, &readPickup, nullptr });
        registerComponent(ComponentInfo{ "Water", 1, &readWater, nullptr });
        registerComponent(ComponentInfo{ "Platform", 1, &readNoted, nullptr });
        registerComponent(ComponentInfo{ "Spawn", 1, &readNoted, nullptr });
        registerComponent(ComponentInfo{ "Tag", 1, &readNoted, nullptr });

        registerBehavior(BehaviorInfo{ "prop.solid", &attachProp });
        registerBehavior(BehaviorInfo{ "model", &attachModelBehavior });
        registerBehavior(BehaviorInfo{ "ai.animal", &attachAnimal });
        registerBehavior(BehaviorInfo{ "ai.wolf", &attachAnimal });
        registerBehavior(BehaviorInfo{ "ai.hunter", &attachHunter });
        registerBehavior(BehaviorInfo{ "player.controller", &attachPlayer });
        registerBehavior(BehaviorInfo{ "light.searchlight", &attachLocalLight });
        registerBehavior(BehaviorInfo{ "light.ambient", &attachAmbient });
        registerBehavior(BehaviorInfo{ "light.directional", &attachDirectional });
        registerBehavior(BehaviorInfo{ "particle.emit", &attachParticle });
        registerBehavior(BehaviorInfo{ "cloud.volume", &attachCloud });
        registerBehavior(BehaviorInfo{ "stream.ribbon", &attachStream });
        registerBehavior(BehaviorInfo{ "movie.cue", &attachMovie });
        registerBehavior(BehaviorInfo{ "water.volume", &attachWater });
        registerBehavior(BehaviorInfo{ "pickup", &attachPickup });
        registerBehavior(BehaviorInfo{ "platform.marker", &attachPlatform });
        registerBehavior(BehaviorInfo{ "spawn.point", &attachSpawnPoint });
        registerBehavior(BehaviorInfo{ "spawner", &attachSpawnerBehavior });
    }

    Entity instantiateArchetype(const ArchetypeSpawnHost& host, std::string_view archetypeId, const ArchetypeSpawnPose& pose,
                                std::string_view overridesJson, ContentError* err)
    {
        ContentError local;
        ContentError& fail = err ? *err : local;
        if (!host.world)
        {
            fail.message = "instantiateArchetype needs a world";
            return {};
        }
        const Archetype* archetype = findArchetype(archetypeId);
        if (!archetype)
        {
            fail.message = "unknown archetype '" + std::string(archetypeId) + "'";
            return {};
        }

        World& world = *host.world;
        const Entity entity = world.createEntity();
        world.emplace<TagComponent>(entity, archetype->editor.label.empty() ? archetype->name : archetype->editor.label);
        TransformComponent xf;
        xf.position = pose.position;
        xf.rotation = pose.rotation;
        xf.scale    = pose.scale;
        world.emplace<TransformComponent>(entity, xf);
        ArchetypeParamsComponent params;
        params.archetype = archetype->name;
        params.replicate = archetype->replicate;
        params.color[0]  = pose.color[0];
        params.color[1]  = pose.color[1];
        params.color[2]  = pose.color[2];
        params.color[3]  = pose.color[3];
        world.emplace<ArchetypeParamsComponent>(entity, std::move(params));

        std::unordered_map<std::string, std::string> overrides;
        if (!overridesJson.empty())
        {
            const json overlay = json::parse(overridesJson.begin(), overridesJson.end(), nullptr, false);
            if (overlay.is_discarded() || !overlay.is_object())
            {
                fail.message = "overrides are not a JSON object";
                destroySpawn(world, entity);
                return {};
            }
            for (auto it = overlay.begin(); it != overlay.end(); ++it)
                overrides.emplace(it.key(), it.value().dump());
        }

        for (const ArchetypeComponent& comp : archetype->components)
        {
            const ComponentInfo* info = findComponent(comp.name);
            const auto replacement = overrides.find(comp.name);
            std::string merged;
            std::string_view text = comp.paramsJson;
            if (replacement != overrides.end())
            {
                merged = mergeComponentJson(comp.paramsJson, replacement->second, fail);
                if (merged.empty() && !fail.message.empty())
                {
                    destroySpawn(world, entity);
                    return {};
                }
                text = merged;
            }
            if (!info || !info->read || !info->read(world, entity, text, fail))
            {
                if (fail.message.empty())
                    fail.message = "component '" + comp.name + "' failed";
                destroySpawn(world, entity);
                return {};
            }
        }

        for (const ArchetypeBehavior& behavior : archetype->behaviors)
        {
            const BehaviorInfo* info = findBehavior(behavior.name);
            if (!info || !info->attach(host, entity, behavior.paramsJson, fail))
            {
                if (fail.message.empty())
                    fail.message = "behavior '" + behavior.name + "' failed";
                DE_LOG_WARN(LogCategory::Core, "Content: {}", fail.message);
                destroySpawn(world, entity);
                return {};
            }
        }
        return entity;
    }

} // namespace Dark
