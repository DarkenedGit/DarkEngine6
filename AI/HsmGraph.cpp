#include "AI/HsmGraph.h"
#include "Core/Log.h"

namespace Dark
{
    HsmGraphDef::HsmGraphDef()
    {
        type = AssetType::HsmGraph;
    }

    int32_t HsmGraphDef::findStateIndex(std::string_view key) const
    {
        for (uint32_t i = 0; i < states.size(); ++i)
        {
            if (states[i].name == key)
                return static_cast<int32_t>(i);
        }
        return -1;
    }

    int32_t HsmGraphDef::findEventIndex(std::string_view key) const
    {
        for (uint32_t i = 0; i < events.size(); ++i)
        {
            if (events[i].name == key)
                return static_cast<int32_t>(i);
        }
        return -1;
    }

    AI::HsmEventId HsmGraphDef::eventId(std::string_view key) const
    {
        const int32_t i = findEventIndex(key);
        if (i < 0)
            return AI::kHsmEventNone;
        return events[static_cast<uint32_t>(i)].id;
    }

    float HsmGraphDef::paramFloat(std::string_view key, float fallback) const
    {
        for (const HsmParamDef& p : params)
        {
            if (p.name == key)
                return p.defaultFloat;
        }
        return fallback;
    }

    void HsmGraphInstance::reset()
    {
        if (m_machine.isRunning())
            m_machine.stop();
        m_machine.setRoot(nullptr);
        m_machine.setOwner(nullptr);
        m_states.clear();
        m_byName.clear();
        m_pathNames.clear();
        m_ownedDef = HsmGraphDef{};
        m_def      = nullptr;
        m_actions.clear();
        m_guards.clear();
    }

    AI::HsmAction HsmGraphInstance::bindAction(const std::string& name, const HsmNamedActionMap& actions) const
    {
        if (name.empty())
            return {};
        const auto it = actions.find(name);
        if (it == actions.end())
        {
            DE_LOG_WARN(LogCategory::AI, "HsmGraph: unknown action '{}'", name);
            return {};
        }
        return it->second;
    }

    AI::HsmGuard HsmGraphInstance::bindGuard(const std::string& name, const HsmNamedGuardMap& guards) const
    {
        if (name.empty())
            return {};
        const auto it = guards.find(name);
        if (it == guards.end())
        {
            DE_LOG_WARN(LogCategory::AI, "HsmGraph: unknown guard '{}'", name);
            return {};
        }
        return it->second;
    }

    bool HsmGraphInstance::build(const HsmGraphDef& def, void* owner, const HsmNamedActionMap& actions, const HsmNamedGuardMap& guards)
    {
        reset();
        if (def.states.empty() || def.root.empty())
        {
            DE_LOG_ERROR(LogCategory::AI, "HsmGraph: states and root are required");
            return false;
        }
        if (def.findStateIndex(def.root) < 0)
        {
            DE_LOG_ERROR(LogCategory::AI, "HsmGraph: unknown root '{}'", def.root);
            return false;
        }

        m_actions = actions;
        m_guards  = guards;
        m_states.reserve(def.states.size());
        for (const HsmStateDef& st : def.states)
        {
            if (st.name.empty())
            {
                DE_LOG_ERROR(LogCategory::AI, "HsmGraph: state missing name");
                reset();
                return false;
            }
            if (m_byName.contains(st.name))
            {
                DE_LOG_ERROR(LogCategory::AI, "HsmGraph: duplicate state '{}'", st.name);
                reset();
                return false;
            }
            auto node = std::make_unique<AI::HsmState>(st.name);
            m_byName[st.name] = node.get();
            m_states.push_back(std::move(node));
        }

        for (const HsmStateDef& st : def.states)
        {
            AI::HsmState* node = m_byName[st.name];
            if (!st.parent.empty())
            {
                AI::HsmState* parent = findState(st.parent);
                if (!parent)
                {
                    DE_LOG_ERROR(LogCategory::AI, "HsmGraph: state '{}' parent '{}' not found", st.name, st.parent);
                    reset();
                    return false;
                }
                node->setParent(parent);
            }
            node->setHistory(st.history);
            node->setInheritEntryExit(st.inheritEntryExit);
            if (!st.base.empty())
            {
                AI::HsmState* base = findState(st.base);
                if (!base)
                {
                    DE_LOG_ERROR(LogCategory::AI, "HsmGraph: state '{}' base '{}' not found", st.name, st.base);
                    reset();
                    return false;
                }
                node->setBase(base);
            }
            if (AI::HsmAction enter = bindAction(st.onEnter, actions))
                node->onEnter(std::move(enter));
            if (AI::HsmAction exit = bindAction(st.onExit, actions))
                node->onExit(std::move(exit));
        }

        for (const HsmStateDef& st : def.states)
        {
            if (st.initial.empty())
                continue;
            AI::HsmState* node    = m_byName[st.name];
            AI::HsmState* initial = findState(st.initial);
            if (!initial)
            {
                DE_LOG_ERROR(LogCategory::AI, "HsmGraph: state '{}' initial '{}' not found", st.name, st.initial);
                reset();
                return false;
            }
            node->setInitial(initial);
        }

        for (const HsmTransitionDef& t : def.transitions)
        {
            const AI::HsmEventId ev = def.eventId(t.event);
            if (t.event.empty() || ev == AI::kHsmEventNone)
            {
                DE_LOG_ERROR(LogCategory::AI, "HsmGraph: transition missing event '{}'", t.event);
                reset();
                return false;
            }
            AI::HsmState* target = findState(t.to);
            if (!target && t.kind != AI::HsmTransitionKind::Internal)
            {
                DE_LOG_ERROR(LogCategory::AI, "HsmGraph: transition to '{}' not found", t.to);
                reset();
                return false;
            }
            AI::HsmAction action = bindAction(t.action, actions);
            AI::HsmGuard  guard  = bindGuard(t.guard, guards);

            std::vector<AI::HsmState*> sources;
            if (t.from.size() == 1 && t.from[0] == "*")
            {
                AI::HsmState* root = findState(def.root);
                if (root)
                    sources.push_back(root);
            }
            else
            {
                for (const std::string& fromName : t.from)
                {
                    AI::HsmState* src = findState(fromName);
                    if (!src)
                    {
                        DE_LOG_ERROR(LogCategory::AI, "HsmGraph: transition from '{}' not found", fromName);
                        reset();
                        return false;
                    }
                    sources.push_back(src);
                }
            }
            if (sources.empty())
            {
                DE_LOG_ERROR(LogCategory::AI, "HsmGraph: transition has no from states");
                reset();
                return false;
            }
            for (AI::HsmState* src : sources)
            {
                if (t.kind == AI::HsmTransitionKind::Internal)
                    src->addInternalTransition(ev, guard, action);
                else
                    src->addTransition(ev, target, guard, action, t.kind);
            }
        }

        AI::HsmState* root = findState(def.root);
        m_ownedDef = def;
        m_def      = &m_ownedDef;
        m_machine.setRoot(root);
        m_machine.setOwner(owner);
        return true;
    }

    bool HsmGraphInstance::start()
    {
        if (!m_machine.start())
            return false;
        refreshPathNames();
        return true;
    }

    void HsmGraphInstance::stop()
    {
        m_machine.stop();
        m_pathNames.clear();
    }

    bool HsmGraphInstance::isRunning() const
    {
        return m_machine.isRunning();
    }

    bool HsmGraphInstance::processEvent(AI::HsmEvent event)
    {
        const bool handled = m_machine.processEvent(event);
        refreshPathNames();
        return handled;
    }

    bool HsmGraphInstance::processEventNamed(std::string_view name, void* payload, float dt)
    {
        const AI::HsmEventId id = eventId(name);
        if (id == AI::kHsmEventNone)
            return false;
        AI::HsmEvent ev{};
        ev.id        = id;
        ev.payload   = payload;
        ev.deltaTime = dt;
        return processEvent(ev);
    }

    bool HsmGraphInstance::tick(float dt)
    {
        const bool handled = m_machine.tick(dt);
        refreshPathNames();
        return handled;
    }

    bool HsmGraphInstance::isIn(std::string_view stateName) const
    {
        const AI::HsmState* s = findState(stateName);
        return s && m_machine.isIn(s);
    }

    const char* HsmGraphInstance::leafName() const
    {
        const AI::HsmState* leaf = m_machine.currentLeaf();
        return leaf ? leaf->name().c_str() : "";
    }

    AI::HsmEventId HsmGraphInstance::eventId(std::string_view name) const
    {
        return m_def ? m_def->eventId(name) : AI::kHsmEventNone;
    }

    float HsmGraphInstance::paramFloat(std::string_view name, float fallback) const
    {
        return m_def ? m_def->paramFloat(name, fallback) : fallback;
    }

    AI::HsmState* HsmGraphInstance::findState(std::string_view name) const
    {
        const auto it = m_byName.find(std::string(name));
        return it == m_byName.end() ? nullptr : it->second;
    }

    void HsmGraphInstance::refreshPathNames()
    {
        m_pathNames.clear();
        for (AI::HsmState* s : m_machine.activePath())
        {
            if (s)
                m_pathNames.push_back(s->name());
        }
    }

    HsmGraphDef makeHunterHsmGraph()
    {
        HsmGraphDef def;
        def.name = "hunter";
        def.root = "Root";
        def.events = {
            { "See", 10 },
            { "Lose", 11 },
            { "Wet", 12 },
            { "MemoryDone", 13 },
            { "Assist", 14 },
            { "Flee", 15 },
            { "AssistDone", 16 },
            { "FleeDone", 17 },
        };
        def.params = { { "memorySec", 1.5f } };
        def.states = {
            { "Root", "", "Wander" },
            { "Wander", "Root" },
            { "Chase", "Root" },
            { "Memory", "Root" },
            { "Assist", "Root" },
            { "Flee", "Root" },
        };
        def.transitions = {
            { { "Wander" }, "Chase", "See" },
            { { "Wander" }, "Assist", "Assist" },
            { { "Wander" }, "Flee", "Flee" },
            { { "Chase" }, "Memory", "Lose", "", "armMemory" },
            { { "Chase" }, "Wander", "Wet" },
            { { "Chase" }, "Flee", "Flee" },
            { { "Memory" }, "Chase", "See" },
            { { "Memory" }, "Wander", "Wet" },
            { { "Memory" }, "Wander", "MemoryDone" },
            { { "Memory" }, "Assist", "Assist" },
            { { "Memory" }, "Flee", "Flee" },
            { { "Assist" }, "Chase", "See" },
            { { "Assist" }, "Wander", "Wet" },
            { { "Assist" }, "Wander", "AssistDone" },
            { { "Assist" }, "Flee", "Flee" },
            { { "Flee" }, "Wander", "Wet" },
            { { "Flee" }, "Wander", "FleeDone" },
        };
        return def;
    }

    HsmGraphDef makePlayerHsmGraph()
    {
        HsmGraphDef def;
        def.name = "player";
        def.root = "Root";
        def.events = {
            { "Jump", 20 },
            { "Land", 21 },
            { "Fall", 22 },
            { "EnterWater", 23 },
            { "LeaveWater", 24 },
            { "Die", 25 },
            { "Respawn", 26 },
        };
        def.states = {
            { "Root", "", "Alive" },
            { "Alive", "Root", "Grounded", "", AI::HsmHistory::Shallow },
            { "Grounded", "Alive" },
            { "Jumping", "Alive" },
            { "Falling", "Alive" },
            { "Swimming", "Alive" },
            { "Dead", "Root" },
        };
        def.transitions = {
            { { "Grounded" }, "Jumping", "Jump" },
            { { "Jumping" }, "Falling", "Fall" },
            { { "Jumping" }, "Grounded", "Land" },
            { { "Falling" }, "Grounded", "Land" },
            { { "Alive" }, "Swimming", "EnterWater" },
            { { "Swimming" }, "Grounded", "LeaveWater" },
            { { "Alive" }, "Dead", "Die" },
            { { "Dead" }, "Alive", "Respawn" },
        };
        return def;
    }
} // namespace Dark
