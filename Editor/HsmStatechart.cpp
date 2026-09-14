#include "Editor/HsmStatechart.h"
#include "Core/UiPalette.h"

#include <cmath>
#include <string>
#include <vector>

namespace Dark
{
    namespace
    {
        ImU32 toU32(const UiColor& c, float a = 1.0f)
        {
            const float aa = c.a * a;
            return IM_COL32(static_cast<int>(c.r * 255.0f + 0.5f), static_cast<int>(c.g * 255.0f + 0.5f), static_cast<int>(c.b * 255.0f + 0.5f),
                            static_cast<int>(aa * 255.0f + 0.5f));
        }

        ImVec2 chartToScreen(ImVec2 canvas, ImVec2 pan, float zoom, float x, float y)
        {
            return ImVec2(canvas.x + (x + pan.x) * zoom, canvas.y + (y + pan.y) * zoom);
        }

        ImVec2 boxCenter(const HsmChartBox& b)
        {
            return ImVec2(b.x + b.w * 0.5f, b.y + b.h * 0.5f);
        }

        ImVec2 edgeToward(const HsmChartBox& b, ImVec2 target)
        {
            const ImVec2 c  = boxCenter(b);
            const float  dx = target.x - c.x;
            const float  dy = target.y - c.y;
            const float  hx = b.w * 0.5f;
            const float  hy = b.h * 0.5f;
            if (fabsf(dx) < 1.0e-4f && fabsf(dy) < 1.0e-4f)
                return c;
            const float sx = (fabsf(dx) > 1.0e-4f) ? (hx / fabsf(dx)) : 1.0e9f;
            const float sy = (fabsf(dy) > 1.0e-4f) ? (hy / fabsf(dy)) : 1.0e9f;
            const float t  = (sx < sy) ? sx : sy;
            return ImVec2(c.x + dx * t, c.y + dy * t);
        }

        void drawArrowHead(ImDrawList* dl, ImVec2 tip, ImVec2 dir, float zoom, ImU32 col)
        {
            const float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
            if (len < 1.0e-4f)
                return;
            const float  inv = 1.0f / len;
            const ImVec2 n(dir.x * inv, dir.y * inv);
            const ImVec2 p(-n.y, n.x);
            const float  s = 8.0f * zoom;
            dl->AddTriangleFilled(tip, ImVec2(tip.x - n.x * s + p.x * s * 0.45f, tip.y - n.y * s + p.y * s * 0.45f),
                                  ImVec2(tip.x - n.x * s - p.x * s * 0.45f, tip.y - n.y * s - p.y * s * 0.45f), col);
        }

        void drawTransition(ImDrawList* dl, ImVec2 canvas, ImVec2 pan, float zoom, const HsmChartBox& from, const HsmChartBox& to, const char* label, bool active, int offset)
        {
            const ImU32 col = active ? toU32(UiPalette::kAccentEditor) : toU32(UiPalette::kTextMuted, 0.85f);
            const ImVec2 fc = boxCenter(from);
            const ImVec2 tc = boxCenter(to);
            ImVec2       a  = edgeToward(from, tc);
            ImVec2       b  = edgeToward(to, fc);
            if (from.stateIndex == to.stateIndex)
            {
                a = ImVec2(from.x + from.w, from.y + from.h * 0.35f);
                b = ImVec2(from.x + from.w * 0.72f, from.y);
                const ImVec2 as = chartToScreen(canvas, pan, zoom, a.x, a.y);
                const ImVec2 bs = chartToScreen(canvas, pan, zoom, b.x, b.y);
                const float  r  = 22.0f * zoom;
                dl->AddBezierCubic(as, ImVec2(as.x + r, as.y - r * 0.2f), ImVec2(bs.x + r, bs.y - r), bs, col, 1.5f * zoom);
                drawArrowHead(dl, bs, ImVec2(-1.0f, 0.4f), zoom, col);
                if (label && label[0])
                    dl->AddText(ImVec2((as.x + bs.x) * 0.5f + 6.0f * zoom, (as.y + bs.y) * 0.5f - 18.0f * zoom), col, label);
                return;
            }

            const float  bump = (8.0f + static_cast<float>(offset) * 6.0f);
            const ImVec2 as   = chartToScreen(canvas, pan, zoom, a.x, a.y);
            const ImVec2 bs   = chartToScreen(canvas, pan, zoom, b.x, b.y);
            ImVec2       mid((as.x + bs.x) * 0.5f, (as.y + bs.y) * 0.5f);
            ImVec2       d(bs.x - as.x, bs.y - as.y);
            const float  len = sqrtf(d.x * d.x + d.y * d.y);
            if (len > 1.0f)
            {
                d.x /= len;
                d.y /= len;
            }
            const ImVec2 perp(-d.y, d.x);
            mid.x += perp.x * bump * zoom;
            mid.y += perp.y * bump * zoom;
            dl->AddBezierQuadratic(as, mid, bs, col, 1.6f * zoom);
            drawArrowHead(dl, bs, ImVec2(bs.x - mid.x, bs.y - mid.y), zoom, col);
            if (label && label[0])
            {
                const ImVec2 ts = ImGui::CalcTextSize(label);
                dl->AddText(ImVec2(mid.x - ts.x * 0.5f, mid.y - ts.y - 2.0f), col, label);
            }
        }
    } // namespace

    bool drawHsmStatechart(const HsmGraphDef& def, const HsmGraphInstance* preview, int& selectedState, ImVec2& pan, float& zoom, bool fitNow)
    {
        HsmChartLayout layout;
        if (!layoutHsmStatechart(def, layout))
        {
            ImGui::TextDisabled("No states to layout.");
            return false;
        }

        ImGui::TextDisabled("Nested boxes are composite states. Gold outline is the live configuration. Wheel zoom, MMB pan.");
        if (zoom < 0.2f)
            zoom = 1.0f;

        const ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
        ImVec2       canvasSize = ImGui::GetContentRegionAvail();
        if (canvasSize.x < 80.0f)
            canvasSize.x = 80.0f;
        if (canvasSize.y < 120.0f)
            canvasSize.y = 120.0f;

        if (fitNow && layout.width > 1.0f && layout.height > 1.0f)
        {
            const float zx = (canvasSize.x - 16.0f) / layout.width;
            const float zy = (canvasSize.y - 16.0f) / layout.height;
            zoom           = (zx < zy) ? zx : zy;
            if (zoom > 1.4f)
                zoom = 1.4f;
            if (zoom < 0.25f)
                zoom = 0.25f;
            pan.x = (canvasSize.x / zoom - layout.width) * 0.5f;
            pan.y = (canvasSize.y / zoom - layout.height) * 0.5f;
        }

        ImGui::InvisibleButton("hsm_chart_canvas", canvasSize);
        const bool hovered = ImGui::IsItemHovered();

        if (hovered && ImGui::GetIO().MouseWheel != 0.0f)
        {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const ImVec2 before((mouse.x - canvasPos.x) / zoom - pan.x, (mouse.y - canvasPos.y) / zoom - pan.y);
            zoom *= (ImGui::GetIO().MouseWheel > 0.0f) ? 1.12f : (1.0f / 1.12f);
            if (zoom < 0.25f)
                zoom = 0.25f;
            if (zoom > 2.8f)
                zoom = 2.8f;
            pan.x = (mouse.x - canvasPos.x) / zoom - before.x;
            pan.y = (mouse.y - canvasPos.y) / zoom - before.y;
        }
        if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || (ImGui::GetIO().KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left))))
        {
            pan.x += ImGui::GetIO().MouseDelta.x / zoom;
            pan.y += ImGui::GetIO().MouseDelta.y / zoom;
        }

        bool changed = false;
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyAlt)
        {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const float  cx    = (mouse.x - canvasPos.x) / zoom - pan.x;
            const float  cy    = (mouse.y - canvasPos.y) / zoom - pan.y;
            const int    hit   = hitTestHsmChart(layout, cx, cy);
            if (hit >= 0 && hit != selectedState)
            {
                selectedState = hit;
                changed       = true;
            }
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);
        dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), toU32(UiPalette::kVoid));
        dl->AddRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), toU32(UiPalette::kBorder));

        const float grid = 24.0f * zoom;
        if (grid >= 8.0f)
        {
            const ImU32 gc = toU32(UiPalette::kBorder, 0.25f);
            const float ox = fmodf(pan.x * zoom, grid);
            const float oy = fmodf(pan.y * zoom, grid);
            for (float x = ox; x < canvasSize.x; x += grid)
                dl->AddLine(ImVec2(canvasPos.x + x, canvasPos.y), ImVec2(canvasPos.x + x, canvasPos.y + canvasSize.y), gc);
            for (float y = oy; y < canvasSize.y; y += grid)
                dl->AddLine(ImVec2(canvasPos.x, canvasPos.y + y), ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + y), gc);
        }

        for (const HsmChartBox& box : layout.boxes)
        {
            if (box.stateIndex < 0 || static_cast<uint32_t>(box.stateIndex) >= def.states.size())
                continue;
            const HsmStateDef& st       = def.states[static_cast<uint32_t>(box.stateIndex)];
            const bool         isLeaf   = box.h <= kHsmChartLeafH + 0.5f;
            const bool         selected = (box.stateIndex == selectedState);
            const bool         live     = preview && preview->isIn(st.name);
            const ImVec2       a        = chartToScreen(canvasPos, pan, zoom, box.x, box.y);
            const ImVec2       b        = chartToScreen(canvasPos, pan, zoom, box.x + box.w, box.y + box.h);
            ImU32 fill = toU32(isLeaf ? UiPalette::kRaised : UiPalette::kPanel, selected ? 1.0f : 0.92f);
            if (live)
                fill = toU32(UiPalette::kHover);
            const ImU32 stroke = live ? toU32(UiPalette::kAccentEditor) : (selected ? toU32(UiPalette::kAccentEditor, 0.85f) : toU32(UiPalette::kBorder));
            dl->AddRectFilled(a, b, fill, 6.0f * zoom);
            dl->AddRect(a, b, stroke, 6.0f * zoom, 0, (live ? 2.4f : 1.2f) * zoom);
            dl->AddText(ImVec2(a.x + 8.0f * zoom, a.y + 4.0f * zoom), toU32(UiPalette::kText), st.name.c_str());
            if (st.history != AI::HsmHistory::None)
            {
                const char* h = (st.history == AI::HsmHistory::Deep) ? "H*" : "H";
                const ImVec2 ts = ImGui::CalcTextSize(h);
                dl->AddText(ImVec2(b.x - ts.x - 8.0f * zoom, a.y + 4.0f * zoom), toU32(UiPalette::kAccentEditor), h);
            }
        }

        int offset = 0;
        for (const HsmTransitionDef& t : def.transitions)
        {
            const int toIdx = def.findStateIndex(t.to);
            if (toIdx < 0)
                continue;
            const HsmChartBox* toBox = findHsmChartBox(layout, toIdx);
            if (!toBox)
                continue;
            std::vector<int> fromIdx;
            if (t.from.size() == 1 && t.from[0] == "*")
            {
                const int root = def.findStateIndex(def.root);
                if (root >= 0)
                    fromIdx.push_back(root);
            }
            else
            {
                for (const std::string& name : t.from)
                {
                    const int fi = def.findStateIndex(name);
                    if (fi >= 0)
                        fromIdx.push_back(fi);
                }
            }
            const bool liveEdge = preview && preview->isIn(t.to) && [&]() {
                for (int fi : fromIdx)
                {
                    if (fi >= 0 && static_cast<uint32_t>(fi) < def.states.size() && preview->isIn(def.states[static_cast<uint32_t>(fi)].name))
                        return true;
                }
                return false;
            }();
            for (int fi : fromIdx)
            {
                const HsmChartBox* fromBox = findHsmChartBox(layout, fi);
                if (!fromBox)
                    continue;
                const bool self = (t.kind == AI::HsmTransitionKind::Internal) || (fi == toIdx);
                drawTransition(dl, canvasPos, pan, zoom, *fromBox, self ? *fromBox : *toBox, t.event.c_str(), liveEdge, offset++);
            }
        }

        for (const HsmChartBox& box : layout.boxes)
        {
            if (box.stateIndex < 0 || static_cast<uint32_t>(box.stateIndex) >= def.states.size())
                continue;
            const HsmStateDef& st = def.states[static_cast<uint32_t>(box.stateIndex)];
            if (st.initial.empty())
                continue;
            const int initIdx = def.findStateIndex(st.initial);
            const HsmChartBox* initBox = findHsmChartBox(layout, initIdx);
            if (!initBox)
                continue;
            const float  r  = 5.0f * zoom;
            const ImVec2 p0 = chartToScreen(canvasPos, pan, zoom, initBox->x - 14.0f, initBox->y + initBox->h * 0.5f);
            const ImVec2 p1 = chartToScreen(canvasPos, pan, zoom, initBox->x, initBox->y + initBox->h * 0.5f);
            dl->AddCircleFilled(p0, r, toU32(UiPalette::kText));
            dl->AddLine(p0, p1, toU32(UiPalette::kText), 1.5f * zoom);
            drawArrowHead(dl, p1, ImVec2(1.0f, 0.0f), zoom, toU32(UiPalette::kText));
        }

        dl->PopClipRect();
        ImGui::SetCursorScreenPos(ImVec2(canvasPos.x, canvasPos.y + canvasSize.y + 4.0f));
        ImGui::Dummy(ImVec2(1.0f, 1.0f));
        return changed;
    }
} // namespace Dark
