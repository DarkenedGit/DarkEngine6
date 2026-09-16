#pragma once

#include "Animation/AnimGraph.h"
#include "Animation/AnimationSet.h"
#include "Ui/ImGuiHost.h"

#include <string>
#include <vector>

using EditorImGui = ImGuiHost;

namespace Dark::EditorImGuiHelpers
{

// Shared ImGui combo widgets used by AnimEditorPanel and HsmEditorPanel.
bool comboString(const char* label, std::string& value, const std::vector<std::string>& items, bool allowEmpty);
bool comboClip(const char* label, const AnimationSet& set, uint32_t& clipIndex);
bool comboState(const char* label, const AnimGraphDef& def, uint32_t& stateIndex, bool allowAny);

} // namespace Dark::EditorImGuiHelpers
