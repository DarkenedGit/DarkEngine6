#include "AI/Brain.h"
#include "Core/Log.h"

namespace Dark::AI
{
    HsmNamedActionMap Brain::hunterActions()
    {
        HsmNamedActionMap actions;
        actions["armMemory"] = [this](HsmContext&) {
            m_memoryLeft = m_graph.paramFloat("memorySec", kMemorySec);
        };
        return actions;
    }

    Brain::Brain()
    {
        load(makeHunterHsmGraph());
    }

    bool Brain::load(const HsmGraphDef& def)
    {
        HsmGraphDef previous = m_def;
        m_def = def;
        if (m_graph.build(m_def, this, hunterActions()))
            return true;
        DE_LOG_ERROR(LogCategory::AI, "Brain: failed to build HSM graph '{}'", def.name);
        m_def = std::move(previous);
        if (!m_def.states.empty())
            m_graph.build(m_def, this, hunterActions());
        return false;
    }

    bool Brain::fire(std::string_view eventName)
    {
        return m_graph.processEventNamed(eventName);
    }

    bool Brain::start()
    {
        return m_graph.start();
    }

    void Brain::tick(float dt, bool seesOnLand, bool playerInWater)
    {
        if (!m_graph.isRunning())
            return;
        if (playerInWater)
        {
            fire("Wet");
            return;
        }
        if (leaf() == Leaf::Flee)
            return;
        if (seesOnLand)
        {
            fire("See");
            return;
        }
        if (leaf() == Leaf::Assist)
            return;
        fire("Lose");
        if (leaf() == Leaf::Memory)
        {
            m_memoryLeft -= dt;
            if (m_memoryLeft <= 0.0f)
                fire("MemoryDone");
        }
    }

    void Brain::onAssist()
    {
        if (!m_graph.isRunning())
            return;
        const Leaf now = leaf();
        if (now == Leaf::Flee || now == Leaf::Chase || now == Leaf::Assist)
            return;
        fire("Assist");
    }

    void Brain::onFlee()
    {
        if (!m_graph.isRunning())
            return;
        if (leaf() == Leaf::Flee)
            return;
        fire("Flee");
    }

    void Brain::onAssistDone()
    {
        if (!m_graph.isRunning() || leaf() != Leaf::Assist)
            return;
        fire("AssistDone");
    }

    void Brain::onFleeDone()
    {
        if (!m_graph.isRunning() || leaf() != Leaf::Flee)
            return;
        fire("FleeDone");
    }

    Leaf Brain::leaf() const
    {
        if (m_graph.isIn("Chase"))
            return Leaf::Chase;
        if (m_graph.isIn("Memory"))
            return Leaf::Memory;
        if (m_graph.isIn("Assist"))
            return Leaf::Assist;
        if (m_graph.isIn("Flee"))
            return Leaf::Flee;
        return Leaf::Wander;
    }
} // namespace Dark::AI
