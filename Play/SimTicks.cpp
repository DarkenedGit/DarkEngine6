#include "Play/SimTicks.h"

#include <cmath>

namespace Dark
{

    SimTickPlan planSimTicks(float wallDt, float scale, float maxTick, int maxTicks, float maxFrame)
    {
        SimTickPlan plan;
        if (!(wallDt > 0.0f) || !(scale > 0.0f) || !(maxTick > 0.0f) || maxTicks < 1)
            return plan;
        float       total  = wallDt * scale;
        const float budget = maxTick * static_cast<float>(maxTicks);
        const float cap    = (maxFrame > 0.0f && maxFrame < budget) ? maxFrame : budget;
        if (total > cap)
        {
            total        = cap;
            plan.clamped = true;
        }
        // A hair over a whole number of ticks is float noise, not one more tick.
        int ticks = static_cast<int>(std::ceil(total / maxTick - 1.0e-4f));
        if (ticks < 1)
            ticks = 1;
        if (ticks > maxTicks)
            ticks = maxTicks;
        plan.ticks  = ticks;
        plan.tickDt = total / static_cast<float>(ticks);
        plan.total  = total;
        return plan;
    }

} // namespace Dark
