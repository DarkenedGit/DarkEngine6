#include "AI/Brain.h"

namespace Dark::AI
{
    Brain::Brain()
    {
        m_wander.setParent(&m_root);
        m_chase.setParent(&m_root);
        m_memory.setParent(&m_root);
        m_root.setInitial(&m_wander);

        m_wander.addTransition(kHunterSee, &m_chase);
        m_chase.addTransition(kHunterLose, &m_memory, {}, [this](HsmContext&) { m_memoryLeft = kMemorySec; });
        m_chase.addTransition(kHunterWet, &m_wander);
        m_memory.addTransition(kHunterSee, &m_chase);
        m_memory.addTransition(kHunterWet, &m_wander);
        m_memory.addTransition(kHunterMemoryDone, &m_wander);
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
        if (seesOnLand)
        {
            m_machine.processEvent(HsmEvent{ kHunterSee });
            return;
        }
        m_machine.processEvent(HsmEvent{ kHunterLose });
        if (leaf() == Leaf::Memory)
        {
            m_memoryLeft -= dt;
            if (m_memoryLeft <= 0.0f)
                m_machine.processEvent(HsmEvent{ kHunterMemoryDone });
        }
    }

    Leaf Brain::leaf() const
    {
        if (m_machine.isIn(&m_chase))
            return Leaf::Chase;
        if (m_machine.isIn(&m_memory))
            return Leaf::Memory;
        return Leaf::Wander;
    }
} // namespace Dark::AI
