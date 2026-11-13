#pragma once

// ImGui property rows for Editor, Sandbox, and VisualDebugger.
// Do not include this from a DarkEngine translation unit.

#include <imgui.h>

namespace Dark
{
    // Shared two-column table. Prop* uses it when one is open, otherwise each call is its own row.
    bool BeginPropTable(const char* id);
    void EndPropTable();

    bool PropFloat(const char* label, float* v, float speed = 1.0f, float vMin = 0.0f, float vMax = 0.0f, const char* fmt = "%.3f", ImGuiSliderFlags flags = 0);
    bool PropFloat2(const char* label, float v[2], float speed = 1.0f, float vMin = 0.0f, float vMax = 0.0f, const char* fmt = "%.3f", ImGuiSliderFlags flags = 0);
    bool PropFloat3(const char* label, float v[3], float speed = 1.0f, float vMin = 0.0f, float vMax = 0.0f, const char* fmt = "%.3f", ImGuiSliderFlags flags = 0);
    bool PropSlider(const char* label, float* v, float vMin, float vMax, const char* fmt = "%.3f", ImGuiSliderFlags flags = 0);
    bool PropCheckbox(const char* label, bool* v);
    bool PropCombo(const char* label, int* current, const char* const items[], int count);
    bool PropColor3(const char* label, float* col, ImGuiColorEditFlags flags = 0);
    bool PropColor4(const char* label, float* col, ImGuiColorEditFlags flags = 0);
    void PropSeparator(const char* label);
} // namespace Dark
