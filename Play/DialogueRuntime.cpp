#include "Play/DialogueRuntime.h"

#include "AI/AiComponents.h"
#include "AI/Awareness.h"
#include "AI/Brain.h"
#include "Character/HealthComponent.h"
#include "Character/HitReaction.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/QuestRuntime.h"
#include "Play/DialogueFacts.h"
#include "Play/DialogueMemory.h"
#include "Play/DialogueQuery.h"

#include <cmath>

namespace Dark
{
    namespace
    {
        const char* interruptName(DialogueInterrupt reason)
        {
            switch (reason)
            {
            case DialogueInterrupt::Distance: return "distance";
            case DialogueInterrupt::Alert:    return "alert";
            case DialogueInterrupt::Combat:   return "combat";
            case DialogueInterrupt::Death:    return "death";
            case DialogueInterrupt::Menu:     return "menu";
            case DialogueInterrupt::Pause:    return "pause";
            case DialogueInterrupt::Cancel:   return "cancel";
            case DialogueInterrupt::Ui:       return "ui";
            }
            return "unknown";
        }

        bool entityDead(World& world, Entity e)
        {
            const HealthComponent* hp = world.get<HealthComponent>(e);
            return hp && !hp->health.alive();
        }

        bool entityInCombat(World& world, Entity e)
        {
            if (const AiAgentComponent* agent = world.get<AiAgentComponent>(e))
            {
                if (agent->inDuel || agent->meleeWindup > 0.0f || agent->meleeAttack >= 0)
                    return true;
            }
            if (const BrainComponent* brain = world.get<BrainComponent>(e))
            {
                if (brain->brain)
                {
                    const AI::Leaf leaf = brain->brain->leaf();
                    if (leaf == AI::Leaf::Chase || leaf == AI::Leaf::Assist)
                        return true;
                }
            }
            if (const HitReactionComponent* hit = world.get<HitReactionComponent>(e))
            {
                if (hit->hit.stunned() || hit->hit.knockingBack())
                    return true;
            }
            return false;
        }

        bool linePhase(DialoguePhase phase)
        {
            return phase == DialoguePhase::Resume || phase == DialoguePhase::NodeLine || phase == DialoguePhase::Refusal;
        }
    } // namespace

    void DialogueRuntime::setGraph(const DialogueGraphDef* graph)
    {
        if (active())
            return;
        m_graph = graph;
    }

    void DialogueRuntime::setAudio(DialogueLineAudio audio)
    {
        m_audio = audio;
    }

    void DialogueRuntime::setFacts(DialogueFactsComponent* facts)
    {
        m_facts = facts;
    }

    void DialogueRuntime::setUiBlocked(bool blocked)
    {
        m_uiBlocked = blocked;
    }

    bool DialogueRuntime::active() const
    {
        return m_phase != DialoguePhase::Idle;
    }

    bool DialogueRuntime::choiceOpen() const
    {
        return m_phase == DialoguePhase::Choice;
    }

    bool DialogueRuntime::locksWorld() const
    {
        return m_pauseWorld && (m_phase == DialoguePhase::Resume || m_phase == DialoguePhase::NodeLine || m_phase == DialoguePhase::Choice);
    }

    void DialogueRuntime::requestSkip()
    {
        if (linePhase(m_phase))
            m_skip = true;
    }

    void DialogueRuntime::moveChoice(int delta)
    {
        if (m_phase == DialoguePhase::Choice)
            m_move += delta;
    }

    void DialogueRuntime::commitChoice()
    {
        if (m_phase == DialoguePhase::Choice)
            m_commit = true;
    }

    void DialogueRuntime::writeBookmark(World& world, DialoguePending pending)
    {
        if (!m_graph || !m_npc.valid())
            return;
        DialogueMemoryComponent* memory = world.get<DialogueMemoryComponent>(m_npc);
        if (!memory)
            return;
        memory->bookmark.valid   = true;
        memory->bookmark.graphId = m_graph->id;
        memory->bookmark.nodeId  = m_nodeId;
        memory->bookmark.pending = pending;
    }

    void DialogueRuntime::endConversation(World& world)
    {
        if (m_audio.stop)
            m_audio.stop(m_audio.user);
        if (DialogueMemoryComponent* memory = m_npc.valid() ? world.get<DialogueMemoryComponent>(m_npc) : nullptr)
            memory->bookmark = {};
        m_phase = DialoguePhase::Idle;
        m_pauseWorld = false;
        m_hasLine = false;
        m_committed = false;
        m_skip = false;
        m_commit = false;
        m_move = 0;
        m_subtitle.clear();
        m_choiceIndex.clear();
    }

    void DialogueRuntime::beginNodeLine(World& world)
    {
        m_phase = DialoguePhase::NodeLine;
        m_committed = false;
        m_shownLeft = 0.0f;
        m_waited = 0.0f;
        m_subtitle.clear();
        m_hasLine = false;
        m_selected = 0;
        DialogueMemoryComponent* memory = world.get<DialogueMemoryComponent>(m_npc);
        const DialogueNode* node = m_graph ? m_graph->findNode(m_nodeId) : nullptr;
        const DialogueLine* line = (memory && node) ? pickDialogueLine(memory->lines, node->lines) : nullptr;
        if (line)
        {
            m_line = *line;
            m_hasLine = true;
        }
        writeBookmark(world, DialoguePending::Line);
    }

    void DialogueRuntime::beginChoice(World& world)
    {
        m_phase = DialoguePhase::Choice;
        m_hasLine = false;
        m_committed = false;
        m_choiceIndex.clear();
        m_selected = 0;
        const DialogueNode* node = m_graph ? m_graph->findNode(m_nodeId) : nullptr;
        DialogueMemoryComponent* memory = world.get<DialogueMemoryComponent>(m_npc);
        if (!node || !memory)
        {
            endConversation(world);
            return;
        }
        DialogueQueryView view;
        view.facts = m_facts;
        view.inventory = m_player.valid() ? world.get<InventoryComponent>(m_player) : nullptr;
        view.playerHealth = m_player.valid() ? world.get<HealthComponent>(m_player) : nullptr;
        view.npcHealth = world.get<HealthComponent>(m_npc);
        view.npc = world.get<AiAgentComponent>(m_npc);
        view.quest = m_quest;
        for (int i = 0; i < static_cast<int>(node->choices.size()); ++i)
        {
            const DialogueChoice& choice = node->choices[static_cast<size_t>(i)];
            if (choice.once && dialogueHeard(memory->lines, choice.id))
                continue;
            bool show = true;
            for (const DialogueCondition& condition : choice.conditions)
            {
                if (!dialogueConditionPasses(*m_graph, condition, view))
                {
                    show = false;
                    break;
                }
            }
            if (show)
                m_choiceIndex.push_back(i);
        }
        if (m_choiceIndex.empty())
        {
            DE_LOG_WARN(LogCategory::AI, "Dialogue: no legal choices on \"{}\"", m_nodeId);
            endConversation(world);
            return;
        }
        writeBookmark(world, DialoguePending::Choice);
    }

    void DialogueRuntime::beginResume(World& world)
    {
        DialogueMemoryComponent* memory = world.get<DialogueMemoryComponent>(m_npc);
        if (!memory)
        {
            m_phase = DialoguePhase::Idle;
            return;
        }
        m_nodeId = memory->bookmark.nodeId;
        const DialoguePending pending = memory->bookmark.pending;
        if (m_graph->resume.empty())
        {
            if (pending == DialoguePending::Choice)
                beginChoice(world);
            else
                beginNodeLine(world);
            return;
        }
        m_phase = DialoguePhase::Resume;
        m_committed = false;
        m_shownLeft = 0.0f;
        m_waited = 0.0f;
        m_subtitle.clear();
        const DialogueLine* line = pickDialogueLine(memory->lines, m_graph->resume);
        m_hasLine = line != nullptr;
        if (line)
            m_line = *line;
        if (!m_hasLine)
        {
            if (pending == DialoguePending::Choice)
                beginChoice(world);
            else
                beginNodeLine(world);
        }
    }

    void DialogueRuntime::beginRefusal(World& world)
    {
        m_pauseWorld = false;
        DE_LOG_INFO(LogCategory::AI, "Dialogue: refusal {}", m_npc.id());
        if (!m_graph || m_graph->refusal.empty())
        {
            DE_LOG_WARN(LogCategory::AI, "Dialogue: refusal bank is empty");
            m_phase = DialoguePhase::Idle;
            return;
        }
        DialogueMemoryComponent* memory = world.get<DialogueMemoryComponent>(m_npc);
        const DialogueLine* line = memory ? pickDialogueLine(memory->lines, m_graph->refusal) : nullptr;
        if (!line)
        {
            DE_LOG_WARN(LogCategory::AI, "Dialogue: refusal bank is empty");
            m_phase = DialoguePhase::Idle;
            return;
        }
        m_phase = DialoguePhase::Refusal;
        m_line = *line;
        m_hasLine = true;
        m_committed = false;
        m_shownLeft = 0.0f;
        m_waited = 0.0f;
        m_subtitle.clear();
    }

    bool DialogueRuntime::tryStart(World& world, Entity player, Entity npc, QuestRuntime* quest)
    {
        if (active() || !m_graph || !npc.valid())
            return false;
        m_world = &world;
        if (const AiAgentComponent* agent = world.get<AiAgentComponent>(npc))
        {
            if (agent->carried)
                return false;
        }
        if (!world.get<DialogueMemoryComponent>(npc))
            world.emplace<DialogueMemoryComponent>(npc);

        const bool dead = entityDead(world, npc);
        bool alert = false;
        if (const AiAgentComponent* agent = world.get<AiAgentComponent>(npc))
            alert = agent->aware.alertness == AI::Alertness::Alert;
        const bool combat = entityInCombat(world, npc) || entityInCombat(world, player);

        DialogueMemoryComponent* memory = world.get<DialogueMemoryComponent>(npc);
        bool bookmarkOk = memory && memory->bookmark.valid && memory->bookmark.graphId == m_graph->id;
        if (bookmarkOk && !m_graph->findNode(memory->bookmark.nodeId))
        {
            DE_LOG_WARN(LogCategory::AI, "Dialogue: dropped bookmark {} / {}", memory->bookmark.graphId, memory->bookmark.nodeId);
            memory->bookmark = {};
            bookmarkOk = false;
        }

        m_player = player;
        m_npc = npc;
        m_quest = quest;
        m_skip = false;
        m_commit = false;
        m_move = 0;
        m_speakerName = m_graph->speaker;

        const DialogueEntryKind kind = chooseDialogueEntry(true, dead, alert, combat, bookmarkOk);
        if (kind == DialogueEntryKind::Missing)
            return false;
        if (kind == DialogueEntryKind::Refusal)
        {
            m_pauseWorld = false;
            beginRefusal(world);
            return active();
        }
        m_pauseWorld = m_graph->pauseWorld;
        if (kind == DialogueEntryKind::Resume)
        {
            beginResume(world);
            return active();
        }
        m_nodeId = m_graph->entry;
        if (!m_graph->findNode(m_nodeId))
        {
            m_phase = DialoguePhase::Idle;
            m_pauseWorld = false;
            return false;
        }
        beginNodeLine(world);
        return active();
    }

    void DialogueRuntime::interrupt(DialogueInterrupt reason)
    {
        if (!active())
            return;
        if (m_world)
        {
            if (m_phase == DialoguePhase::Choice)
                writeBookmark(*m_world, DialoguePending::Choice);
            else if (m_phase == DialoguePhase::NodeLine)
                writeBookmark(*m_world, DialoguePending::Line);
        }
        DE_LOG_INFO(LogCategory::AI, "Dialogue: interrupt {} on {}", interruptName(reason), m_npc.id());
        if (m_audio.stop)
            m_audio.stop(m_audio.user);
        m_phase = DialoguePhase::Idle;
        m_pauseWorld = false;
        m_hasLine = false;
        m_committed = false;
        m_skip = false;
        m_commit = false;
        m_move = 0;
        m_subtitle.clear();
        m_choiceIndex.clear();
    }

    void DialogueRuntime::advance(World& world)
    {
        if (m_audio.stop)
            m_audio.stop(m_audio.user);
        if (m_phase == DialoguePhase::Refusal)
        {
            m_phase = DialoguePhase::Idle;
            m_pauseWorld = false;
            m_hasLine = false;
            m_subtitle.clear();
            return;
        }
        if (m_phase == DialoguePhase::Resume)
        {
            DialogueMemoryComponent* memory = world.get<DialogueMemoryComponent>(m_npc);
            const DialoguePending pending = memory ? memory->bookmark.pending : DialoguePending::Line;
            if (pending == DialoguePending::Choice)
                beginChoice(world);
            else
                beginNodeLine(world);
            return;
        }
        if (m_phase == DialoguePhase::NodeLine)
            beginChoice(world);
    }

    void DialogueRuntime::commitLine(World& world, float dt)
    {
        if (!m_hasLine)
        {
            advance(world);
            return;
        }
        float shown = 2.5f;
        bool ready = true;
        if (m_audio.acquire)
            ready = m_audio.acquire(m_audio.user, m_line, m_npc, m_waited + dt, shown);
        if (!ready)
        {
            m_waited += dt;
            return;
        }
        m_committed = true;
        m_shownLeft = shown;
        m_subtitle = m_line.text;
        m_speakerName = m_graph ? m_graph->speaker : std::string{};
        if (DialogueMemoryComponent* memory = world.get<DialogueMemoryComponent>(m_npc))
            rememberDialogueLine(memory->lines, m_line);
        DE_LOG_INFO(LogCategory::AI, "Dialogue: line {} entity {}", m_line.id, m_npc.id());
    }

    bool DialogueRuntime::blockedByWorld(World& world, DialogueInterrupt& reason) const
    {
        if (m_uiBlocked)
        {
            reason = DialogueInterrupt::Ui;
            return true;
        }
        if (entityDead(world, m_npc) || entityDead(world, m_player))
        {
            reason = DialogueInterrupt::Death;
            return true;
        }
        if (const AiAgentComponent* agent = world.get<AiAgentComponent>(m_npc))
        {
            if (agent->aware.alertness == AI::Alertness::Alert)
            {
                reason = DialogueInterrupt::Alert;
                return true;
            }
        }
        if (entityInCombat(world, m_npc) || entityInCombat(world, m_player))
        {
            reason = DialogueInterrupt::Combat;
            return true;
        }
        const TransformComponent* npcXf = world.get<TransformComponent>(m_npc);
        const TransformComponent* playerXf = world.get<TransformComponent>(m_player);
        if (!npcXf || !playerXf || !m_graph)
        {
            reason = DialogueInterrupt::Distance;
            return true;
        }
        const float dx = npcXf->position.x - playerXf->position.x;
        const float dy = npcXf->position.y - playerXf->position.y;
        const float dz = npcXf->position.z - playerXf->position.z;
        const float planar = std::sqrt(dx * dx + dz * dz);
        if (planar > m_graph->leash || std::fabs(dy) > 1.5f)
        {
            reason = DialogueInterrupt::Distance;
            return true;
        }
        return false;
    }

    void DialogueRuntime::tick(World& world, float dt)
    {
        if (!active())
            return;
        m_world = &world;
        DialogueInterrupt reason = DialogueInterrupt::Cancel;
        if (blockedByWorld(world, reason))
        {
            interrupt(reason);
            return;
        }
        if (m_phase == DialoguePhase::Choice)
        {
            const int count = static_cast<int>(m_choiceIndex.size());
            if (count > 0 && m_move != 0)
            {
                m_selected = ((m_selected + m_move) % count + count) % count;
                m_move = 0;
            }
            if (!m_commit)
                return;
            m_commit = false;
            const DialogueNode* node = m_graph ? m_graph->findNode(m_nodeId) : nullptr;
            if (!node || m_selected < 0 || m_selected >= count)
            {
                endConversation(world);
                return;
            }
            const DialogueChoice& choice = node->choices[static_cast<size_t>(m_choiceIndex[static_cast<size_t>(m_selected)])];
            if (choice.once)
            {
                if (DialogueMemoryComponent* memory = world.get<DialogueMemoryComponent>(m_npc))
                    rememberDialogueChoice(memory->lines, choice.id);
            }
            InventoryComponent* bag = m_player.valid() ? world.get<InventoryComponent>(m_player) : nullptr;
            applyDialogueEffects(*m_graph, choice.effects, m_facts, bag, m_quest);
            if (choice.next.empty() || !m_graph->findNode(choice.next))
            {
                endConversation(world);
                return;
            }
            m_nodeId = choice.next;
            beginNodeLine(world);
            return;
        }
        if (!linePhase(m_phase))
            return;
        if (!m_committed)
            commitLine(world, dt);
        if (!active() || !m_committed)
            return;
        if (m_skip)
        {
            m_skip = false;
            advance(world);
            return;
        }
        m_shownLeft -= dt;
        if (m_shownLeft <= 0.0f)
            advance(world);
    }

} // namespace Dark
