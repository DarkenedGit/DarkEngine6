#include "Play/Spawner.h"

#include "AI/AiSystem.h"
#include "Content/ArchetypeSpawn.h"
#include "Content/ContentDb.h"
#include "Content/ContentId.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Play/CharacterAttach.h"
#include "Play/DialogueBinding.h"
#include "Play/DialogueMemory.h"
#include "Play/PlaySession.h"
#include "Play/SpawnHost.h"
#include "Scene/EntityMaster.h"
#include "Terrain/TerrainGrid.h"

namespace Dark
{

    namespace
    {

        void bindListedNpcDialogue(World& world, Entity npc)
        {
            if (!npc.valid() || !world.alive(npc))
                return;
            std::string path = loadEntityMasterType("hunter").dialogue;
            if (path.empty())
                return;
            if (DialogueBindingComponent* existing = world.get<DialogueBindingComponent>(npc))
                existing->graph = std::move(path);
            else
            {
                DialogueBindingComponent binding;
                binding.graph = std::move(path);
                world.emplace<DialogueBindingComponent>(npc, std::move(binding));
            }
            if (!world.get<DialogueMemoryComponent>(npc))
                world.emplace<DialogueMemoryComponent>(npc);
        }

        float listedGroundY(const ListedSpawnHost& host, float x, float z)
        {
            if (host.terrain && host.terrain->valid())
                return host.terrain->heightAtWorld(x, z);
            return 0.0f;
        }

    } // namespace

    std::string resolveListedArchetype(std::string_view unitArchetype)
    {
        const std::string_view text = unitArchetype.empty() ? std::string_view("hunter") : unitArchetype;
        if (const Archetype* archetype = findArchetype(text))
            return archetype->name;
        return canonicalContentId(text);
    }

    Entity spawnListedUnit(const ListedSpawnHost& host, Entity spawner, const SpawnerRequest& req)
    {
        if (!host.world || !host.renderer || !host.assets || !host.pins || !host.audio || !host.ground)
            return {};
        World& world = *host.world;
        const std::string archetypeId = resolveListedArchetype(req.unitArchetype);
        const bool crow = archetypeId == "core:crow";
        const bool wolf = archetypeId == "core:wolf";
        const bool hunter = archetypeId == "core:hunter";
        if (crow && !host.crows)
            return {};
        if (!crow && (!host.ai || !host.play))
            return {};

        const TransformComponent* home = world.get<TransformComponent>(spawner);
        SpawnServices services;
        services.world      = host.world;
        services.assets     = host.assets;
        services.pins       = host.pins;
        services.renderer   = host.renderer;
        services.audio      = host.audio;
        services.ground     = host.ground;
        services.ai         = host.ai;
        services.user       = const_cast<ListedSpawnHost*>(&host);
        services.modelAnimatedOnly = !host.allowStaticModel;
        services.crows      = host.crows;
        services.crowTerrain = host.terrain;
        services.crowHomeSet = true;
        services.crowHomeX  = home ? home->position.x : req.x;
        services.crowHomeZ  = home ? home->position.z : req.z;
        services.crowSeed   = req.seed == 0u ? 1u : req.seed;
        services.crowSlot   = req.slot;
        services.ensureMesh = [](void* user, Entity entity) {
            auto* listed = static_cast<ListedSpawnHost*>(user);
            if (listed && listed->onProxy)
                listed->onProxy(listed->user, entity);
            return true;
        };
        const ArchetypeSpawnHost spawn = makeSpawnHost(services);
        ArchetypeSpawnPose pose{};
        pose.position = Math::Vector3f{ req.x, listedGroundY(host, req.x, req.z) + (crow ? 0.0f : 0.5f), req.z };
        pose.scale    = Math::Vector3f{ 1.0f, 1.0f, 1.0f };
        ContentError err;
        const Entity e = instantiateArchetype(spawn, archetypeId, pose, {}, &err);
        if (!e.valid())
        {
            DE_LOG_WARN(LogCategory::AI, "Spawner: archetype '{}' failed ({})", archetypeId, err.message);
            return {};
        }
        if (!crow && host.noteHunter)
            host.noteHunter(host.user, e);
        if (!crow && host.bindPhysics)
            host.bindPhysics(host.user, e, wolf);
        if (hunter && host.play)
        {
            host.play->stockHunter(e);
            bindListedNpcDialogue(world, e);
        }
        SpawnedUnitComponent mark{};
        mark.spawner = spawner;
        mark.slot    = req.slot;
        if (!world.has<SpawnedUnitComponent>(e))
            world.emplace<SpawnedUnitComponent>(e, mark);
        return e;
    }

} // namespace Dark
