#include "AI/HsmGraphComponent.h"

namespace Dark
{
    void syncPlayerHsm(HsmGraphInstance& graph, uint8_t motorState, bool alive)
    {
        if (!graph.isRunning())
            graph.start();
        if (!alive)
        {
            graph.processEventNamed("Die");
            return;
        }
        if (graph.isIn("Dead"))
            graph.processEventNamed("Respawn");

        // PlayerMoveState: Grounded=0, Jumping=1, Falling=2, Swimming=3
        switch (motorState)
        {
        case 1:
            graph.processEventNamed("Jump");
            break;
        case 2:
            graph.processEventNamed("Fall");
            break;
        case 3:
            graph.processEventNamed("EnterWater");
            break;
        case 0:
        default:
            graph.processEventNamed("Land");
            graph.processEventNamed("LeaveWater");
            break;
        }
    }
} // namespace Dark
