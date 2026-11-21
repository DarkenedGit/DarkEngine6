#include "Core/FixedStep.h"
#include "Play/SimTicks.h"
#include <gtest/gtest.h>

using namespace Dark;

TEST(SimTicks, EightTimesDoesNotOutrunPhysics)
{
    FixedStepAccumulator physics;
    float                simSeconds   = 0.0f;
    int                  physicsSteps = 0;
    for (int frame = 0; frame < 60; ++frame)
    {
        const SimTickPlan plan = planSimTicks(1.0f / 60.0f, 8.0f);
        EXPECT_LE(plan.ticks, kSimTicksMax);
        EXPECT_LE(plan.tickDt, kSimTickMaxSeconds + 1.0e-6f);
        EXPECT_FALSE(plan.clamped);
        for (int i = 0; i < plan.ticks; ++i)
        {
            simSeconds += plan.tickDt;
            physicsSteps += physics.advance(plan.tickDt, 1.0f / 60.0f, 5);
            EXPECT_FALSE(physics.lastDropped);
        }
    }
    EXPECT_NEAR(simSeconds, 8.0f, 0.0001f);
    EXPECT_EQ(physicsSteps, 480);
}

TEST(SimTicks, ClampIsAfterScaling)
{
    const SimTickPlan plan = planSimTicks(0.25f, 8.0f);
    EXPECT_FLOAT_EQ(plan.total, 0.25f);
    EXPECT_EQ(plan.ticks, 8);
    EXPECT_TRUE(plan.clamped);
    EXPECT_EQ(planSimTicks(0.0f, 8.0f).ticks, 0);
    EXPECT_EQ(planSimTicks(-1.0f, 8.0f).ticks, 0);
}

TEST(FixedStep, KeepsFractionAcrossFramesAndAtExactBudget)
{
    FixedStepAccumulator fixed;
    EXPECT_EQ(fixed.advance(0.005f, 0.01f, 5), 0);
    EXPECT_EQ(fixed.advance(0.045f, 0.01f, 5), 5);
    EXPECT_FALSE(fixed.lastDropped);
    EXPECT_EQ(fixed.advance(0.015f, 0.01f, 1), 1);
    EXPECT_NEAR(fixed.accum, 0.005f, 1.0e-6f);
}

TEST(FixedStep, DropsOnlyWholeTickOverBudget)
{
    FixedStepAccumulator fixed;
    EXPECT_EQ(fixed.advance(1.0f, 0.01f, 5), 5);
    EXPECT_TRUE(fixed.lastDropped);
    EXPECT_FLOAT_EQ(fixed.accum, 0.0f);
    fixed.reset();
    EXPECT_FALSE(fixed.lastDropped);
}
