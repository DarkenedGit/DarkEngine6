#include "Editor/DialogueChart.h"

#include <algorithm>
#include <cmath>

namespace Dark
{
    namespace
    {
        constexpr float kNodeW = 168.0f;
        constexpr float kNodeH = 72.0f;

        ImVec2 toScreen(ImVec2 canvas, ImVec2 pan, float zoom, float x, float y)
        {
            return ImVec2(canvas.x + (x + pan.x) * zoom, canvas.y + (y + pan.y) * zoom);
        }

        int nodeAt(const DialogueGraphDef& graph, float x, float y)
        {
            for (int i = static_cast<int>(graph.nodes.size()) - 1; i >= 0; --i)
            {
                const DialogueNode& node = graph.nodes[static_cast<size_t>(i)];
                if (x >= node.x && y >= node.y && x <= node.x + kNodeW && y <= node.y + kNodeH)
                    return i;
            }
            return -1;
        }
    } // namespace

    bool drawDialogueChart(DialogueGraphDef& graph, int& selectedNode, ImVec2& pan, float& zoom)
    {
        const ImVec2 canvas = ImGui::GetCursorScreenPos();
        ImVec2 size = ImGui::GetContentRegionAvail();
        if (size.x < 40.0f)
            size.x = 40.0f;
        if (size.y < 160.0f)
            size.y = 160.0f;
        ImGui::InvisibleButton("dialogue_chart", size);
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 mouse = ImGui::GetIO().MousePos;

        if (hovered && ImGui::GetIO().MouseWheel != 0.0f)
        {
            const float beforeX = (mouse.x - canvas.x) / zoom - pan.x;
            const float beforeY = (mouse.y - canvas.y) / zoom - pan.y;
            zoom *= (ImGui::GetIO().MouseWheel > 0.0f) ? 1.12f : (1.0f / 1.12f);
            zoom = std::clamp(zoom, 0.25f, 2.8f);
            pan.x = (mouse.x - canvas.x) / zoom - beforeX;
            pan.y = (mouse.y - canvas.y) / zoom - beforeY;
        }
        if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || (ImGui::GetIO().KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left))))
        {
            pan.x += ImGui::GetIO().MouseDelta.x / zoom;
            pan.y += ImGui::GetIO().MouseDelta.y / zoom;
        }

        bool selectionChanged = false;
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyAlt)
        {
            const float x = (mouse.x - canvas.x) / zoom - pan.x;
            const float y = (mouse.y - canvas.y) / zoom - pan.y;
            const int hit = nodeAt(graph, x, y);
            if (hit != selectedNode)
            {
                selectedNode = hit;
                selectionChanged = true;
            }
        }
        if (selectedNode >= 0 && selectedNode < static_cast<int>(graph.nodes.size()) && hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyAlt
            && !ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        {
            DialogueNode& node = graph.nodes[static_cast<size_t>(selectedNode)];
            const float x = (mouse.x - canvas.x) / zoom - pan.x;
            const float y = (mouse.y - canvas.y) / zoom - pan.y;
            if (nodeAt(graph, x - ImGui::GetIO().MouseDelta.x / zoom, y - ImGui::GetIO().MouseDelta.y / zoom) == selectedNode)
            {
                node.x += ImGui::GetIO().MouseDelta.x / zoom;
                node.y += ImGui::GetIO().MouseDelta.y / zoom;
            }
        }

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(canvas, ImVec2(canvas.x + size.x, canvas.y + size.y), IM_COL32(18, 20, 24, 255));
        draw->PushClipRect(canvas, ImVec2(canvas.x + size.x, canvas.y + size.y), true);

        for (const DialogueNode& node : graph.nodes)
        {
            for (const DialogueChoice& choice : node.choices)
            {
                if (choice.next.empty())
                    continue;
                const DialogueNode* next = graph.findNode(choice.next);
                if (!next)
                    continue;
                const ImVec2 a = toScreen(canvas, pan, zoom, node.x + kNodeW, node.y + kNodeH * 0.5f);
                const ImVec2 b = toScreen(canvas, pan, zoom, next->x, next->y + kNodeH * 0.5f);
                draw->AddLine(a, b, IM_COL32(180, 160, 90, 255), 1.5f * zoom);
            }
        }

        for (int i = 0; i < static_cast<int>(graph.nodes.size()); ++i)
        {
            const DialogueNode& node = graph.nodes[static_cast<size_t>(i)];
            const ImVec2 a = toScreen(canvas, pan, zoom, node.x, node.y);
            const ImVec2 b = toScreen(canvas, pan, zoom, node.x + kNodeW, node.y + kNodeH);
            const bool selected = i == selectedNode;
            const bool entry = node.id == graph.entry;
            const ImU32 fill = selected ? IM_COL32(48, 58, 42, 255) : IM_COL32(32, 36, 42, 255);
            const ImU32 stroke = entry ? IM_COL32(214, 178, 92, 255) : IM_COL32(120, 128, 140, 255);
            draw->AddRectFilled(a, b, fill, 4.0f * zoom);
            draw->AddRect(a, b, stroke, 4.0f * zoom, 0, (selected ? 2.2f : 1.2f) * zoom);
            draw->AddText(ImVec2(a.x + 8.0f * zoom, a.y + 6.0f * zoom), IM_COL32(230, 228, 220, 255), node.id.c_str());
            if (!node.lines.empty())
                draw->AddText(ImVec2(a.x + 8.0f * zoom, a.y + 26.0f * zoom), IM_COL32(170, 174, 180, 255), node.lines[0].text.c_str());
        }
        draw->PopClipRect();
        return selectionChanged;
    }

} // namespace Dark
