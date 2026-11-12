#include "Play/Spawner.h"

#include "AI/AiSystem.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Play/CharacterAttach.h"
#include "Play/Crow.h"
#include "Play/DialogueBinding.h"
#include "Play/DialogueMemory.h"
#include "Play/PlaySession.h"
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

    Entity spawnListedUnit(const ListedSpawnHost& host, Entity spawner, const SpawnerRequest& req)
    {
        if (!host.world || !host.renderer || !host.assets || !host.pins)
            return {};
        World& world = *host.world;
        if (req.unit == SpawnerUnit::Crow)
        {
            if (!host.crows)
                return {};
            const TransformComponent* home  = world.get<TransformComponent>(spawner);
            const float               homeX = home ? home->position.x : req.x;
            const float               homeZ = home ? home->position.z : req.z;
            return spawnCrow(world, *host.renderer, *host.assets, *host.pins, *host.crows, host.terrain, spawner, req, listedGroundY(host, req.x, req.z), homeX, homeZ);
        }

        if (!host.ai || !host.audio || !host.ground || !host.play)
            return {};
        const bool         wolf = req.unit == SpawnerUnit::Wolf;
        TransformComponent xf{};
        xf.position = Math::Vector3f{ req.x, listedGroundY(host, req.x, req.z) + 0.5f, req.z };
        xf.scale    = Math::Vector3f{ 1.0f, 1.0f, 1.0f };
        const Entity e = host.ai->spawnHunter(world, *host.pins, *host.assets, xf);
        if (!e.valid())
        {
            DE_LOG_WARN(LogCategory::AI, "Spawner: failed to create a {}", wolf ? "wolf" : "hunter");
            return {};
        }
        if (host.noteHunter)
            host.noteHunter(host.user, e);

        const CharacterHost character{ world, *host.renderer, *host.assets, *host.pins, *host.audio, *host.ground };
        const ContentModelResult visual = attachNpcVisual(character, e, wolf ? NpcKind::Wolf : NpcKind::Hunter,
                                                          host.allowStaticModel ? ModelAttach::AllowStatic : ModelAttach::AnimatedOnly);
        if (!visual.attached && host.onProxy)
            host.onProxy(host.user, e);
        if (host.bindPhysics)
            host.bindPhysics(host.user, e, wolf);
        if (!wolf)
        {
            host.play->stockHunter(e);
            bindListedNpcDialogue(world, e);
        }
        SpawnedUnitComponent mark{};
        mark.spawner = spawner;
        mark.slot    = req.slot;
        world.emplace<SpawnedUnitComponent>(e, mark);
        return e;
    }

} // namespace Dark
