#include "Editor/EditorImGui.h"

#include <imgui.h>

namespace Dark::EditorImGuiHelpers
{
namespace
{
const char* clipNameAt(const AnimationSet* set, uint32_t index)
{
    if (!set)
        return "(none)";
    const AnimationClip* clip = set->clipAt(index);
    return clip ? clip->name.c_str() : "(invalid)";
}
} // namespace

bool comboString(const char* label, std::string& value, const std::vector<std::string>& items, bool allowEmpty)
{
    const char* preview = value.empty() ? (allowEmpty ? "(none)" : "(invalid)") : value.c_str();
    bool        changed = false;
    if (!ImGui::BeginCombo(label, preview))
        return false;
    if (allowEmpty)
    {
        const bool sel = value.empty();
        if (ImGui::Selectable("(none)", sel))
        {
            value   = {};
            changed = true;
        }
    }
    for (const std::string& item : items)
    {
        const bool sel = (item == value);
        if (ImGui::Selectable(item.c_str(), sel))
        {
            value   = item;
            changed = true;
        }
        if (sel)
            ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
    return changed;
}

bool comboClip(const char* label, const AnimationSet& set, uint32_t& clipIndex)
{
    const char* preview = clipNameAt(&set, clipIndex);
    bool        changed = false;
    if (!ImGui::BeginCombo(label, preview))
        return false;
    for (uint32_t i = 0; i < set.clipCount(); ++i)
    {
        const bool sel = (i == clipIndex);
        if (ImGui::Selectable(clipNameAt(&set, i), sel))
        {
            clipIndex = i;
            changed   = true;
        }
        if (sel)
            ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
    return changed;
}

bool comboState(const char* label, const AnimGraphDef& def, uint32_t& stateIndex, bool allowAny)
{
    const char* preview = "(invalid)";
    if (allowAny && stateIndex == kAnyState)
        preview = "*";
    else if (stateIndex < def.states.size())
        preview = def.states[stateIndex].name.c_str();

    bool changed = false;
    if (!ImGui::BeginCombo(label, preview))
        return false;
    if (allowAny)
    {
        const bool sel = (stateIndex == kAnyState);
        if (ImGui::Selectable("*", sel))
        {
            stateIndex = kAnyState;
            changed    = true;
        }
    }
    for (uint32_t i = 0; i < def.states.size(); ++i)
    {
        const bool sel = (stateIndex == i);
        if (ImGui::Selectable(def.states[i].name.c_str(), sel))
        {
            stateIndex = i;
            changed    = true;
        }
        if (sel)
            ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
    return changed;
}

} // namespace Dark::EditorImGuiHelpers
