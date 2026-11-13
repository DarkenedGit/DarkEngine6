#include "Ui/ImGuiWidgets.h"

#include "Ui/ImGuiTheme.h"

namespace
{
    int g_propTable = 0;

    struct PropRow
    {
        explicit PropRow(const char* label)
        {
            ImGui::PushID(label ? label : "##prop");
            if (g_propTable > 0)
            {
                ImGui::TableNextRow();
                ok = true;
            }
            else
            {
                owned = ImGui::BeginTable("##proprow", 2, ImGuiTableFlags_SizingStretchProp);
                if (owned)
                {
                    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.42f);
                    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.58f);
                    ImGui::TableNextRow();
                    ok = true;
                }
            }
            if (!ok)
                return;
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(Dark::toImVec4(Dark::UiPalette::kTextMuted), "%s", label ? label : "");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1.0f);
        }

        ~PropRow()
        {
            if (owned)
                ImGui::EndTable();
            ImGui::PopID();
        }

        bool ok    = false;
        bool owned = false;
    };
} // namespace

namespace Dark
{
    bool BeginPropTable(const char* id)
    {
        ImGui::PushID(id ? id : "##props");
        if (!ImGui::BeginTable("##props", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX))
        {
            ImGui::PopID();
            return false;
        }
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.42f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.58f);
        ++g_propTable;
        return true;
    }

    void EndPropTable()
    {
        if (g_propTable <= 0)
            return;
        ImGui::EndTable();
        ImGui::PopID();
        --g_propTable;
    }

    bool PropFloat(const char* label, float* v, float speed, float vMin, float vMax, const char* fmt, ImGuiSliderFlags flags)
    {
        PropRow row(label);
        if (!row.ok || !v)
            return false;
        return ImGui::DragFloat("##v", v, speed, vMin, vMax, fmt, flags);
    }

    bool PropFloat2(const char* label, float v[2], float speed, float vMin, float vMax, const char* fmt, ImGuiSliderFlags flags)
    {
        PropRow row(label);
        if (!row.ok || !v)
            return false;
        return ImGui::DragFloat2("##v", v, speed, vMin, vMax, fmt, flags);
    }

    bool PropFloat3(const char* label, float v[3], float speed, float vMin, float vMax, const char* fmt, ImGuiSliderFlags flags)
    {
        PropRow row(label);
        if (!row.ok || !v)
            return false;
        return ImGui::DragFloat3("##v", v, speed, vMin, vMax, fmt, flags);
    }

    bool PropSlider(const char* label, float* v, float vMin, float vMax, const char* fmt, ImGuiSliderFlags flags)
    {
        PropRow row(label);
        if (!row.ok || !v)
            return false;
        return ImGui::SliderFloat("##v", v, vMin, vMax, fmt, flags);
    }

    bool PropCheckbox(const char* label, bool* v)
    {
        PropRow row(label);
        if (!row.ok || !v)
            return false;
        return ImGui::Checkbox("##v", v);
    }

    bool PropCombo(const char* label, int* current, const char* const items[], int count)
    {
        PropRow row(label);
        if (!row.ok || !current || !items)
            return false;
        return ImGui::Combo("##v", current, items, count);
    }

    bool PropColor3(const char* label, float* col, ImGuiColorEditFlags flags)
    {
        PropRow row(label);
        if (!row.ok || !col)
            return false;
        return ImGui::ColorEdit3("##v", col, flags);
    }

    bool PropColor4(const char* label, float* col, ImGuiColorEditFlags flags)
    {
        PropRow row(label);
        if (!row.ok || !col)
            return false;
        return ImGui::ColorEdit4("##v", col, flags);
    }

    void PropSeparator(const char* label)
    {
        if (label && label[0])
            ImGui::SeparatorText(label);
        else
            ImGui::Separator();
    }
} // namespace Dark
