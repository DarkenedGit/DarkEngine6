#pragma once

#include "AI/Hsm.h"

namespace Dark::AI
{
    inline constexpr HsmEventId kHunterSee        = 10;
    inline constexpr HsmEventId kHunterLose       = 11;
    inline constexpr HsmEventId kHunterWet        = 12;
    inline constexpr HsmEventId kHunterMemoryDone = 13;
    inline constexpr HsmEventId kHunterAssist     = 14;
    inline constexpr HsmEventId kHunterFlee       = 15;
    inline constexpr HsmEventId kHunterAssistDone = 16;
    inline constexpr HsmEventId kHunterFleeDone   = 17;

    enum class Leaf
    {
        Wander,
        Chase,
        Memory,
        Assist,
        Flee
    };

    class Brain
    {
    public:
        static constexpr float kMemorySec = 1.5f;

        Brain();
        bool start();
        void tick(float dt, bool seesOnLand, bool playerInWater);
        void onAssist();
        void onFlee();
        void onAssistDone();
        void onFleeDone();
        Leaf leaf() const;
        float      memoryLeft() const { return m_memoryLeft; }

    private:
        HsmState   m_root{ "Root" };
        HsmState   m_wander{ "Wander" };
        HsmState   m_chase{ "Chase" };
        HsmState   m_memory{ "Memory" };
        HsmState   m_assist{ "Assist" };
        HsmState   m_flee{ "Flee" };
        HsmMachine m_machine;
        float      m_memoryLeft = 0.0f;
    };
} // namespace Dark::AI
