#pragma once

#include "AI/Hsm.h"

namespace Dark::AI
{
    inline constexpr HsmEventId kHunterSee        = 10;
    inline constexpr HsmEventId kHunterLose       = 11;
    inline constexpr HsmEventId kHunterWet        = 12;
    inline constexpr HsmEventId kHunterMemoryDone = 13;

    enum class Leaf
    {
        Wander,
        Chase,
        Memory
    };

    class Brain
    {
    public:
        static constexpr float kMemorySec = 1.5f;

        Brain();
        bool start();
        void tick(float dt, bool seesOnLand, bool playerInWater);
        Leaf leaf() const;
        float      memoryLeft() const { return m_memoryLeft; }

    private:
        HsmState   m_root{ "Root" };
        HsmState   m_wander{ "Wander" };
        HsmState   m_chase{ "Chase" };
        HsmState   m_memory{ "Memory" };
        HsmMachine m_machine;
        float      m_memoryLeft = 0.0f;
    };
} // namespace Dark::AI
