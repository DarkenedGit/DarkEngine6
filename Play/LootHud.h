#pragma once

// ImGui loot card shared by Sandbox and Editor.
// Do not include this from a DarkEngine translation unit. Play/*.cpp is compiled into
// DarkEngine, and DarkEngine does not link Dear ImGui. UnitTests must not include it either.
//
// takeAllKey is the pickup key. The host leaves it false on the frame the card opens,
// so the press that opened the window does not also empty it.

#include "Gameplay/Inventory.h"
#include "Gameplay/Item.h"

#include "Ui/ImGuiGameChrome.h"

#include <imgui.h>

namespace Dark
{

    // Draws the loot card. grant returns how many of the stack moved into the bag.
    // A take-all (button or takeAllKey) closes the card. A single Take leaves it open.
    // Returns false when the card should close.
    template <typename Grant>
    bool drawLootCard(LootComponent& loot, UiAccent accent, const char* takeAllLabel, bool takeAllKey, Grant&& grant)
    {
        bool        open  = true;
        const char* title = (loot.label && loot.label[0] != '\0') ? loot.label : "Loot";
        const char* all   = (takeAllLabel && takeAllLabel[0] != '\0') ? takeAllLabel : "Take all";
        ImGui::SetNextWindowSize(ImVec2(uiPx(320.0f), uiPx(300.0f)), ImGuiCond_FirstUseEver);
        const bool shown   = BeginGameCard(title, &open, accent);
        const bool takeAll = takeAllKey || (shown && ImGui::Button(all));
        if (shown)
            ImGui::Separator();
        for (int i = 0; i < InventoryComponent::kBagSize; ++i)
        {
            ItemStack& item = loot.contents.bag[i];
            if (item.empty() || !item.def)
                continue;
            bool takeOne = false;
            if (shown)
            {
                ImGui::PushID(i);
                ImGui::Text("%s x%d (%s)", item.def->name, item.count, itemTypeName(item.def->type));
                ImGui::SameLine();
                takeOne = ImGui::SmallButton("Take");
                ImGui::PopID();
            }
            if (takeOne || takeAll)
            {
                item.count -= grant(item);
                if (item.count <= 0)
                    item = {};
            }
        }
        if (shown && itemCount(loot.contents) == 0)
            ImGui::TextUnformatted("Empty");
        if (takeAll)
            open = false;
        EndGameCard();
        return open;
    }

} // namespace Dark
