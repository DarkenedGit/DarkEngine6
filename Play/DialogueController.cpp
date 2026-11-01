#include "Play/DialogueController.h"

#include "AI/AiComponents.h"
#include "Assets/AssetManager.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Play/Carry.h"
#include "Play/DialogueBinding.h"
#include "Play/DialogueFacts.h"
#include "Play/DialogueMemory.h"
#include "Play/GuardVoice.h"

#include <cmath>

namespace Dark
{
    using namespace Math;

    void DialogueController::bind(void (*fill)(void* user, DialogueHost& host), void* user)
    {
        m_fill = fill;
        m_user = user;
    }

    void DialogueController::pull()
    {
        m_host = {};
        if (m_fill)
            m_fill(m_user, m_host);
    }

    World* DialogueController::world()
    {
        pull();
        return m_host.world;
    }

    void DialogueController::releaseSpeaker()
    {
        if (!m_talkNpc.valid())
            return;
        if (m_host.guardVoice)
            m_host.guardVoice->setSuppressed(m_talkNpc, false, m_host.audio);
        m_talkNpc = {};
    }

    Entity DialogueController::pickTarget()
    {
        pull();
        if (!m_host.enabled || !m_host.world)
            return {};
        World& world = *m_host.world;
        if (carriedBody(world).valid())
            return {};
        const TransformComponent* playerXf = m_host.player.valid() ? world.get<TransformComponent>(m_host.player) : nullptr;
        if (!playerXf)
            return {};
        Vector3f look = m_host.look;
        if (look.MagnitudeSqrd() < 1.0e-6f)
            return {};
        look.Normalize();
        const Vector3f camPos = m_host.cameraPos;
        const Entity   player = m_host.player;
        AssetManager*  assets = m_host.assets;

        Entity best{};
        float  bestDot = 0.94f;
        world.each<DialogueBindingComponent>([&](Entity npc, DialogueBindingComponent& binding) {
            if (binding.graph.empty() || npc == player)
                return;
            if (const AiAgentComponent* agent = world.get<AiAgentComponent>(npc); agent && agent->carried)
                return;
            const TransformComponent* npcXf = world.get<TransformComponent>(npc);
            if (!npcXf)
                return;
            const Vector3f flat{ npcXf->position.x - playerXf->position.x, 0.0f, npcXf->position.z - playerXf->position.z };
            float range = 2.5f;
            if (assets)
            {
                if (const AssetRef<DialogueGraphDef> graph = assets->tryLoadDialogueGraph(binding.graph))
                    range = graph->talkRange;
            }
            if (flat.MagnitudeSqrd() > range * range)
                return;
            const Vector3f to{ npcXf->position.x - camPos.x, npcXf->position.y + 1.1f - camPos.y, npcXf->position.z - camPos.z };
            const float len2 = to.MagnitudeSqrd();
            if (len2 < 1.0e-6f)
                return;
            const float dot = look.Dot(to * (1.0f / std::sqrt(len2)));
            if (dot > bestDot)
            {
                bestDot = dot;
                best    = npc;
            }
        });
        return best;
    }

    void DialogueController::tryBegin()
    {
        pull();
        if (!m_host.enabled || !m_host.world || !m_host.assets)
            return;
        if (m_runtime.active())
            return;
        const Entity npc = pickTarget();
        if (!npc.valid())
            return;
        const DialogueBindingComponent* binding = m_host.world->get<DialogueBindingComponent>(npc);
        if (!binding || binding->graph.empty())
            return;
        AssetRef<DialogueGraphDef> loaded = m_host.assets->tryLoadDialogueGraph(binding->graph);
        if (!loaded)
            return;
        m_graph = std::move(loaded);
        m_runtime.setGraph(m_graph.get());
        m_speech.setServices(m_host.assets, m_host.audio, m_host.world);
        m_speech.startSynth();
        m_runtime.setAudio(m_speech.hook());
        DialogueFactsComponent* facts = nullptr;
        if (m_host.session.valid())
            facts = m_host.world->get<DialogueFactsComponent>(m_host.session);
        m_runtime.setFacts(facts);
        if (!m_runtime.tryStart(*m_host.world, m_host.player, npc, m_host.quest))
            return;
        if (m_host.guardVoice)
            m_host.guardVoice->setSuppressed(npc, true, m_host.audio);
        m_talkNpc = npc;
    }

    void DialogueController::tick(float dt)
    {
        pull();
        if (!m_host.enabled || !m_host.world)
            return;
        m_speech.setServices(m_host.assets, m_host.audio, m_host.world);
        DialogueFactsComponent* facts = nullptr;
        if (m_host.session.valid())
            facts = m_host.world->get<DialogueFactsComponent>(m_host.session);
        m_runtime.setFacts(facts);
        m_runtime.setUiBlocked(m_host.uiBlocked);
        m_runtime.tick(*m_host.world, dt);
        if (m_runtime.active())
            m_speech.follow(m_runtime.speaker());
        if (m_talkNpc.valid() && !m_runtime.active())
            releaseSpeaker();
    }

    void DialogueController::endPlay()
    {
        pull();
        releaseSpeaker();
        m_runtime.interrupt(DialogueInterrupt::Cancel);
        if (m_host.world)
        {
            clearDialogueMemory(*m_host.world);
            if (m_host.session.valid())
            {
                if (DialogueFactsComponent* facts = m_host.world->get<DialogueFactsComponent>(m_host.session))
                    facts->facts.clear();
            }
        }
        m_speech.stop();
        m_runtime.setGraph(nullptr);
        m_graph = {};
        m_talkNpc = {};
    }

} // namespace Dark
