#include "Editor/TranslateGizmo.h"

#include "Math/MathDefines.h"

#include <imgui.h>

namespace Dark::EditorDetail
{
    namespace
    {
        ImU32 axisColor(TranslateGizmoAxis axis, bool highlight)
        {
            if (highlight)
                return IM_COL32(255, 220, 64, 255);
            switch (axis)
            {
            case TranslateGizmoAxis::X:
            case TranslateGizmoAxis::YZ:
                return IM_COL32(232, 72, 72, 255);
            case TranslateGizmoAxis::Y:
            case TranslateGizmoAxis::XZ:
                return IM_COL32(72, 210, 88, 255);
            case TranslateGizmoAxis::Z:
            case TranslateGizmoAxis::XY:
                return IM_COL32(72, 140, 255, 255);
            default:
                return IM_COL32(230, 230, 230, 255);
            }
        }

        ImVec2 toDraw(const ImVec2& vpPos, const Math::Vector2f& s)
        {
            return ImVec2(vpPos.x + s.x, vpPos.y + s.y);
        }

        void addArrow(ImDrawList* dl, const ImVec2& from, const ImVec2& to, ImU32 col, float thickness)
        {
            dl->AddLine(from, to, col, thickness);
            const float dx = to.x - from.x;
            const float dy = to.y - from.y;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len < 8.0f)
                return;
            const float inv = 1.0f / len;
            const ImVec2 dir(dx * inv, dy * inv);
            const ImVec2 n(-dir.y, dir.x);
            const float head = Math::Clamp(len * 0.22f, 8.0f, 16.0f);
            const ImVec2 p1(to.x - dir.x * head + n.x * head * 0.45f, to.y - dir.y * head + n.y * head * 0.45f);
            const ImVec2 p2(to.x - dir.x * head - n.x * head * 0.45f, to.y - dir.y * head - n.y * head * 0.45f);
            dl->AddTriangleFilled(to, p1, p2, col);
        }
    } // namespace

    void drawTranslateGizmo(const Camera3D& cam, const Math::Vector3f& origin, float vw, float vh, const TranslateGizmoStyle& style)
    {
        ImDrawList* dl = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
        if (!dl)
            return;

        Math::Vector2f originS{};
        if (!projectToScreen(cam, origin, vw, vh, originS))
            return;

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const ImVec2 vpPos = vp ? vp->Pos : ImVec2(0.0f, 0.0f);
        const float size = gizmoWorldLength(cam, origin, vh, style.selected ? style.pixelLength : style.pixelLength * 0.62f);
        const float thickness = style.selected ? 3.2f : 2.0f;
        const uint8_t alpha = style.selected ? 255 : 180;

        constexpr float kAxis0 = 0.18f;
        constexpr float kAxis1 = 1.00f;
        constexpr float kPlane0 = 0.22f;
        constexpr float kPlane1 = 0.48f;

        const Math::Vector3f axes[3] = {
            Math::Vector3f(1.0f, 0.0f, 0.0f),
            Math::Vector3f(0.0f, 1.0f, 0.0f),
            Math::Vector3f(0.0f, 0.0f, 1.0f)
        };
        const TranslateGizmoAxis axisId[3] = { TranslateGizmoAxis::X, TranslateGizmoAxis::Y, TranslateGizmoAxis::Z };
        const TranslateGizmoAxis planeId[3] = { TranslateGizmoAxis::YZ, TranslateGizmoAxis::XZ, TranslateGizmoAxis::XY };

        if (style.selected)
        {
            const Math::Vector3f look = cam.GetLook();
            for (int i = 0; i < 3; ++i)
            {
                if (!gizmoPlaneFacingCamera(axes[i], look))
                    continue;
                const Math::Vector3f u = axes[(i + 1) % 3];
                const Math::Vector3f v = axes[(i + 2) % 3];
                Math::Vector2f a{}, b{}, c{}, d{};
                if (!projectToScreen(cam, origin + u * (size * kPlane0) + v * (size * kPlane0), vw, vh, a))
                    continue;
                if (!projectToScreen(cam, origin + u * (size * kPlane1) + v * (size * kPlane0), vw, vh, b))
                    continue;
                if (!projectToScreen(cam, origin + u * (size * kPlane1) + v * (size * kPlane1), vw, vh, c))
                    continue;
                if (!projectToScreen(cam, origin + u * (size * kPlane0) + v * (size * kPlane1), vw, vh, d))
                    continue;
                const bool hot = style.highlight == planeId[i];
                ImU32 fill = axisColor(planeId[i], hot);
                fill = (fill & 0x00FFFFFF) | (static_cast<ImU32>(hot ? 140 : 70) << 24);
                const ImVec2 pa = toDraw(vpPos, a);
                const ImVec2 pb = toDraw(vpPos, b);
                const ImVec2 pc = toDraw(vpPos, c);
                const ImVec2 pd = toDraw(vpPos, d);
                dl->AddTriangleFilled(pa, pb, pc, fill);
                dl->AddTriangleFilled(pa, pc, pd, fill);
                dl->AddQuad(pa, pb, pc, pd, axisColor(planeId[i], hot), 1.5f);
            }
        }

        for (int i = 0; i < 3; ++i)
        {
            Math::Vector2f a{}, b{};
            if (!projectToScreen(cam, origin + axes[i] * (size * kAxis0), vw, vh, a))
                continue;
            if (!projectToScreen(cam, origin + axes[i] * (size * kAxis1), vw, vh, b))
                continue;
            const bool hot = style.highlight == axisId[i];
            ImU32 col = axisColor(axisId[i], hot);
            if (!style.selected && !hot)
                col = (col & 0x00FFFFFF) | (static_cast<ImU32>(alpha) << 24);
            addArrow(dl, toDraw(vpPos, a), toDraw(vpPos, b), col, hot ? thickness + 1.0f : thickness);
        }

        const bool originHot = style.highlight == TranslateGizmoAxis::View;
        const float r = style.selected ? 6.0f : 4.0f;
        dl->AddCircleFilled(toDraw(vpPos, originS), r, originHot ? IM_COL32(255, 220, 64, 255) : IM_COL32(235, 235, 235, alpha), 16);
        dl->AddCircle(toDraw(vpPos, originS), r + 1.0f, IM_COL32(20, 20, 20, 180), 16, 1.25f);
    }
} // namespace Dark::EditorDetail
