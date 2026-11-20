#pragma once

namespace Dark
{

    // Scaled sim time per frame is clamped here, after the time scale. A hitch at 8x cannot ask for two seconds.
    inline constexpr float kSimFrameMaxSeconds = 0.25f;
    // One sim tick is at most this long, so a tick never needs more than two 1/60 s physics steps.
    inline constexpr float kSimTickMaxSeconds = 1.0f / 30.0f;
    // Tick budget per frame. kSimFrameMaxSeconds / kSimTickMaxSeconds rounded up.
    inline constexpr int kSimTicksMax = 8;

    // How one frame's scaled sim time is cut into equal ticks.
    struct SimTickPlan
    {
        int   ticks   = 0;     // 0 when no sim time passes this frame
        float tickDt  = 0.0f;  // each tick, in sim seconds
        float total   = 0.0f;  // ticks * tickDt
        bool  clamped = false; // wallDt * scale went over kSimFrameMaxSeconds
    };

    // wallDt * scale, clamped to maxFrame, in the fewest equal ticks no longer than maxTick (at most maxTicks).
    // At 1x and 30 fps or faster this is one tick of wallDt, as before.
    SimTickPlan planSimTicks(float wallDt, float scale, float maxTick = kSimTickMaxSeconds, int maxTicks = kSimTicksMax, float maxFrame = kSimFrameMaxSeconds);

} // namespace Dark
