#include "Content/ArchetypeSpawn.h"

#include "AI/AiSystem.h"
#include "Content/ArchetypeParams.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Play/CharacterAttach.h"
#include "Play/Spawner.h"

#include "third_party/nlohmann/json.hpp"

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

        void attachListedModel(const ArchetypeSpawnHost& host, Entity entity, const CharacterHost& character)
        {
            const ArchetypeParamsComponent* params = host.world->get<ArchetypeParamsComponent>(entity);
            if (params && !params->modelGltf.empty())
            {
                const ContentModelResult visual = attachContentModel(character, entity, params->modelGltf.c_str(), ModelAttach::AllowStatic);
                if (!visual.attached && host.ensureMesh)
                    host.ensureMesh(host.user, entity);
                return;
            }
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
            ensureLocalPlayerRig(character, entity, nullptr, nullptr);
            return true;
        }

        bool attachProp(const ArchetypeSpawnHost& host, Entity entity, std::string_view, ContentError&)
        {
            if (host.ensureMesh)
                host.ensureMesh(host.user, entity);
            return true;
        }

        bool attachSpawnerBehavior(const ArchetypeSpawnHost& host, Entity entity, std::string_view paramsJson, ContentError& err)
        {
            if (!host.world)
            {
                err.message = "spawner needs a world";
                return false;
            }
            json object;
            if (!readObject(paramsJson, object, err, "spawner"))
                return false;
            SpawnerComponent spawner;
            if (object.contains("unit") && object["unit"].is_string())
                spawner.desc.unitArchetype = object["unit"].get<std::string>();
            clampSpawnerDesc(spawner.desc);
            host.world->emplace<SpawnerComponent>(entity, std::move(spawner));
            return true;
        }

        bool attachNoop(const ArchetypeSpawnHost&, Entity, std::string_view, ContentError&)
        {
            return true;
        }

        void destroySpawn(World& world, Entity entity)
        {
            if (entity.valid() && world.alive(entity))
                world.destroyEntity(entity);
        }
    } // namespace

    void registerBuiltinContent()
    {
        registerComponent(ComponentInfo{ "Model", 1, &readModel, nullptr });
        registerComponent(ComponentInfo{ "Physics", 1, &readPhysics, nullptr });
        registerComponent(ComponentInfo{ "Combat", 1, &readCombat, nullptr });
        registerComponent(ComponentInfo{ "Health", 1, &readHealth, nullptr });
        registerComponent(ComponentInfo{ "Dialogue", 1, &readDialogue, nullptr });
        registerComponent(ComponentInfo{ "Mesh", 1, &readNoted, nullptr });
        registerComponent(ComponentInfo{ "LocalLight", 1, &readNoted, nullptr });
        registerComponent(ComponentInfo{ "ParticleEmitter", 1, &readNoted, nullptr });
        registerComponent(ComponentInfo{ "CloudVolume", 1, &readNoted, nullptr });
        registerComponent(ComponentInfo{ "Stream", 1, &readNoted, nullptr });
        registerComponent(ComponentInfo{ "Spawner", 1, &readNoted, nullptr });
        registerComponent(ComponentInfo{ "Movie", 1, &readNoted, nullptr });
        registerComponent(ComponentInfo{ "Pickup", 1, &readNoted, nullptr });
        registerComponent(ComponentInfo{ "Tag", 1, &readNoted, nullptr });

        registerBehavior(BehaviorInfo{ "prop.solid", &attachProp });
        registerBehavior(BehaviorInfo{ "model", &attachModelBehavior });
        registerBehavior(BehaviorInfo{ "ai.animal", &attachAnimal });
        registerBehavior(BehaviorInfo{ "ai.wolf", &attachAnimal });
        registerBehavior(BehaviorInfo{ "ai.hunter", &attachHunter });
        registerBehavior(BehaviorInfo{ "player.controller", &attachPlayer });
        registerBehavior(BehaviorInfo{ "light.searchlight", &attachNoop });
        registerBehavior(BehaviorInfo{ "pickup", &attachNoop });
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
            const auto            replacement = overrides.find(comp.name);
            const std::string_view text = replacement == overrides.end() ? std::string_view(comp.paramsJson) : std::string_view(replacement->second);
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
