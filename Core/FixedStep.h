#pragma once

namespace Dark
{

    // Fixed-step accumulator. advance() adds dt and returns how many whole steps to run now.
    // At maxSteps the leftover time is dropped so a long hitch cannot spiral; dropped() reports it.
    struct FixedStepAccumulator
    {
        float accum       = 0.0f;
        bool  lastDropped = false;

        int advance(float dt, float step, int maxSteps)
        {
            lastDropped = false;
            if (!(dt > 0.0f) || !(step > 0.0f) || maxSteps <= 0)
                return 0;
            accum += dt;
            int steps = 0;
            while (accum >= step && steps < maxSteps)
            {
                accum -= step;
                ++steps;
            }
            if (steps == maxSteps && accum >= step)
            {
                accum       = 0.0f;
                lastDropped = true;
            }
            return steps;
        }

        void reset()
        {
            accum       = 0.0f;
            lastDropped = false;
        }
    };

} // namespace Dark
