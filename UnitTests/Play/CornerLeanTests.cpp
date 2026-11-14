#include <gtest/gtest.h>

#include "Play/PlayLean.h"

#include <cmath>
#include <limits>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

using namespace Dark;

namespace
{

// Debug DE_ASSERT breaks. Continue so the non-finite path can return 0.
LONG WINAPI continuePastBreakpoint(EXCEPTION_POINTERS* info)
{
    if (!info || !info->ExceptionRecord || !info->ContextRecord || info->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT)
        return EXCEPTION_CONTINUE_SEARCH;
    // int 3 is one byte and the context still points at it.
    info->ContextRecord->Rip += 1;
    return EXCEPTION_CONTINUE_EXECUTION;
}

struct BreakpointContinue
{
    BreakpointContinue()
    {
        handle = AddVectoredExceptionHandler(1, continuePastBreakpoint);
    }

    ~BreakpointContinue()
    {
        if (handle)
            RemoveVectoredExceptionHandler(handle);
    }

    BreakpointContinue(const BreakpointContinue&)            = delete;
    BreakpointContinue& operator=(const BreakpointContinue&) = delete;

    void* handle = nullptr;
};

} // namespace

TEST(CornerLean, Target)
{
    EXPECT_FLOAT_EQ(cornerLeanTarget(true, false, true), -1.0f);
    EXPECT_FLOAT_EQ(cornerLeanTarget(false, true, true), 1.0f);
    EXPECT_FLOAT_EQ(cornerLeanTarget(true, true, true), 0.0f);
    EXPECT_FLOAT_EQ(cornerLeanTarget(false, false, true), 0.0f);
    EXPECT_FLOAT_EQ(cornerLeanTarget(true, false, false), 0.0f);
    EXPECT_FLOAT_EQ(cornerLeanTarget(false, true, false), 0.0f);
    EXPECT_FLOAT_EQ(cornerLeanTarget(true, true, false), 0.0f);
}

TEST(CornerLean, StepHoldAndRelease)
{
    const float held = stepCornerLean(0.0f, 1.0f, kCornerLeanSeconds);
    EXPECT_NEAR(held, 1.0f, 1.0e-3f);

    const float released = stepCornerLean(held, 0.0f, kCornerLeanSeconds);
    EXPECT_NEAR(released, 0.0f, 1.0e-3f);

    EXPECT_FLOAT_EQ(stepCornerLean(0.35f, 1.0f, 0.0f), 0.35f);
    EXPECT_FLOAT_EQ(stepCornerLean(0.35f, -1.0f, -0.05f), 0.35f);

    const float half = stepCornerLean(0.0f, 1.0f, kCornerLeanSeconds * 0.5f);
    EXPECT_NEAR(half, 0.5f, 1.0e-3f);

    const BreakpointContinue breakpoints;
    EXPECT_FLOAT_EQ(stepCornerLean(std::numeric_limits<float>::quiet_NaN(), 1.0f, 0.016f), 0.0f);
    EXPECT_FLOAT_EQ(stepCornerLean(0.25f, std::numeric_limits<float>::infinity(), 0.016f), 0.0f);
    EXPECT_FLOAT_EQ(stepCornerLean(0.25f, 1.0f, std::numeric_limits<float>::quiet_NaN()), 0.0f);
}

TEST(CornerLean, FractionTracksTheSmoothedLean)
{
    constexpr float boom  = -0.42f;
    constexpr float reach = 0.70f;

    EXPECT_FLOAT_EQ(leanedLateral(boom, 0.0f, reach), -0.42f);
    EXPECT_NEAR(leanedLateral(boom, 1.0f, reach), 0.70f, 1.0e-5f);
    EXPECT_NEAR(leanedLateral(boom, -1.0f, reach), -0.70f, 1.0e-5f);
    EXPECT_NEAR(leanedLateral(boom, 0.5f, reach), 0.14f, 1.0e-5f);

    const float halfLateral = leanedLateral(boom, 0.5f, reach);
    EXPECT_NEAR(leanFractionForLateral(boom, halfLateral, 0.5f, reach), 0.5f, 1.0e-4f);

    const float clamped = leanFractionForLateral(boom, 0.33f, 1.0f, reach);
    EXPECT_NEAR(clamped, 0.75f / 1.12f, 1.0e-4f);
    EXPECT_GT(std::fabs(clamped - 1.0f), 0.2f);
    EXPECT_GT(std::fabs(clamped - (0.33f / 0.42f)), 0.05f);

    EXPECT_FLOAT_EQ(leanFractionForLateral(boom, 0.33f, 0.0f, reach), 0.0f);
}
