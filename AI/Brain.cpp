#include "AI/Brain.h"

namespace Dark::AI
{
    Brain::Brain()
    {
        m_wander.setParent(&m_root);
        m_chase.setParent(&m_root);
        m_memory.setParent(&m_root);
        m_assist.setParent(&m_root);
        m_flee.setParent(&m_root);
        m_root.setInitial(&m_wander);

        m_wander.addTransition(kHunterSee, &m_chase);
        m_wander.addTransition(kHunterAssist, &m_assist);
        m_wander.addTransition(kHunterFlee, &m_flee);

        m_chase.addTransition(kHunterLose, &m_memory, {}, [this](HsmContext&) { m_memoryLeft = kMemorySec; });
        m_chase.addTransition(kHunterWet, &m_wander);
        m_chase.addTransition(kHunterFlee, &m_flee);

        m_memory.addTransition(kHunterSee, &m_chase);
        m_memory.addTransition(kHunterWet, &m_wander);
        m_memory.addTransition(kHunterMemoryDone, &m_wander);
        m_memory.addTransition(kHunterAssist, &m_assist);
        m_memory.addTransition(kHunterFlee, &m_flee);

        m_assist.addTransition(kHunterSee, &m_chase);
        m_assist.addTransition(kHunterWet, &m_wander);
        m_assist.addTransition(kHunterAssistDone, &m_wander);
        m_assist.addTransition(kHunterFlee, &m_flee);

        m_flee.addTransition(kHunterWet, &m_wander);
        m_flee.addTransition(kHunterFleeDone, &m_wander);

        m_machine.setRoot(&m_root);
        m_machine.setOwner(this);
    }

    bool Brain::start()
    {
        return m_machine.start();
    }

    void Brain::tick(float dt, bool seesOnLand, bool playerInWater)
    {
        if (!m_machine.isRunning())
            return;
        if (playerInWater)
        {
            m_machine.processEvent(HsmEvent{ kHunterWet });
            return;
        }
        if (leaf() == Leaf::Flee)
            return;
        if (seesOnLand)
        {
            m_machine.processEvent(HsmEvent{ kHunterSee });
            return;
        }
        if (leaf() == Leaf::Assist)
            return;
        m_machine.processEvent(HsmEvent{ kHunterLose });
        if (leaf() == Leaf::Memory)
        {
            m_memoryLeft -= dt;
            if (m_memoryLeft <= 0.0f)
                m_machine.processEvent(HsmEvent{ kHunterMemoryDone });
        }
    }

    void Brain::onAssist()
    {
        if (!m_machine.isRunning())
            return;
        const Leaf now = leaf();
        if (now == Leaf::Flee || now == Leaf::Chase || now == Leaf::Assist)
            return;
        m_machine.processEvent(HsmEvent{ kHunterAssist });
    }

    void Brain::onFlee()
    {
        if (!m_machine.isRunning())
            return;
        if (leaf() == Leaf::Flee)
            return;
        m_machine.processEvent(HsmEvent{ kHunterFlee });
    }

    void Brain::onAssistDone()
    {
        if (!m_machine.isRunning() || leaf() != Leaf::Assist)
            return;
        m_machine.processEvent(HsmEvent{ kHunterAssistDone });
    }

    void Brain::onFleeDone()
    {
        if (!m_machine.isRunning() || leaf() != Leaf::Flee)
            return;
        m_machine.processEvent(HsmEvent{ kHunterFleeDone });
    }

    Leaf Brain::leaf() const
    {
        if (m_machine.isIn(&m_chase))
            return Leaf::Chase;
        if (m_machine.isIn(&m_memory))
            return Leaf::Memory;
        if (m_machine.isIn(&m_assist))
            return Leaf::Assist;
        if (m_machine.isIn(&m_flee))
            return Leaf::Flee;
        return Leaf::Wander;
    }
} // namespace Dark::AI
