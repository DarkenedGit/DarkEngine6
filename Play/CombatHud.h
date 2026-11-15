#pragma once

// ImGui health bar and crosshair shared by Sandbox and Editor.
// Do not include this from a DarkEngine translation unit. Play/*.cpp is compiled into
// DarkEngine, and DarkEngine does not link Dear ImGui. UnitTests must not include it either.

#include "Core/UiPalette.h"
#include "Math/MathHelper.h"
#include "Ui/ImGuiHost.h"
#include "Ui/ImGuiTheme.h"
#include "Weapons/Weapon.h"

#include <imgui.h>

namespace Dark
{

    inline void fillHudRect(ImDrawList* list, float cx, float cy, float w, float h, ImU32 color)
    {
        if (!list || w <= 0.0f || h <= 0.0f)
            return;
        list->AddRectFilled(ImVec2(cx - w * 0.5f, cy - h * 0.5f), ImVec2(cx + w * 0.5f, cy + h * 0.5f), color);
    }

    // Lower-right bar. ratio is 0..1.
    inline void drawHealthBar(float ratio)
    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        if (!vp)
            return;
        ImDrawList* list = ImGui::GetForegroundDrawList(const_cast<ImGuiViewport*>(vp));
        if (!list)
            return;

        const float pad = uiPx(28.0f);
        float       barW = uiPx(280.0f);
        float       barH = uiPx(22.0f);
        float       border = uiPx(3.0f);
        const float maxW = vp->Size.x - pad * 2.0f;
        const float maxH = vp->Size.y - pad * 2.0f;
        if (maxW < 8.0f || maxH < 4.0f)
            return;
        if (barW > maxW)
            barW = maxW;
        if (barH > maxH)
            barH = maxH;
        if (border * 2.0f >= barH || border * 2.0f >= barW)
            border = 1.0f;

        const float x0 = vp->Pos.x + vp->Size.x - pad - barW;
        const float y0 = vp->Pos.y + vp->Size.y - pad - barH;
        const float fill = Math::Clamp(ratio, 0.0f, 1.0f);
        const UiColor bar = UiPalette::healthFill(fill);

        list->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + barW, y0 + barH), toImU32(UiPalette::kVoid.withAlpha(0.82f)));
        const float innerX = x0 + border;
        const float innerY = y0 + border;
        const float innerW = barW - border * 2.0f;
        const float innerH = barH - border * 2.0f;
        list->AddRectFilled(ImVec2(innerX, innerY), ImVec2(innerX + innerW, innerY + innerH), toImU32(UiPalette::kInset.withAlpha(0.90f)));
        if (fill > 1.0e-4f)
            list->AddRectFilled(ImVec2(innerX, innerY), ImVec2(innerX + innerW * fill, innerY + innerH), toImU32(bar.withAlpha(0.95f)));
    }

    // Screen-center reticle plus the melee / gun / throwable cells under it.
    inline void drawCrosshair(WeaponKind active, bool throwableSlot)
    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        if (!vp)
            return;
        ImDrawList* list = ImGui::GetForegroundDrawList(const_cast<ImGuiViewport*>(vp));
        if (!list)
            return;

        const float cx = vp->Pos.x + vp->Size.x * 0.5f;
        const float cy = vp->Pos.y + vp->Size.y * 0.5f;

        UiColor accent = UiPalette::kText;
        if (active == WeaponKind::Projectile)
            accent = UiPalette::kWarn;
        else if (active == WeaponKind::Throwable)
            accent = UiPalette::kOk;
        const UiColor dim = UiPalette::kTextMuted;

        const float arm = uiPx(11.0f);
        const float thick = uiPx(2.0f);
        const float gap = uiPx(5.0f);
        const ImU32 mark = toImU32(accent.withAlpha(0.92f));
        fillHudRect(list, cx - gap - arm * 0.5f, cy, arm, thick, mark);
        fillHudRect(list, cx + gap + arm * 0.5f, cy, arm, thick, mark);
        fillHudRect(list, cx, cy + gap + arm * 0.5f, thick, arm, mark);
        fillHudRect(list, cx, cy - gap - arm * 0.5f, thick, arm, mark);
        fillHudRect(list, cx, cy, uiPx(2.0f), uiPx(2.0f), toImU32(accent.withAlpha(0.85f)));

        const float slotY = cy + uiPx(28.0f);
        const float cell = uiPx(7.0f);
        const UiColor meleeCol = active == WeaponKind::Melee ? UiPalette::kText : dim;
        const UiColor gunCol = active == WeaponKind::Projectile ? UiPalette::kWarn : dim;
        const float meleeA = active == WeaponKind::Melee ? 0.95f : 0.45f;
        const float gunA = active == WeaponKind::Projectile ? 0.95f : 0.45f;
        if (!throwableSlot)
        {
            fillHudRect(list, cx - uiPx(10.0f), slotY, cell, cell, toImU32(meleeCol.withAlpha(meleeA)));
            fillHudRect(list, cx + uiPx(10.0f), slotY, cell, cell, toImU32(gunCol.withAlpha(gunA)));
            return;
        }

        const UiColor throwCol = active == WeaponKind::Throwable ? UiPalette::kOk : dim;
        const float throwA = active == WeaponKind::Throwable ? 0.95f : 0.45f;
        fillHudRect(list, cx - uiPx(18.0f), slotY, cell, cell, toImU32(meleeCol.withAlpha(meleeA)));
        fillHudRect(list, cx, slotY, cell, cell, toImU32(gunCol.withAlpha(gunA)));
        fillHudRect(list, cx + uiPx(18.0f), slotY, cell, cell, toImU32(throwCol.withAlpha(throwA)));
    }

} // namespace Dark
