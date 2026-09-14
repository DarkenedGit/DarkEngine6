#pragma once

#include "AI/HsmGraph.h"

#include <cstdint>
#include <vector>

namespace Dark
{
    constexpr float kHsmChartLeafH    = 36.0f;
    constexpr float kHsmChartTitleH   = 22.0f;
    constexpr float kHsmChartPad      = 16.0f;
    constexpr float kHsmChartGap      = 14.0f;
    constexpr float kHsmChartMinLeafW = 110.0f;
    constexpr float kHsmChartCharW    = 8.0f;

    struct HsmChartBox
    {
        int   stateIndex = -1;
        int   depth      = 0;
        float x          = 0.0f;
        float y          = 0.0f;
        float w          = 0.0f;
        float h          = 0.0f;
    };

    struct HsmChartLayout
    {
        std::vector<HsmChartBox> boxes;
        float                    width  = 0.0f;
        float                    height = 0.0f;
    };

    // Nested left-to-right layout. Composite boxes contain their children.
    bool layoutHsmStatechart(const HsmGraphDef& def, HsmChartLayout& out);

    const HsmChartBox* findHsmChartBox(const HsmChartLayout& layout, int stateIndex);

    // Smallest (deepest) box containing (x, y) in chart space, or -1.
    int hitTestHsmChart(const HsmChartLayout& layout, float x, float y);
} // namespace Dark
