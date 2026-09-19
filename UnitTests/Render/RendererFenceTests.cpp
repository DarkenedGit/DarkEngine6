#include <gtest/gtest.h>

#include "Render/Renderer.h"

using namespace Dark;

TEST(RendererFence, ResizeMustNotLeaveAStaleSlot)
{
    // After waitForGpu on slot 0 the next signal is 4, but slot 1 still holds 2
    // (last signaled on the fence was 3). GetCurrentBackBufferIndex() may land
    // on slot 1; signaling 2 is illegal and can hang the INFINITE fence wait.
    uint64_t values[2] = { 4, 2 };
    Renderer::broadcastFenceValueAfterWait(values, 2, 0);
    EXPECT_EQ(values[0], 4u);
    EXPECT_EQ(values[1], 4u);
}

TEST(RendererFence, BroadcastIgnoresOutOfRangeIndex)
{
    uint64_t values[2] = { 4, 2 };
    Renderer::broadcastFenceValueAfterWait(values, 2, 9);
    EXPECT_EQ(values[0], 4u);
    EXPECT_EQ(values[1], 2u);
}

TEST(RendererFence, DepthClearValueIsZero)
{
    float (Renderer::*fn)() const = &Renderer::depthClearValue;
    (void)fn;
    EXPECT_FLOAT_EQ(kDepthClear, 0.0f);
}
