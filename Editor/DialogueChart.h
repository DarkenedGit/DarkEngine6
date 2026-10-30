#pragma once

#include "Play/DialogueGraph.h"

#include <imgui.h>

namespace Dark
{

    // Node boxes at DialogueNode::x/y. Wheel zoom, MMB or Alt-LMB pan, drag moves the selected node.
    // Returns true when the selected node index changes.
    bool drawDialogueChart(DialogueGraphDef& graph, int& selectedNode, ImVec2& pan, float& zoom);

} // namespace Dark
