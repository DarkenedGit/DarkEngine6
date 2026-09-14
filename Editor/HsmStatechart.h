#pragma once

#include "AI/HsmGraph.h"
#include "AI/HsmStatechartLayout.h"

#include <imgui.h>

namespace Dark
{
    // Nested UML-style statechart. Wheel zoom, MMB/Alt-LMB pan, click selects a state.
    // Returns true if selectedState changed.
    bool drawHsmStatechart(const HsmGraphDef& def, const HsmGraphInstance* preview, int& selectedState, ImVec2& pan, float& zoom, bool fitNow);
} // namespace Dark
