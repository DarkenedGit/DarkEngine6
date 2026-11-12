#include "Audio/SoundComponents.h"
#include "Combat/ArmorPieces.h"
#include "SandboxHud.h"
#include "SandboxApp.h"

#include "AI/AiComponents.h"
#include "Character/HealthComponent.h"
#include "Character/SkillComponent.h"
#include "ECS/Components.h"
#include "Combat/ArmorStats.h"
#include "Character/ItemView.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/PowerComponents.h"
#include "Gameplay/QuestZone.h"
#include "Play/Carry.h"
#include "Media/MovieHud.h"
#include "Play/DialogueHud.h"
#include "Play/PlayerFlashlight.h"
#include "Math/Vector4f.h"
#include "Ui/NpcInfoOverlay.h"
#include "Weapons/HittableComponent.h"
#include "Weapons/WeaponLoadout.h"
#include "Combat/CombatSystem.h"
#include "Combat/JumpAttackComponent.h"
#include "Combat/StatusDef.h"
#include "Combat/StatusEffectComponent.h"
#include "Core/Log.h"
#include "Debug/DebugTypes.h"
#include "Network/NetTypes.h"
#include "Math/MathDefines.h"
#include "Render/DebugRenderState.h"
#include "Render/GpuResourceCache.h"
#include "Render/Profile.h"
#include "Render/ScenePath.h"
#include "Sky/Environment.h"

#include "Ui/Icons.h"

#include <imgui.h>

#include <cstdio>
#include <string>

using namespace Dark;
using namespace Math;

namespace
{

const char* roleLabel(NetRole role)
{
    switch (role)
    {
    case NetRole::Idle:
        return "Idle";
    case NetRole::Joining:
        return "Joining";
    case NetRole::Host:
        return "Host";
    case NetRole::Client:
        return "Client";
    default:
        return "?";
    }
}

const char* jumpAttackPhaseName(Combat::JumpAttackPhase phase)
{
    switch (phase)
    {
    case Combat::JumpAttackPhase::Idle:
        return "Idle";
    case Combat::JumpAttackPhase::Telegraph:
        return "Telegraph";
    case Combat::JumpAttackPhase::Leap:
        return "Leap";
    case Combat::JumpAttackPhase::Connected:
        return "Connected";
    case Combat::JumpAttackPhase::Pound:
        return "Pound";
    case Combat::JumpAttackPhase::Recover:
        return "Recover";
    default:
        return "?";
    }
}

void formatIPv4(char* out, size_t cap, const Address& addr)
{
    const uint32_t ip = addr.ipv4;
    std::snprintf(out, cap, "%u.%u.%u.%u:%u", (ip >> 24) & 255u, (ip >> 16) & 255u, (ip >> 8) & 255u, ip & 255u, addr.port);
}

void applyDebugStatus(World& world, Entity body, Combat::StatusId id)
{
    if (!body.valid())
        return;
    Combat::StatusEffectComponent* st = world.get<Combat::StatusEffectComponent>(body);
    if (!st)
        return;
    // Shock hitch lives in CombatSystem::resolve (Applied snapshot). Empty source so the slot is not self.
    if (id == Combat::StatusId::Shock)
    {
        Combat::DamageEvent ev{};
        ev.target   = body;
        ev.amount   = 0.0f;
        ev.flags    = Combat::DamageFlags::None;
        ev.statusId = static_cast<uint8_t>(id);
        ev.hitDir   = Vector3f{ 0.0f, 0.0f, 1.0f };
        Combat::CombatSystem combat;
        combat.resolve(world, ev);
        return;
    }
    st->applyStatus(id, 0.0f, 0.0f, Entity{});
}

} // namespace


SandboxApp& SandboxHud::app() const
{
    return *m_app;
}

void SandboxHud::drawInventory()
{
    const Entity        body = app().possessedBody();
    InventoryComponent* inv  = body.valid() ? app().world().get<InventoryComponent>(body) : nullptr;
    if (!inv)
        return;
    ImGui::SetNextWindowSize(ImVec2(380.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Inventory", &app().m_showInventory))
    {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Equipped");
    for (int s = 1; s < static_cast<int>(EquipSlot::Count); ++s)
    {
        const EquipSlot  slot = static_cast<EquipSlot>(s);
        const ItemStack& eq   = inv->equipped[s];
        ImGui::PushID(s);
        if (slot == EquipSlot::Light)
        {
            // L toggles the beam. The slot staying filled does not tell the panel that.
            const bool on = !eq.empty() && app().m_play.runtime().offhand.lightOn;
            ImGui::Text("%s: %s", equipSlotName(slot), on ? "equipped" : "unequipped");
            if (!eq.empty())
            {
                ImGui::SameLine();
                ImGui::TextUnformatted(eq.def->name);
                ImGui::SameLine();
                if (ImGui::SmallButton("To bag"))
                    unequipItem(*inv, slot);
            }
        }
        else
        {
            ImGui::Text("%s: %s", equipSlotName(slot), eq.empty() ? "-" : eq.def->name);
            if (!eq.empty())
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("Unequip"))
                    unequipItem(*inv, slot);
            }
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Bag");
    for (int i = 0; i < InventoryComponent::kBagSize; ++i)
    {
        const ItemStack item = inv->bag[i];
        if (item.empty())
            continue;
        ImGui::PushID(100 + i);
        if (item.def->slot == EquipSlot::Light)
            ImGui::Text("%s x%d (%s) — unequipped", item.def->name, item.count, itemTypeName(item.def->type));
        else
            ImGui::Text("%s x%d (%s)", item.def->name, item.count, itemTypeName(item.def->type));
        if (item.def->type == ItemType::Consumable)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Use"))
                useItem(app().world(), body, *inv, i);
        }
        else if (item.def->type == ItemType::Armor || item.def->type == ItemType::Tool)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Equip"))
                equipItem(*inv, i);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Drop"))
            app().dropInventoryItem(i);
        ImGui::PopID();
    }
    ImGui::End();
}

void SandboxHud::drawQuestHud()
{
    if (!app().m_play.quest().active())
        return;
    const ImGuiViewport* vp   = ImGui::GetMainViewport();
    ImDrawList*          dl   = ImGui::GetForegroundDrawList(const_cast<ImGuiViewport*>(vp));
    ImFont*              font = ImGui::GetFont();
    const float          fs   = ImGui::GetFontSize();
    const QuestDef&      def  = app().m_play.quest().def();

    const float width = 330.0f;
    const float x     = vp->Pos.x + vp->Size.x - 28.0f - width;
    float       y     = vp->Pos.y + 70.0f;
    struct Line
    {
        std::string text;
        ImU32       color;
        float       size;
    };
    std::vector<Line> lines;
    lines.push_back(Line{ def.title, IM_COL32(240, 205, 110, 255), fs * 1.2f });
    if (app().m_play.quest().complete())
        lines.push_back(Line{ "Complete. Press J for rewards.", IM_COL32(120, 230, 140, 255), fs });
    for (size_t i = 0; i < def.objectives.size(); ++i)
    {
        const ObjectiveDef&   o  = def.objectives[i];
        const ObjectiveState& st = app().m_play.quest().state(i);
        if (!st.done && !app().m_play.quest().open(i))
            continue;
        std::string text = std::string(st.done ? "[x] " : "[ ] ") + o.text;
        if (o.count > 1)
            text += std::format(" ({}/{})", st.progress, o.count);
        if (o.optional)
            text += "  optional";
        const ImU32 color = st.done ? IM_COL32(120, 200, 135, 200) : (o.optional ? IM_COL32(190, 190, 190, 235) : IM_COL32(245, 245, 245, 255));
        lines.push_back(Line{ text, color, fs });
    }
    float height = 14.0f;
    for (const Line& l : lines)
        height += l.size + 6.0f;
    dl->AddRectFilled(ImVec2(x - 12.0f, y - 10.0f), ImVec2(x + width, y + height), IM_COL32(10, 14, 12, 150), 4.0f);
    dl->AddRectFilled(ImVec2(x - 12.0f, y - 10.0f), ImVec2(x - 9.0f, y + height), IM_COL32(240, 205, 110, 220));
    for (const Line& l : lines)
    {
        dl->AddText(font, l.size, ImVec2(x + 1.0f, y + 1.0f), IM_COL32(0, 0, 0, 180), l.text.c_str(), nullptr, width - 8.0f);
        dl->AddText(font, l.size, ImVec2(x, y), l.color, l.text.c_str(), nullptr, width - 8.0f);
        y += l.size + 6.0f;
    }

    if (!m_questBanners.empty())
    {
        const QuestBanner& b     = m_questBanners.front();
        const float        alpha = Math::Clamp(Math::Min(b.timeLeft, 3.5f - b.timeLeft) * 3.0f, 0.0f, 1.0f);
        const float        midX  = vp->Pos.x + vp->Size.x * 0.5f;
        const float        top   = vp->Pos.y + vp->Size.y * 0.16f;
        const float        labelSize = fs * 1.1f;
        const float        textSize   = fs * 2.0f;
        const ImVec2       ls    = font->CalcTextSizeA(labelSize, FLT_MAX, 0.0f, b.label.c_str());
        const ImVec2       ts    = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, b.text.c_str());
        const int          a     = static_cast<int>(alpha * 255.0f);
        const ImU32        col   = (b.color & 0x00FFFFFFu) | (static_cast<ImU32>(a) << 24);
        dl->AddRectFilled(ImVec2(midX - Math::Max(ls.x, ts.x) * 0.5f - 24.0f, top - 10.0f), ImVec2(midX + Math::Max(ls.x, ts.x) * 0.5f + 24.0f, top + ls.y + ts.y + 16.0f),
                          IM_COL32(10, 14, 12, static_cast<int>(alpha * 160.0f)), 4.0f);
        dl->AddText(font, labelSize, ImVec2(midX - ls.x * 0.5f, top), col, b.label.c_str());
        dl->AddText(font, textSize, ImVec2(midX - ts.x * 0.5f + 2.0f, top + ls.y + 6.0f), IM_COL32(0, 0, 0, a * 3 / 4), b.text.c_str());
        dl->AddText(font, textSize, ImVec2(midX - ts.x * 0.5f, top + ls.y + 4.0f), IM_COL32(255, 255, 255, a), b.text.c_str());
    }

    Vector3f target;
    const Entity              body = app().possessedBody();
    const TransformComponent* pxf  = body.valid() ? app().world().get<TransformComponent>(body) : nullptr;
    if (!pxf || app().m_play.quest().complete() || !app().questTarget(target))
        return;
    const Vector4f clip = app().m_viewCamera.GetViewProj() * Vector4f(Vector3f(target.x, target.y + 1.4f, target.z), 1.0f);
    float          nx   = clip.w > 1.0e-4f ? clip.x / clip.w : -clip.x;
    float          ny   = clip.w > 1.0e-4f ? clip.y / clip.w : -clip.y;
    if (clip.w <= 1.0e-4f)
    {
        const float m = Math::Max(std::fabs(nx), std::fabs(ny));
        nx = m > 1.0e-6f ? nx / m : 0.0f;
        ny = m > 1.0e-6f ? ny / m : -1.0f;
    }
    const float edge = Math::Max(std::fabs(nx), std::fabs(ny));
    if (edge > 0.9f)
    {
        nx *= 0.9f / edge;
        ny *= 0.9f / edge;
    }
    const ImVec2 c{ vp->Pos.x + (nx * 0.5f + 0.5f) * vp->Size.x, vp->Pos.y + (1.0f - (ny * 0.5f + 0.5f)) * vp->Size.y };
    const float  r = 9.0f;
    dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), IM_COL32(240, 205, 110, 230));
    dl->AddQuad(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), IM_COL32(0, 0, 0, 200), 1.5f);
    char         dist[24];
    std::snprintf(dist, sizeof(dist), "%.0f m", static_cast<double>((target - pxf->position).Magnitude()));
    const ImVec2 ds = ImGui::CalcTextSize(dist);
    dl->AddText(ImVec2(c.x - ds.x * 0.5f + 1.0f, c.y + r + 3.0f), IM_COL32(0, 0, 0, 200), dist);
    dl->AddText(ImVec2(c.x - ds.x * 0.5f, c.y + r + 2.0f), IM_COL32(240, 205, 110, 255), dist);
}

void SandboxHud::drawAlarm()
{
    const float alarmTime = app().m_play.facility().alarmTime;
    if (alarmTime <= 0.0f)
        return;
    const ImGuiViewport* vp    = ImGui::GetMainViewport();
    ImDrawList*          dl    = ImGui::GetForegroundDrawList(const_cast<ImGuiViewport*>(vp));
    ImFont*              font  = ImGui::GetFont();
    const float          size  = ImGui::GetFontSize() * 2.2f;
    const char*          text  = "ALARM";
    const ImVec2         ts    = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    const int            pulse = static_cast<int>(160.0f + 95.0f * std::sin(alarmTime * 9.0f));
    const ImVec2         at{ vp->Pos.x + (vp->Size.x - ts.x) * 0.5f, vp->Pos.y + 24.0f };
    dl->AddText(font, size, ImVec2(at.x + 2.0f, at.y + 2.0f), IM_COL32(0, 0, 0, 200), text);
    dl->AddText(font, size, at, IM_COL32(255, 60, 50, pulse), text);
}

void SandboxHud::drawUpgradeHint()
{
    const Entity              body = app().possessedBody();
    const InventoryComponent* inv  = body.valid() ? app().world().get<InventoryComponent>(body) : nullptr;
    if (!inv || m_showUpgrades)
        return;
    bool ready = false;
    const FacilityState& facility = app().m_play.facility();
    for (const UpgradeDef& up : facility.defs)
        ready = ready || (std::find(facility.owned.begin(), facility.owned.end(), up.id) == facility.owned.end() && canAfford(*inv, up));
    if (!ready)
        return;
    const char*          text = "Upgrade ready  [K]";
    const ImGuiViewport* vp   = ImGui::GetMainViewport();
    ImDrawList*          dl   = ImGui::GetForegroundDrawList(const_cast<ImGuiViewport*>(vp));
    const ImVec2         ts   = ImGui::CalcTextSize(text);
    const ImVec2         at{ vp->Pos.x + vp->Size.x - 28.0f - ts.x, vp->Pos.y + vp->Size.y - 130.0f };
    dl->AddText(ImVec2(at.x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, 200), text);
    dl->AddText(at, IM_COL32(150, 240, 170, 255), text);
}

void SandboxHud::drawUpgrades()
{
    ImGui::SetNextWindowSize(ImVec2(480.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Upgrades", &m_showUpgrades))
    {
        ImGui::End();
        return;
    }
    const Entity        body = app().possessedBody();
    InventoryComponent* inv  = body.valid() ? app().world().get<InventoryComponent>(body) : nullptr;
    if (!inv)
    {
        ImGui::TextDisabled("No body to modify.");
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("Spend stolen research and materials to modify yourself. Upgrades last for this session.");
    ImGui::Text("You have: %d Research Data, %d Bio Material", heldCount(*inv, "research"), heldCount(*inv, "bio_material"));
    ImGui::Separator();
    FacilityState& facility = app().m_play.facility();
    for (const UpgradeDef& up : facility.defs)
    {
        ImGui::PushID(up.id.c_str());
        const bool owned = std::find(facility.owned.begin(), facility.owned.end(), up.id) != facility.owned.end();
        ImGui::TextColored(owned ? ImVec4(0.47f, 0.90f, 0.55f, 1.0f) : ImVec4(0.94f, 0.80f, 0.43f, 1.0f), "%s%s", up.name.c_str(), owned ? "  (owned)" : "");
        ImGui::TextWrapped("%s", up.description.c_str());
        std::string cost;
        for (const UpgradeCost& c : up.cost)
        {
            const ItemDef* def = findItemDef(c.item);
            if (!cost.empty())
                cost += ", ";
            cost += std::format("{} {} ({} held)", c.count, def ? def->name : c.item.c_str(), heldCount(*inv, c.item));
        }
        ImGui::TextDisabled("Cost: %s", cost.c_str());
        if (!owned)
        {
            const bool afford = canAfford(*inv, up);
            ImGui::BeginDisabled(!afford);
            if (ImGui::Button("Modify") && payFor(*inv, up))
            {
                facility.owned.push_back(up.id);
                app().m_play.refreshUpgrades();
                playSoundCue(app().world(), app().audio(), app().assets(), app().m_camera, "click");
                DE_LOG_INFO("Upgrades: bought {}", up.name);
            }
            ImGui::EndDisabled();
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::End();
}

void SandboxHud::drawJournal()
{
    ImGui::SetNextWindowSize(ImVec2(440.0f, 360.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Journal", &m_showJournal))
    {
        ImGui::End();
        return;
    }
    if (!app().m_play.quest().active())
    {
        ImGui::TextDisabled("No quest in this level.");
        ImGui::End();
        return;
    }
    const QuestDef& def = app().m_play.quest().def();
    ImGui::TextColored(ImVec4(0.94f, 0.80f, 0.43f, 1.0f), "%s", def.title.c_str());
    if (app().m_play.quest().complete())
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.47f, 0.90f, 0.55f, 1.0f), "(complete)");
    }
    if (!def.description.empty())
        ImGui::TextWrapped("%s", def.description.c_str());
    ImGui::Separator();
    ImGui::TextUnformatted("Objectives");
    for (size_t i = 0; i < def.objectives.size(); ++i)
    {
        const ObjectiveDef&   o  = def.objectives[i];
        const ObjectiveState& st = app().m_play.quest().state(i);
        const bool            open = app().m_play.quest().open(i);
        if (!st.done && !open)
        {
            ImGui::TextDisabled("  ???");
            continue;
        }
        const std::string progress = o.count > 1 ? std::format(" ({}/{})", st.progress, o.count) : std::string();
        const ImVec4      color    = st.done ? ImVec4(0.47f, 0.80f, 0.53f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        ImGui::TextColored(color, "%s %s%s%s", st.done ? "[x]" : "[ ]", o.text.c_str(), progress.c_str(), o.optional ? "  (optional)" : "");
    }
    if (!def.rewards.empty())
    {
        ImGui::Separator();
        ImGui::TextUnformatted(app().m_play.quest().complete() ? "Rewards (received)" : "Rewards");
        for (const QuestReward& r : def.rewards)
        {
            const ItemDef* item = findItemDef(r.item);
            ImGui::BulletText("%s x%d", item ? item->name : r.item.c_str(), r.count);
        }
    }
    ImGui::End();
}

void SandboxHud::drawGuardSpeech()
{
    const std::vector<GuardSubtitle>& subs = app().m_guardVoice.subtitles();
    if (subs.empty())
        return;
    const ImGuiViewport*      vp   = ImGui::GetMainViewport();
    ImDrawList*               dl   = ImGui::GetBackgroundDrawList(const_cast<ImGuiViewport*>(vp));
    ImFont*                   font = ImGui::GetFont();
    const float               fs   = ImGui::GetFontSize();
    const Entity              body = app().possessedBody();
    const TransformComponent* pxf  = body.valid() ? app().world().get<TransformComponent>(body) : nullptr;
    const float               vw   = static_cast<float>(app().renderer().width());
    const float               vh   = static_cast<float>(app().renderer().height());
    const GuardSubtitle*      closest = nullptr;
    float                     closestD = 1.0e12f;
    for (const GuardSubtitle& sub : subs)
    {
        const TransformComponent* gxf = app().world().alive(sub.speaker) ? app().world().get<TransformComponent>(sub.speaker) : nullptr;
        if (!gxf)
            continue;
        const float d = pxf ? (gxf->position - pxf->position).MagnitudeSqrd() : 0.0f;
        if (d > 45.0f * 45.0f)
            continue;
        if (d < closestD)
        {
            closestD = d;
            closest  = &sub;
        }
        const Vector4f clip = app().m_viewCamera.GetViewProj() * Vector4f(Vector3f(gxf->position.x, gxf->position.y + 2.1f, gxf->position.z), 1.0f);
        if (clip.w <= 1.0e-4f || clip.z / clip.w < 0.0f || clip.z / clip.w > 1.0f)
            continue;
        const int    a  = static_cast<int>(Math::Clamp(sub.timeLeft * 3.0f, 0.0f, 1.0f) * 255.0f);
        const ImVec2 ts = font->CalcTextSizeA(fs, 260.0f, 260.0f, sub.text.c_str());
        const ImVec2 at{ vp->Pos.x + (clip.x / clip.w * 0.5f + 0.5f) * vw - ts.x * 0.5f, vp->Pos.y + (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * vh - ts.y };
        dl->AddRectFilled(ImVec2(at.x - 6.0f, at.y - 3.0f), ImVec2(at.x + ts.x + 6.0f, at.y + ts.y + 3.0f), IM_COL32(10, 14, 12, a * 150 / 255), 3.0f);
        dl->AddText(font, fs, at, IM_COL32(255, 255, 255, a), sub.text.c_str(), nullptr, 260.0f);
    }
    if (closest)
    {
        const std::string line = "Guard: " + closest->text;
        const float       size = fs * 1.15f;
        const ImVec2      ts   = font->CalcTextSizeA(size, FLT_MAX, 0.0f, line.c_str());
        const ImVec2      at{ vp->Pos.x + (vp->Size.x - ts.x) * 0.5f, vp->Pos.y + vp->Size.y - 150.0f };
        dl->AddText(font, size, ImVec2(at.x + 1.5f, at.y + 1.5f), IM_COL32(0, 0, 0, 220), line.c_str());
        dl->AddText(font, size, at, IM_COL32(235, 235, 210, 255), line.c_str());
    }
}

void SandboxHud::drawDialogue()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    if (!vp)
        return;
    DialogueHudView view;
    view.viewProj      = app().m_viewCamera.GetViewProj();
    view.list          = ImGui::GetBackgroundDrawList(const_cast<ImGuiViewport*>(vp));
    view.projectWidth  = static_cast<float>(app().renderer().width());
    view.projectHeight = static_cast<float>(app().renderer().height());
    view.windowName    = "Dialogue";
    const std::string talkPrompt = app().input().actions().keyBracket("talk", "[T]") + " Talk";
    view.talkPrompt    = talkPrompt.c_str();
    drawDialogueHud(app().m_dialogue, view);
    drawMovieHud(app().m_movie);
}

void SandboxHud::drawStaminaBar()
{
    const PlayerMotor* motor = app().localMotor();
    if (!motor || motor->stamina() >= 0.999f)
        return;
    const ImGuiViewport* vp  = ImGui::GetMainViewport();
    ImDrawList*          dl  = ImGui::GetForegroundDrawList(const_cast<ImGuiViewport*>(vp));
    const float          w   = 220.0f;
    const ImVec2         min{ vp->Pos.x + vp->Size.x - 28.0f - w, vp->Pos.y + vp->Size.y - 92.0f };
    const ImVec2         max{ min.x + w, min.y + 8.0f };
    const float          s   = Math::Clamp(motor->stamina(), 0.0f, 1.0f);
    dl->AddRectFilled(min, max, IM_COL32(0, 0, 0, 150), 3.0f);
    dl->AddRectFilled(min, ImVec2(min.x + w * s, max.y), s < 0.25f ? IM_COL32(230, 90, 60, 230) : IM_COL32(240, 210, 90, 230), 3.0f);
}

void SandboxHud::drawBreathBar()
{
    const PlayerMotor* motor = app().localMotor();
    if (!motor || motor->breath() >= 0.999f)
        return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList*          dl = ImGui::GetForegroundDrawList(const_cast<ImGuiViewport*>(vp));
    const float          w  = 220.0f;
    const ImVec2         min{ vp->Pos.x + vp->Size.x - 28.0f - w, vp->Pos.y + vp->Size.y - 108.0f };
    const ImVec2         max{ min.x + w, min.y + 8.0f };
    const float          b  = Math::Clamp(motor->breath(), 0.0f, 1.0f);
    dl->AddRectFilled(min, max, IM_COL32(0, 0, 0, 150), 3.0f);
    dl->AddRectFilled(min, ImVec2(min.x + w * b, max.y), b < 0.25f ? IM_COL32(230, 90, 60, 230) : IM_COL32(90, 180, 255, 230), 3.0f);
}

void SandboxHud::drawArmorHud()
{
    const Entity              body = app().possessedBody();
    const InventoryComponent* inv  = body.valid() ? app().world().get<InventoryComponent>(body) : nullptr;
    if (!inv)
        return;

    const Combat::ArmorPiecesComponent* pieces = app().world().get<Combat::ArmorPiecesComponent>(body);
    if (!pieces || pieces->count == 0)
        return;
    struct Zone
    {
        const char*     name;
        Combat::HitPart a;
        Combat::HitPart b;
    };
    const Zone  zones[] = { { "Head", Combat::HitPart::Head, Combat::HitPart::Head },
                            { "Chest", Combat::HitPart::Chest, Combat::HitPart::Chest },
                            { "Arms", Combat::HitPart::RightArm, Combat::HitPart::LeftArm },
                            { "Legs", Combat::HitPart::RightLeg, Combat::HitPart::LeftLeg } };
    std::string text;
    for (const Zone& z : zones)
    {
        int   worn = 0, intact = 0;
        float absorb = 0.0f;
        for (int i = 0; i < pieces->count; ++i)
        {
            const Combat::ArmorPiece& p = pieces->pieces[i];
            if (p.part != z.a && p.part != z.b)
                continue;
            ++worn;
            intact += p.attached ? 1 : 0;
            absorb = p.absorb;
        }
        if (worn == 0)
            continue;
        if (!text.empty())
            text += "   ";
        if (intact == 0)
            text += std::format("{} broken", z.name);
        else if (intact < worn)
            text += std::format("{} -{:.0f}% (one broken)", z.name, absorb * 100.0f);
        else
            text += std::format("{} -{:.0f}%", z.name, absorb * 100.0f);
    }
    if (text.empty())
        return;

    const ImGuiViewport* vp   = ImGui::GetMainViewport();
    const ImVec2         size = ImGui::CalcTextSize(text.c_str());
    const ImVec2         pos{ vp->Pos.x + vp->Size.x - 28.0f - size.x, vp->Pos.y + vp->Size.y - 56.0f - size.y };
    ImDrawList*          dl = ImGui::GetForegroundDrawList(const_cast<ImGuiViewport*>(vp));
    dl->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 0, 0, 200), text.c_str());
    dl->AddText(pos, IM_COL32(150, 200, 255, 255), text.c_str());
}

void SandboxHud::drawPickupLabels()
{
    ImDrawList* dl = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
    const float vw = static_cast<float>(app().renderer().width());
    const float vh = static_cast<float>(app().renderer().height());
    if (!dl || vw < 1.0f || vh < 1.0f)
        return;
    const ImVec2              vpPos   = ImGui::GetMainViewport()->Pos;
    const Entity              body    = app().possessedBody();
    const TransformComponent* pxf     = body.valid() ? app().world().get<TransformComponent>(body) : nullptr;
    const PlayInteractPick    pick    = app().m_play.pickInteract();
    const Entity              nearest = pick.kind == PlayInteract::Item ? pick.e : Entity{};
    const std::string         useKey  = app().input().actions().keyBracket("pickup", "[G]");
    const std::string         takeKey = app().input().actions().keyBracket("takedown", "[E]");
    const std::string         usePrefix = useKey + " ";

    auto label = [&](Entity e, const char* name) {
        const TransformComponent* xf = app().world().get<TransformComponent>(e);
        if (!xf)
            return;
        if (pxf && (xf->position - pxf->position).MagnitudeSqrd() > 15.0f * 15.0f)
            return;

        const Vector4f clip = app().m_viewCamera.GetViewProj() * Vector4f(Vector3f(xf->position.x, xf->position.y + 0.5f, xf->position.z), 1.0f);
        if (clip.w <= 1.0e-4f)
            return;
        const float invW = 1.0f / clip.w;
        const float ndcZ = clip.z * invW;
        if (ndcZ < 0.0f || ndcZ > 1.0f)
            return;
        const float sx = (clip.x * invW * 0.5f + 0.5f) * vw;
        const float sy = (1.0f - (clip.y * invW * 0.5f + 0.5f)) * vh;

        const bool canTake = nearest.valid() && e.id() == nearest.id();
        char       text[112];
        std::snprintf(text, sizeof(text), "%s%s", canTake ? usePrefix.c_str() : "", name);

        const ImVec2 ts = ImGui::CalcTextSize(text);
        const ImVec2 pos{ vpPos.x + sx - ts.x * 0.5f, vpPos.y + sy - ts.y };
        dl->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 0, 0, 200), text);
        dl->AddText(pos, canTake ? IM_COL32(255, 220, 90, 255) : IM_COL32(235, 235, 235, 255), text);
    };

    app().world().each<PowerBoxComponent>([&](Entity e, PowerBoxComponent& box) {
        if (!pxf)
            return;
        const TransformComponent* bxf = app().world().get<TransformComponent>(e);
        const Vector3f            at  = bxf ? bxf->position : box.pos;
        const Vector3f            d{ at.x - pxf->position.x, 0.0f, at.z - pxf->position.z };
        if (d.MagnitudeSqrd() > 15.0f * 15.0f)
            return;
        const Vector4f clip = app().m_viewCamera.GetViewProj() * Vector4f(Vector3f(at.x, at.y + 1.0f, at.z), 1.0f);
        if (clip.w <= 1.0e-4f || clip.z / clip.w < 0.0f || clip.z / clip.w > 1.0f)
            return;
        const bool        close = pick.kind == PlayInteract::Power && pick.e == e;
        const std::string power = close ? (useKey + (box.on ? " Cut power" : " Restore power")) : std::string(box.on ? "Power box" : "Power box (off)");
        const char*       text  = power.c_str();
        const ImVec2 ts    = ImGui::CalcTextSize(text);
        const ImVec2 pos{ vpPos.x + (clip.x / clip.w * 0.5f + 0.5f) * vw - ts.x * 0.5f, vpPos.y + (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * vh - ts.y };
        dl->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 0, 0, 200), text);
        dl->AddText(pos, close ? IM_COL32(255, 220, 90, 255) : (box.on ? IM_COL32(150, 230, 150, 255) : IM_COL32(230, 120, 100, 255)), text);
    });

    auto prompt = [&](const Vector3f& at, const char* text, ImU32 color) {
        const Vector4f clip = app().m_viewCamera.GetViewProj() * Vector4f(at, 1.0f);
        if (clip.w <= 1.0e-4f || clip.z / clip.w < 0.0f || clip.z / clip.w > 1.0f)
            return;
        const ImVec2 ts = ImGui::CalcTextSize(text);
        const ImVec2 pos{ vpPos.x + (clip.x / clip.w * 0.5f + 0.5f) * vw - ts.x * 0.5f, vpPos.y + (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * vh - ts.y };
        dl->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 0, 0, 200), text);
        dl->AddText(pos, color, text);
    };
    if (pxf && carriedBody(app().world()).valid())
    {
        const std::string drop = useKey + " Drop body";
        prompt(pxf->position + Vector3f{ 0.0f, 1.4f, 0.0f }, drop.c_str(), IM_COL32(255, 220, 90, 255));
    }
    else if (const Entity target = app().m_play.takedownTarget(); target.valid())
    {
        if (const TransformComponent* gxf = app().world().get<TransformComponent>(target))
        {
            const std::string take = takeKey + " Takedown";
            prompt(gxf->position + Vector3f{ 0.0f, 1.2f, 0.0f }, take.c_str(), IM_COL32(255, 110, 90, 255));
        }
    }
    else if (pick.kind == PlayInteract::Body)
    {
        if (const TransformComponent* gxf = app().world().get<TransformComponent>(pick.e))
        {
            const std::string lift = useKey + " Pick up body";
            prompt(gxf->position + Vector3f{ 0.0f, 0.6f, 0.0f }, lift.c_str(), IM_COL32(255, 220, 90, 255));
        }
    }
    else if (pick.kind == PlayInteract::Hide)
    {
        const TransformComponent* hxf = app().world().get<TransformComponent>(pick.e);
        const HideSpotComponent*  hs  = app().world().get<HideSpotComponent>(pick.e);
        if (hxf && hs)
        {
            const std::string text = useKey + " Hide in " + hs->label;
            prompt(hxf->position + Vector3f{ 0.0f, 2.4f, 0.0f }, text.c_str(), IM_COL32(255, 220, 90, 255));
        }
    }
    else if (pick.kind == PlayInteract::Door)
    {
        const DoorComponent* door = app().world().get<DoorComponent>(pick.e);
        if (door)
        {
            const std::string text = useKey + (door->wantOpen ? " Close door" : (door->lock.empty() ? " Open door" : " Locked door"));
            prompt(door->closedPos + Vector3f{ 0.0f, 1.0f, 0.0f }, text.c_str(), IM_COL32(255, 220, 90, 255));
        }
    }
    if (const PlayRuntime& play = app().m_play.runtime(); play.climbOffer)
    {
        const std::string climb = useKey + (play.climbDown ? " Climb down" : " Climb");
        prompt(play.climbAt, climb.c_str(), IM_COL32(255, 220, 90, 255));
    }
    const FacilityState& facility = app().m_play.facility();
    if (facility.doorMsgTime > 0.0f && !facility.doorMsg.empty())
    {
        const ImVec2 ts = ImGui::CalcTextSize(facility.doorMsg.c_str());
        const ImVec2 at{ vpPos.x + (vw - ts.x) * 0.5f, vpPos.y + vh - 220.0f };
        dl->AddText(ImVec2(at.x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, 220), facility.doorMsg.c_str());
        dl->AddText(at, IM_COL32(255, 200, 120, 255), facility.doorMsg.c_str());
    }
    if (facility.hiddenIn.valid())
    {
        const HideSpotComponent* hs   = app().world().get<HideSpotComponent>(facility.hiddenIn);
        const std::string        text = "Hidden in " + (hs ? hs->label : std::string("hiding spot")) + "   " + useKey + " Step out";
        const ImVec2             ts   = ImGui::CalcTextSize(text.c_str());
        const ImVec2             at{ vpPos.x + (vw - ts.x) * 0.5f, vpPos.y + vh - 190.0f };
        dl->AddText(ImVec2(at.x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, 220), text.c_str());
        dl->AddText(at, IM_COL32(150, 220, 255, 255), text.c_str());
    }

    char name[96];
    app().world().each<ItemPickupComponent>([&](Entity e, ItemPickupComponent& pickup) {
        if (pickup.item.empty())
            return;
        if (pickup.item.count > 1)
            std::snprintf(name, sizeof(name), "%s x%d", pickup.item.def->name, pickup.item.count);
        else
            std::snprintf(name, sizeof(name), "%s", pickup.item.def->name);
        label(e, name);
    });
    app().world().each<LootComponent>([&](Entity e, LootComponent& loot) {
        const int n = itemCount(loot.contents);
        if (n == 0)
            return;
        std::snprintf(name, sizeof(name), "%s (%d)", loot.label, n);
        label(e, name);
    });
}

void SandboxHud::drawGuardAwareness()
{
    ImDrawList* dl = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
    const float vw = static_cast<float>(app().renderer().width());
    const float vh = static_cast<float>(app().renderer().height());
    if (!dl || vw < 1.0f || vh < 1.0f)
        return;
    const ImVec2              vp   = ImGui::GetMainViewport()->Pos;
    const Entity              body = app().possessedBody();
    const TransformComponent* pxf  = body.valid() ? app().world().get<TransformComponent>(body) : nullptr;
    ImFont*                   font = ImGui::GetFont();
    const ImVec2              mid{ vp.x + vw * 0.5f, vp.y + vh * 0.5f };

    auto project = [&](const Vector3f& p, ImVec2& out) {
        const Vector4f clip = app().m_viewCamera.GetViewProj() * Vector4f(p, 1.0f);
        if (clip.w <= 1.0e-4f)
            return false;
        const float invW = 1.0f / clip.w;
        if (clip.z * invW < 0.0f || clip.z * invW > 1.0f)
            return false;
        out = ImVec2(vp.x + (clip.x * invW * 0.5f + 0.5f) * vw, vp.y + (1.0f - (clip.y * invW * 0.5f + 0.5f)) * vh);
        return true;
    };
    auto stateColor = [](AI::Alertness a, int alpha) {
        if (a == AI::Alertness::Alert)
            return IM_COL32(235, 50, 40, alpha);
        if (a == AI::Alertness::Calm)
            return IM_COL32(245, 245, 245, alpha);
        return IM_COL32(255, 196, 40, alpha);
    };

    struct Marker
    {
        Vector3f at;
        bool     sound;
        bool     hunting;
    };
    std::vector<Marker> markers;
    bool                anyoneSees = false;

    app().world().each<AiAgentComponent>([&](Entity e, AiAgentComponent& ai) {
        const HealthComponent*    hp = app().world().get<HealthComponent>(e);
        const TransformComponent* xf = app().world().get<TransformComponent>(e);
        const AI::AwarenessState& aw = ai.aware;
        if (!xf || !hp || !hp->health.alive() || !isInteractGuard(app().world(), e))
            return;
        if (aw.alertness == AI::Alertness::Calm && aw.meter < 0.02f)
        {
            ImVec2 c;
            if (aw.wary && pxf && (xf->position - pxf->position).MagnitudeSqrd() <= 45.0f * 45.0f
                && project(Vector3f(xf->position.x, xf->position.y + 1.3f, xf->position.z), c))
                dl->AddCircle(c, 9.0f, IM_COL32(230, 180, 40, 150), 20, 2.5f);
            return;
        }
        const bool alert   = aw.alertness == AI::Alertness::Alert;
        const bool curious = !alert && aw.alertness != AI::Alertness::Calm;
        anyoneSees = anyoneSees || aw.seesNow;
        if ((curious || alert) && !aw.seesNow)
        {
            const bool hunt = alert || aw.alertness == AI::Alertness::Searching;
            bool       seen = false;
            for (Marker& m : markers)
            {
                if ((m.at - aw.noticePos).MagnitudeSqrd() < 4.0f)
                {
                    m.hunting = m.hunting || hunt;
                    m.sound   = m.sound && aw.heardOnly;
                    seen      = true;
                }
            }
            if (!seen)
                markers.push_back(Marker{ aw.noticePos, aw.heardOnly, hunt });
        }

        if (pxf)
        {
            const float rel   = Math::WrapPi(std::atan2(xf->position.x - pxf->position.x, xf->position.z - pxf->position.z) - app().m_play.runtime().lookYaw);
            const float theta = rel - 1.5707963f;
            const float fill  = alert ? 1.0f : Math::Clamp(aw.meter, 0.0f, 1.0f);
            const int   alpha = static_cast<int>(110.0f + 145.0f * fill);
            const float half  = 0.08f + 0.22f * fill;
            dl->PathArcTo(mid, 95.0f, theta - half, theta + half, 12);
            dl->PathStroke(stateColor(aw.alertness, alpha), 0, 6.0f);
        }

        if (pxf && (xf->position - pxf->position).MagnitudeSqrd() > 45.0f * 45.0f)
            return;
        ImVec2 c;
        if (!project(Vector3f(xf->position.x, xf->position.y + 1.3f, xf->position.z), c))
            return;
        const float r = 13.0f;
        if (alert)
        {
            dl->AddCircleFilled(c, r + 2.0f, IM_COL32(0, 0, 0, 150), 24);
            dl->AddCircleFilled(c, r, stateColor(aw.alertness, 255), 24);
        }
        else
        {
            const float fill = Math::Clamp(aw.meter, 0.0f, 1.0f);
            dl->AddCircle(c, r, IM_COL32(0, 0, 0, 170), 24, 6.0f);
            dl->AddCircle(c, r, (curious || aw.wary) ? IM_COL32(120, 90, 20, 200) : IM_COL32(90, 90, 90, 160), 24, 4.0f);
            if (fill > 0.01f)
            {
                dl->PathArcTo(c, r, -1.5707963f, -1.5707963f + fill * 6.2831853f, 24);
                dl->PathStroke(stateColor(aw.alertness, 255), 0, 4.0f);
            }
        }
        if (alert || curious)
        {
            const char*  icon = alert ? "!" : "?";
            const float  size = ImGui::GetFontSize() * 1.3f;
            const ImVec2 ts   = font->CalcTextSizeA(size, FLT_MAX, 0.0f, icon);
            dl->AddText(font, size, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), alert ? IM_COL32(255, 255, 255, 255) : stateColor(aw.alertness, 255), icon);
        }
    });

    for (const Marker& m : markers)
    {
        if (anyoneSees)
            break;
        if (pxf && (Vector3f{ m.at.x - pxf->position.x, 0.0f, m.at.z - pxf->position.z }).MagnitudeSqrd() < 3.0f * 3.0f)
            continue;
        const ImU32 fill = m.hunting ? IM_COL32(255, 196, 40, 70) : IM_COL32(255, 255, 255, 55);
        const ImU32 edge = m.hunting ? IM_COL32(255, 196, 40, 220) : IM_COL32(255, 255, 255, 200);
        const float baseY = m.at.y - 0.5f;
        if (m.sound)
        {
            ImVec2 pts[24];
            int    count = 0;
            for (int i = 0; i < 24; ++i)
            {
                const float a = 6.2831853f * static_cast<float>(i) / 24.0f;
                if (project(Vector3f(m.at.x + std::sin(a) * 1.5f, baseY + 0.05f, m.at.z + std::cos(a) * 1.5f), pts[count]))
                    ++count;
            }
            if (count == 24)
                dl->AddPolyline(pts, count, edge, ImDrawFlags_Closed, 3.0f);
            ImVec2 c;
            if (project(Vector3f(m.at.x, baseY + 0.9f, m.at.z), c))
            {
                const float  size = ImGui::GetFontSize() * 1.6f;
                const ImVec2 ts   = font->CalcTextSizeA(size, FLT_MAX, 0.0f, "?");
                dl->AddText(font, size, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), edge, "?");
            }
            continue;
        }
        ImVec2 feet;
        ImVec2 head;
        if (!project(Vector3f(m.at.x, baseY, m.at.z), feet) || !project(Vector3f(m.at.x, m.at.y + 1.2f, m.at.z), head))
            continue;
        const float  h     = feet.y - head.y;
        const float  headR = h * 0.1f;
        const ImVec2 headC{ head.x, head.y + headR };
        const float  sh    = h * 0.2f;
        const float  hip   = h * 0.12f;
        const float  neckY = headC.y + headR * 1.25f;
        const float  hipY  = neckY + h * 0.38f;
        const ImVec2 shape[6] = { { feet.x - sh, neckY }, { feet.x + sh, neckY }, { feet.x + hip, hipY }, { feet.x + hip * 0.9f, feet.y }, { feet.x - hip * 0.9f, feet.y }, { feet.x - hip, hipY } };
        dl->AddCircleFilled(headC, headR, fill, 20);
        dl->AddCircle(headC, headR, edge, 20, 2.0f);
        dl->AddConvexPolyFilled(shape, 6, fill);
        dl->AddPolyline(shape, 6, edge, ImDrawFlags_Closed, 2.0f);
    }

    {
        float exposure = 1.0f;
        if (const PlayerStealthComponent* stealth = app().localStealth())
            exposure = stealth->exposure;
        const float  e      = Math::Clamp(exposure, 0.0f, 1.0f);
        const ImVec2 c{ mid.x, vp.y + vh - 70.0f };
        const float  w      = 34.0f;
        const float  open   = 4.0f + 12.0f * e;
        const ImU32  col    = e > 0.6f ? IM_COL32(255, 220, 120, 230) : (e > 0.3f ? IM_COL32(230, 230, 230, 210) : IM_COL32(140, 150, 160, 180));
        dl->AddBezierQuadratic(ImVec2(c.x - w, c.y), ImVec2(c.x, c.y - open * 2.0f), ImVec2(c.x + w, c.y), col, 3.0f);
        dl->AddBezierQuadratic(ImVec2(c.x - w, c.y), ImVec2(c.x, c.y + open * 2.0f), ImVec2(c.x + w, c.y), col, 3.0f);
        dl->AddCircleFilled(c, 3.0f + 6.0f * e, col, 20);
    }

    bool inGoal = false;
    if (pxf && !app().m_play.quest().active())
    {
        app().world().each<GoalVolumeComponent>([&](Entity, GoalVolumeComponent& goal) {
            if (inGoal)
                return;
            const AABox3f& box = goal.box;
            if (box.Max.x > box.Min.x && pxf->position.x >= box.Min.x && pxf->position.x <= box.Max.x && pxf->position.z >= box.Min.z && pxf->position.z <= box.Max.z)
                inGoal = true;
        });
    }
    if (inGoal)
        app().m_goalShown = 4.0f;
    if (app().m_goalShown > 0.0f)
    {
        app().m_goalShown -= app().m_frameDt;
        const char*  text = "OBJECTIVE COMPLETE";
        const float  size = ImGui::GetFontSize() * 2.6f;
        const ImVec2 ts   = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
        const ImVec2 pos{ mid.x - ts.x * 0.5f, vp.y + vh * 0.28f };
        dl->AddText(font, size, ImVec2(pos.x + 2.0f, pos.y + 2.0f), IM_COL32(0, 0, 0, 200), text);
        dl->AddText(font, size, pos, IM_COL32(120, 230, 140, 255), text);
    }
}

void SandboxHud::drawDefenseText()
{
    if (m_defenseTextLeft <= 0.0f || !m_defenseText || !m_defenseText[0])
        return;
    const float          a     = Math::Clamp(m_defenseTextLeft / 0.7f, 0.0f, 1.0f);
    const bool           parry = m_defenseParry;
    const ImGuiViewport* vp   = ImGui::GetMainViewport();
    ImDrawList*          dl   = ImGui::GetForegroundDrawList(const_cast<ImGuiViewport*>(vp));
    const float          size = ImGui::GetFontSize() * (parry ? 2.2f : 1.7f);
    const ImVec2         ts   = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, m_defenseText);
    const ImVec2         pos{ vp->Pos.x + (vp->Size.x - ts.x) * 0.5f, vp->Pos.y + vp->Size.y * 0.36f - (1.0f - a) * 18.0f };
    const int            alpha = static_cast<int>(a * 255.0f);
    dl->AddText(ImGui::GetFont(), size, ImVec2(pos.x + 2.0f, pos.y + 2.0f), IM_COL32(0, 0, 0, alpha * 3 / 4), m_defenseText);
    dl->AddText(ImGui::GetFont(), size, pos, parry ? IM_COL32(255, 210, 70, alpha) : IM_COL32(170, 210, 255, alpha), m_defenseText);
}

void SandboxHud::drawTargetMarker()
{
    const Entity              body = app().possessedBody();
    const TransformComponent* pxf  = body.valid() ? app().world().get<TransformComponent>(body) : nullptr;
    WeaponLoadout*            w    = app().localWeapons();
    if (!pxf)
        return;

    if (!w || w->activeKind() != WeaponKind::Melee || !app().ownsWeapon(WeaponKind::Melee))
        return;
    WeaponFireRequest req{};
    req.ownerPos               = pxf->position;
    req.direction              = app().m_viewCamera.GetLook();
    const WeaponWorldQuery q   = app().makeWeaponQuery();
    const int              hit = w->melee().pickTarget(req, q);
    if (hit < 0 || !q.targetEntityAt)
        return;
    const Entity target = q.targetEntityAt(q.targetUser, hit);
    const TransformComponent* xf = target.valid() && app().world().alive(target) ? app().world().get<TransformComponent>(target) : nullptr;
    ImDrawList*               dl = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
    const float               vw = static_cast<float>(app().renderer().width());
    const float               vh = static_cast<float>(app().renderer().height());
    if (!xf || !dl || vw < 1.0f || vh < 1.0f)
        return;

    const Vector4f clip = app().m_viewCamera.GetViewProj() * Vector4f(Vector3f(xf->position.x, xf->position.y + 1.1f, xf->position.z), 1.0f);
    if (clip.w <= 1.0e-4f)
        return;
    const float invW = 1.0f / clip.w;
    if (clip.z * invW < 0.0f || clip.z * invW > 1.0f)
        return;
    const ImVec2 vp = ImGui::GetMainViewport()->Pos;
    const float  sx = vp.x + (clip.x * invW * 0.5f + 0.5f) * vw;
    const float  sy = vp.y + (1.0f - (clip.y * invW * 0.5f + 0.5f)) * vh;
    const float  r  = 6.0f;
    const ImU32  c  = IM_COL32(255, 255, 255, 200);
    dl->AddTriangleFilled(ImVec2(sx - r - 1.5f, sy - r - 1.5f), ImVec2(sx + r + 1.5f, sy - r - 1.5f), ImVec2(sx, sy + 1.5f), IM_COL32(0, 0, 0, 180));
    dl->AddTriangleFilled(ImVec2(sx - r, sy - r), ImVec2(sx + r, sy - r), ImVec2(sx, sy), c);
}

void SandboxHud::drawLoot()
{
    const Entity              body = app().possessedBody();
    InventoryComponent*       inv  = body.valid() ? app().world().get<InventoryComponent>(body) : nullptr;
    const TransformComponent* pxf  = body.valid() ? app().world().get<TransformComponent>(body) : nullptr;
    LootComponent*            loot = app().world().alive(m_lootTarget) ? app().world().get<LootComponent>(m_lootTarget) : nullptr;
    const TransformComponent* lxf  = loot ? app().world().get<TransformComponent>(m_lootTarget) : nullptr;
    if (!inv || !pxf || !lxf || (lxf->position - pxf->position).MagnitudeSqrd() > 3.0f * 3.0f)
    {
        m_lootTarget = {};
        return;
    }

    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(320.0f, 300.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(loot->label, &open))
    {
        const bool takeAll = ImGui::Button("Take all");
        ImGui::Separator();
        for (int i = 0; i < InventoryComponent::kBagSize; ++i)
        {
            ItemStack& item = loot->contents.bag[i];
            if (item.empty())
                continue;
            ImGui::PushID(i);
            ImGui::Text("%s x%d (%s)", item.def->name, item.count, itemTypeName(item.def->type));
            ImGui::SameLine();
            if (ImGui::SmallButton("Take") || takeAll)
            {
                item.count -= app().giveItem(*inv, item);
                if (item.count <= 0)
                    item = {};
            }
            ImGui::PopID();
        }
        if (itemCount(loot->contents) == 0)
            ImGui::TextUnformatted("Empty");
    }
    ImGui::End();

    settleLootTarget(app().world(), app().pins(), app().m_physics, m_lootTarget, open);
}

void SandboxHud::drawNpcInfoOverlay()
{
    if (!app().m_npcInfo.enabled || !m_imgui.isReady())
        return;
    if (!app().m_npcInfo.showState && !app().m_npcInfo.showHealth && !app().m_npcInfo.showTarget)
        return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
    if (!dl)
        return;

    const float vw = static_cast<float>(app().renderer().width());
    const float vh = static_cast<float>(app().renderer().height());
    if (vw < 1.0f || vh < 1.0f)
        return;
    const ImGuiViewport* vp    = ImGui::GetMainViewport();
    const ImVec2         vpPos = vp ? vp->Pos : ImVec2(0.0f, 0.0f);

    app().world().each<BrainComponent>([&](Entity e, BrainComponent& bc) {
        if (!bc.brain)
            return;
        const TransformComponent* xf = app().world().get<TransformComponent>(e);
        if (!xf)
            return;

        NpcInfoSample sample{};
        sample.state = bc.brain->graph().leafName();
        if (const HealthComponent* hp = app().world().get<HealthComponent>(e))
        {
            sample.hasHealth = true;
            sample.hp        = hp->health.hp();
            sample.maxHp     = hp->health.maxHp();
        }
        const bool chasing = bc.brain->leaf() == AI::Leaf::Chase;
        if (const AiAgentComponent* ai = app().world().get<AiAgentComponent>(e))
            sample.hasTarget = chasing || ai->hasLastSeen;
        else
            sample.hasTarget = chasing;

        char text[192];
        if (formatNpcInfoBox(app().m_npcInfo, sample, text, static_cast<int>(sizeof(text))) <= 0)
            return;

        float head = 1.85f;
        if (const HittableComponent* ht = app().world().get<HittableComponent>(e))
            head = ht->halfExtents.y * 2.0f * xf->scale.y + 0.45f;
        const Vector3f worldPos(xf->position.x, xf->position.y + head, xf->position.z);
        const Vector4f clip = app().m_viewCamera.GetViewProj() * Vector4f(worldPos, 1.0f);
        if (clip.w <= 1.0e-4f)
            return;
        const float invW = 1.0f / clip.w;
        const float ndcZ = clip.z * invW;
        if (ndcZ < 0.0f || ndcZ > 1.0f)
            return;
        const float sx = (clip.x * invW * 0.5f + 0.5f) * vw;
        const float sy = (1.0f - (clip.y * invW * 0.5f + 0.5f)) * vh;
        if (sx < -120.0f || sy < -80.0f || sx > vw + 120.0f || sy > vh + 80.0f)
            return;

        const ImVec2 ts  = ImGui::CalcTextSize(text);
        const float  pad = 6.0f;
        const ImVec2 p(vpPos.x + sx - ts.x * 0.5f, vpPos.y + sy - ts.y - 6.0f);
        const ImVec2 a(p.x - pad, p.y - pad);
        const ImVec2 b(p.x + ts.x + pad, p.y + ts.y + pad);
        dl->AddRectFilled(a, b, IM_COL32(12, 14, 18, 210), 5.0f);
        dl->AddRect(a, b, IM_COL32(220, 196, 72, 200), 5.0f, 0, 1.2f);
        dl->AddText(p, IM_COL32(245, 245, 245, 255), text);
    });
}

void SandboxHud::drawProfiler()
{
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    const ImVec2           display = ImGui::GetIO().DisplaySize;
    const float            height  = display.y > 240.0f ? display.y * 0.62f : 480.0f;
    ImGui::SetNextWindowPos(ImVec2(display.x - 16.0f, 16.0f), ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(440.0f, height), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Profiler", &app().m_showProfiler, flags))
    {
        ImGui::End();
        return;
    }

    const ProfileSnapshot& snap = profileSnapshot();
    ImGui::Text("%.1f fps", static_cast<double>(snap.fps));
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::Text("frame %.2f ms", static_cast<double>(snap.frameMs));
    ImGui::Text("CPU %.2f ms", static_cast<double>(snap.cpuMs));
    ImGui::SameLine(0.0f, 16.0f);
    if (snap.gpuValid)
        ImGui::Text("GPU %.2f ms", static_cast<double>(snap.gpuMs));
    else
        ImGui::TextUnformatted("GPU --");
    ImGui::TextDisabled("PIX groups. GPU time lags by a frame or two. P hides.");
    ImGui::Separator();

    const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp
        | ImGuiTableFlags_PadOuterX;
    if (ImGui::BeginTable("pix_groups", 3, tableFlags, ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))
    {
        ImGui::TableSetupColumn("PIX group", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("CPU ms", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (uint32_t i = 0; i < snap.rangeCount; ++i)
        {
            const ProfileRange& range = snap.ranges[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const unsigned depth = range.depth > 8 ? 8u : range.depth;
            if (depth > 0)
            {
                ImGui::Dummy(ImVec2(static_cast<float>(depth) * 12.0f, ImGui::GetTextLineHeight()));
                ImGui::SameLine(0.0f, 0.0f);
            }
            const ImVec2 sw = ImGui::GetCursorScreenPos();
            const float  lh = ImGui::GetTextLineHeight();
            const unsigned r = (range.color >> 16) & 255u;
            const unsigned g = (range.color >> 8) & 255u;
            const unsigned b = range.color & 255u;
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(sw.x, sw.y + 2.0f), ImVec2(sw.x + 8.0f, sw.y + lh - 1.0f), IM_COL32(r, g, b, 255), 1.0f);
            ImGui::Dummy(ImVec2(8.0f, lh));
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::TextUnformatted(range.name[0] != '\0' ? range.name : "(unnamed)");

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", static_cast<double>(range.cpuMs));
            ImGui::TableSetColumnIndex(2);
            if (range.gpuMs < 0.0f)
                ImGui::TextUnformatted("--");
            else
                ImGui::Text("%.3f", static_cast<double>(range.gpuMs));
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void SandboxHud::drawPauseOverlay()
{
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, 16.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    if (ImGui::Begin("##pausedBanner", nullptr, flags))
        ImGui::TextUnformatted("PAUSED  fly cam  WASD move  Q/Space up  Z/Ctrl down  Shift sprint  mouse look  O step  F3 resume");
    ImGui::End();
}

void SandboxHud::drawDevTools()
{
    ImGui::SetNextWindowSize(ImVec2(460.0f, 720.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(28.0f, 28.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Dev Tools", &app().m_showDevTools))
    {
        ImGui::End();
        return;
    }

    if (ImGui::Checkbox("Pause gameplay (fly cam)", &app().m_gameplayPaused))
    {
        app().m_stepGameplay = false;
        DE_LOG_INFO("Sandbox: gameplay paused = {}", app().m_gameplayPaused);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("F3");
    ImGui::BeginDisabled(!app().m_gameplayPaused);
    if (ImGui::Button(ICON_FA_FORWARD_STEP "  Step"))
        app().m_stepGameplay = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("O");
    ImGui::SliderFloat(ICON_FA_GAUGE_HIGH "  Fly speed (m/s)", &app().m_flySpeed, 1.0f, 400.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Free-fly camera while paused. Shift multiplies by 2.5.");
    ImGui::Separator();
    if (ImGui::Checkbox("God Mode", &app().m_godMode))
    {
        app().syncPlayerMode();
        DE_LOG_INFO("Sandbox: god mode = {}", app().m_godMode);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Player takes no damage and plays no hit reaction.");
    if (ImGui::Checkbox("Reaper Mode", &app().m_reaperMode))
    {
        app().syncPlayerMode();
        DE_LOG_INFO("Sandbox: reaper mode = {}", app().m_reaperMode);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Player takes no damage and plays no hit reaction. Any entity that attacks the player dies.");
    ImGui::Separator();

    DebugRenderState& dbg = app().renderer().debugState();
    const float elevDeg   = app().m_env.sunElevation() * 57.2957795f;

    ImGui::TextUnformatted(app().renderer().scenePath() == ScenePath::HybridDeferred ? "Path: hybrid deferred" : "Path: forward");
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::Text("%.0f fps", static_cast<double>(ImGui::GetIO().Framerate));
    ImGui::Text("Draws %u  tris %u  sun %.1f deg", app().renderer().stats().drawCalls, app().renderer().stats().triangles, static_cast<double>(elevDeg));
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int fill = static_cast<int>(dbg.fill);
        if (ImGui::RadioButton("Solid", fill == 0))
        {
            dbg.fill = DebugFill::Solid;
            DE_LOG_INFO("Sandbox: fill = {}", toString(dbg.fill));
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Wireframe", fill == 1))
        {
            dbg.fill = DebugFill::Wireframe;
            DE_LOG_INFO("Sandbox: fill = {}", toString(dbg.fill));
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Points", fill == 2))
        {
            dbg.fill = DebugFill::Points;
            DE_LOG_INFO("Sandbox: fill = {}", toString(dbg.fill));
        }

        if (ImGui::Checkbox("Lighting", &dbg.lighting))
            DE_LOG_INFO("Sandbox: lighting = {}", dbg.lighting);
        ImGui::SameLine();
        ImGui::TextDisabled("F2");
        if (app().renderer().hasGBuffer())
        {
            if (ImGui::Checkbox("Local lights", &dbg.localLights))
                DE_LOG_INFO("Sandbox: local lights = {}", dbg.localLights);
            if (ImGui::Checkbox("Cloud volumes", &dbg.clouds))
                DE_LOG_INFO("Sandbox: cloud volumes = {}", dbg.clouds);
        }

        bool shadows = dbg.shadows;
        if (ImGui::Checkbox("Shadows", &shadows))
        {
            dbg.shadows = shadows;
            app().m_frame.scene().shadows().setDebugEnabled(shadows);
            app().m_frame.scene().localShadows().setDebugEnabled(shadows);
            DE_LOG_INFO("Sandbox: shadows = {}", shadows);
        }
        if (ImGui::Checkbox("GPU particles", &dbg.gpuParticles))
            DE_LOG_INFO("Sandbox: GPU particles = {}", dbg.gpuParticles);
        if (ImGui::Checkbox("Instanced static meshes", &dbg.instancedStatic))
            DE_LOG_INFO("Sandbox: instanced static = {}", dbg.instancedStatic);
        if (ImGui::Checkbox("Indirect static draws", &dbg.instancedIndirect))
            DE_LOG_INFO("Sandbox: indirect static draws = {}", dbg.instancedIndirect);
        if (ImGui::Checkbox("Shadow map tiles", &app().m_showShadowMaps))
        {
            dbg.shadowMapTiles = app().m_showShadowMaps;
            DE_LOG_INFO("Sandbox: shadow map overlay = {}", app().m_showShadowMaps);
        }
        {
            const char* shadowDbg[] = { "Off", "Shadow factor", "Face id" };
            int mode = dbg.localShadowDebug;
            if (mode < 0 || mode > 2)
                mode = 0;
            if (ImGui::Combo("Local shadow", &mode, shadowDbg, 3))
                dbg.localShadowDebug = mode;
        }
        if (ImGui::Checkbox("Depth tile", &app().m_showDepth))
            DE_LOG_INFO("Sandbox: depth overlay = {}", app().m_showDepth);
        if (app().renderer().hasGBuffer())
        {
            if (ImGui::Checkbox("G-buffer tiles", &app().m_showGBuffer))
                DE_LOG_INFO("Sandbox: G-buffer overlay = {}", app().m_showGBuffer);
            if (ImGui::Checkbox("Velocity tile", &app().m_showVelocity))
                DE_LOG_INFO("Sandbox: velocity overlay = {}", app().m_showVelocity);
        }
        if (app().renderer().hasSceneBuffers())
        {
            if (ImGui::Checkbox("ACES tonemap", &dbg.aces))
                DE_LOG_INFO("Sandbox: ACES = {}", dbg.aces);
        }
        if (ImGui::Checkbox("Legacy UNORM albedo", &dbg.legacyUnormAlbedo))
        {
            app().renderer().gpuResources().setAlbedoSamplingRaw(dbg.legacyUnormAlbedo);
            if (app().m_terrainMaterial.isValid())
                app().m_terrainMaterial.setLayerSamplingRaw(app().renderer().device(), dbg.legacyUnormAlbedo);
            DE_LOG_INFO("Sandbox: legacy UNORM albedo = {}", dbg.legacyUnormAlbedo);
        }
        if (app().renderer().hasGBuffer())
        {
            if (ImGui::Checkbox("Show albedo (linear)", &dbg.showAlbedoLinear))
                DE_LOG_INFO("Sandbox: show albedo linear = {}", dbg.showAlbedoLinear);
            if (ImGui::Checkbox("Show albedo (raw UNORM)", &dbg.showAlbedoRaw))
            {
                app().renderer().setLightingAlbedoRaw(dbg.showAlbedoRaw);
                DE_LOG_INFO("Sandbox: show albedo raw = {}", dbg.showAlbedoRaw);
            }
        }
        if (app().renderer().hasGBuffer())
        {
            if (ImGui::Checkbox("Bloom", &dbg.bloom))
                DE_LOG_INFO("Sandbox: bloom = {}", dbg.bloom);
            if (ImGui::Checkbox("TAA", &dbg.taa))
                DE_LOG_INFO("Sandbox: TAA = {}", dbg.taa);
            if (ImGui::Checkbox("Motion blur", &dbg.motionBlur))
                DE_LOG_INFO("Sandbox: motion blur = {}", dbg.motionBlur);
        }
        if (ImGui::Checkbox("Skeleton overlay", &app().m_showSkeleton))
            DE_LOG_INFO("Sandbox: skeleton overlay = {}", app().m_showSkeleton);
        if (ImGui::Checkbox("Decals", &dbg.decalsEnabled))
            DE_LOG_INFO("Sandbox: decals = {}", dbg.decalsEnabled);
        bool decalVolumes = dbg.decalsDebug != 0;
        if (ImGui::Checkbox("Decal volumes", &decalVolumes))
        {
            dbg.decalsDebug = decalVolumes ? 1 : 0;
            DE_LOG_INFO("Sandbox: decal volumes = {}", dbg.decalsDebug);
        }
        {
            const DecalFrameStats decalStats = app().m_frame.scene().decalStats();
            ImGui::Text("Decals  alive %u  drawn %u  culled %u  inside %u  tris %u  recycle %u",
                decalStats.alive, decalStats.drawn, decalStats.culled, decalStats.inside, decalStats.triangles, decalStats.recycleCount);
        }
        if (ImGui::Button("Drop burn"))
        {
            const Vector3f cam = app().m_viewCamera.GetPosition();
            Vector3f look = app().m_viewCamera.GetLook();
            look.y = 0.0f;
            if (look.MagnitudeSqrd() < 1.0e-8f)
                look = Vector3f(0.0f, 0.0f, 1.0f);
            else
                look.Normalize();
            const float x = cam.x + look.x * 1.5f;
            const float z = cam.z + look.z * 1.5f;
            DecalSpawnDesc burn;
            burn.kind     = DecalKind::Burn;
            burn.space    = DecalSpace::World;
            burn.position = Vector3f(x, app().playerGroundHeight(x, z), z);
            burn.axisY    = Vector3f(0.0f, 1.0f, 0.0f);
            burn.axisX    = look;
            if (!app().m_frame.scene().spawnDecal(burn))
                DE_LOG_ERROR(LogCategory::Render, "Sandbox: drop burn failed");
            else
                DE_LOG_INFO("Sandbox: drop burn");
        }
        if (ImGui::Checkbox("Octopus camouflage", &app().m_vision.camouflage))
            DE_LOG_INFO("Sandbox: camouflage = {}", app().m_vision.camouflage);
        ImGui::SameLine();
        ImGui::TextDisabled("C");
    }

    if (ImGui::CollapsingHeader("NPC info", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Checkbox("Show overlay", &app().m_npcInfo.enabled))
            DE_LOG_INFO("Sandbox: NPC info overlay = {}", app().m_npcInfo.enabled);
        ImGui::BeginDisabled(!app().m_npcInfo.enabled);
        if (ImGui::Checkbox("AI state", &app().m_npcInfo.showState))
            DE_LOG_INFO("Sandbox: NPC info state = {}", app().m_npcInfo.showState);
        if (ImGui::Checkbox("Health", &app().m_npcInfo.showHealth))
            DE_LOG_INFO("Sandbox: NPC info health = {}", app().m_npcInfo.showHealth);
        if (ImGui::Checkbox("Has target", &app().m_npcInfo.showTarget))
            DE_LOG_INFO("Sandbox: NPC info target = {}", app().m_npcInfo.showTarget);
        ImGui::EndDisabled();
        ImGui::TextDisabled("World-space box above hunters and wolves.");
    }

    if (ImGui::CollapsingHeader("Camera"))
    {
        float nearZ = app().m_viewCamera.GetNearZ();
        float farZ  = app().m_viewCamera.GetFarZ();
        bool  ch    = false;
        ch |= ImGui::SliderFloat("Near", &nearZ, 0.01f, 1.0f, "%.3f");
        ch |= ImGui::SliderFloat("Far", &farZ, 100.0f, 1.0e6f, "%.0f", ImGuiSliderFlags_Logarithmic);
        if (ch)
            app().m_viewCamera.SetLens(app().m_viewCamera.GetFovY(), app().m_viewCamera.GetAspect(), nearZ, farZ);
    }

    if (app().renderer().hasGBuffer() && ImGui::CollapsingHeader("IBL", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DebugRenderState& iblDbg = app().renderer().debugState();
        if (ImGui::Checkbox("Enabled", &iblDbg.iblEnabled))
            DE_LOG_INFO(LogCategory::Render, "Ibl: enabled={} debug={}", iblDbg.iblEnabled, iblDbg.iblDebug);
        ImGui::SliderFloat("Intensity", &app().m_ibl.intensity, 0.0f, 4.0f, "%.2f");
        float rotDeg = app().m_ibl.rotationRadY * RadToDeg;
        if (ImGui::SliderFloat("Rotation", &rotDeg, -180.0f, 180.0f, "%.1f deg"))
            app().m_ibl.rotationRadY = rotDeg * DegToRad;
        const char* debugViews[] = { "Off", "Irradiance", "Prefilter lod0", "LUT" };
        if (ImGui::Combo("Debug view", &iblDbg.iblDebug, debugViews, 4))
        {
            if (iblDbg.iblDebug < 0)
                iblDbg.iblDebug = 0;
            if (iblDbg.iblDebug > 3)
                iblDbg.iblDebug = 3;
            DE_LOG_INFO(LogCategory::Render, "Ibl: enabled={} debug={}", iblDbg.iblEnabled, iblDbg.iblDebug);
        }
        ImGui::TextUnformatted(app().m_ibl.virtualPath.empty() ? "(off)" : app().m_ibl.virtualPath.c_str());
        ImGui::TextDisabled("Bake %s", app().m_ibl.enabled ? "ready" : "off / failed");
    }

    if (app().renderer().hasGBuffer() && ImGui::CollapsingHeader("SSAO", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PushID("SSAO");
        DebugRenderState& ssaoDbg = app().renderer().debugState();
        if (ImGui::Checkbox("Enabled", &ssaoDbg.ssaoEnabled))
        {
            app().m_ssao.enabled = ssaoDbg.ssaoEnabled;
            DE_LOG_INFO(LogCategory::Render, "Ssao: enabled={} debug={}", ssaoDbg.ssaoEnabled, ssaoDbg.ssaoDebug);
        }
        ImGui::SliderFloat("Intensity", &app().m_ssao.intensity, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Radius", &app().m_ssao.radius, 0.05f, 2.0f, "%.2f");
        ImGui::SliderFloat("Power", &app().m_ssao.power, 0.5f, 4.0f, "%.2f");
        const char* ssaoDebugViews[] = { "Off", "SSAO factor" };
        if (ImGui::Combo("Debug view", &ssaoDbg.ssaoDebug, ssaoDebugViews, 2))
        {
            if (ssaoDbg.ssaoDebug < 0)
                ssaoDbg.ssaoDebug = 0;
            if (ssaoDbg.ssaoDebug > 1)
                ssaoDbg.ssaoDebug = 1;
            DE_LOG_INFO(LogCategory::Render, "Ssao: enabled={} debug={}", ssaoDbg.ssaoEnabled, ssaoDbg.ssaoDebug);
        }
        ImGui::PopID();
    }

    if (ImGui::CollapsingHeader("SSR", app().renderer().hasGBuffer() ? ImGuiTreeNodeFlags_DefaultOpen : 0))
    {
        ImGui::PushID("SSR");
        if (!app().renderer().hasGBuffer())
        {
            ImGui::TextDisabled("HybridDeferred only");
        }
        else
        {
            DebugRenderState& ssrDbg = app().renderer().debugState();
            if (ImGui::Checkbox("Enabled", &ssrDbg.ssrEnabled))
            {
                app().m_ssr.enabled = ssrDbg.ssrEnabled;
                DE_LOG_INFO(LogCategory::Render, "Ssr: enabled={} debug={}", ssrDbg.ssrEnabled, ssrDbg.ssrDebug);
            }
            ImGui::SliderFloat("Max roughness", &app().m_ssr.maxRoughness, 0.05f, 1.0f, "%.2f");
            ImGui::SliderFloat("Thickness", &app().m_ssr.thickness, 0.02f, 2.0f, "%.2f m");
            ImGui::SliderFloat("Stride", &app().m_ssr.stride, 1.0f, 8.0f, "%.1f px");
            ImGui::SliderFloat("Edge fade", &app().m_ssr.edgeFade, 0.0f, 0.25f, "%.2f");
            const char* ssrDebugViews[] = { "Off", "Radiance", "Confidence" };
            if (ImGui::Combo("Debug view", &ssrDbg.ssrDebug, ssrDebugViews, 3))
            {
                if (ssrDbg.ssrDebug < 0)
                    ssrDbg.ssrDebug = 0;
                if (ssrDbg.ssrDebug > 2)
                    ssrDbg.ssrDebug = 2;
                DE_LOG_INFO(LogCategory::Render, "Ssr: enabled={} debug={}", ssrDbg.ssrEnabled, ssrDbg.ssrDebug);
            }
        }
        ImGui::PopID();
    }

    if (ImGui::CollapsingHeader("Sky", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const Vector3f lc = app().m_env.lightColor();
        ImGui::ColorButton("##light", ImVec4(lc.x, lc.y, lc.z, 1.0f), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(18.0f, 18.0f));
        ImGui::SameLine();
        ImGui::Text("Light  ambient (%.2f, %.2f, %.2f)", static_cast<double>(app().m_env.ambientColor().x), static_cast<double>(app().m_env.ambientColor().y),
                    static_cast<double>(app().m_env.ambientColor().z));

        float tod = app().m_env.timeOfDay;
        if (ImGui::SliderFloat("Time of day", &tod, 0.0f, 23.99f, "%.2f h"))
        {
            app().m_env.timeOfDay = tod;
            app().m_env.evaluate();
        }
        bool animate = app().m_env.timeScale > 0.0f;
        if (ImGui::Checkbox("Animate time", &animate))
        {
            app().m_env.timeScale = animate ? 0.35f : 0.0f;
            DE_LOG_INFO("Sky: time scale = {:.2f} h/s", app().m_env.timeScale);
        }

        auto weatherBtn = [&](const char* label, const Sky::WeatherState& w, const ImVec4& col) {
            ImGui::PushStyleColor(ImGuiCol_Button, col);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x + 0.12f, col.y + 0.12f, col.z + 0.12f, 1.0f));
            if (ImGui::Button(label, ImVec2(92.0f, 28.0f)))
            {
                app().m_env.weather = w;
                app().m_env.cloudLayer.windSpeedMps = Sky::cloudWindSpeedMpsFor(w);
                app().m_env.evaluate();
                DE_LOG_INFO("Sky: weather {}", label);
            }
            ImGui::PopStyleColor(2);
        };
        weatherBtn("Clear", Sky::WeatherState::Clear(), ImVec4(0.20f, 0.45f, 0.72f, 1.0f));
        ImGui::SameLine();
        weatherBtn("Partly", Sky::WeatherState::PartlyCloudy(), ImVec4(0.32f, 0.42f, 0.55f, 1.0f));
        ImGui::SameLine();
        weatherBtn("Overcast", Sky::WeatherState::Overcast(), ImVec4(0.38f, 0.40f, 0.42f, 1.0f));
        ImGui::SameLine();
        weatherBtn("Storm", Sky::WeatherState::Storm(), ImVec4(0.28f, 0.22f, 0.40f, 1.0f));
        ImGui::Text("Cover %.0f%%  exposure %.2f", static_cast<double>(app().m_env.weather.cloudCoverage * 100.0f), static_cast<double>(app().m_env.exposure()));
    }

    if (app().renderer().hasGBuffer() && ImGui::CollapsingHeader("Exposure", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PushID("Exposure");
        bool autoOn = app().m_autoExposure.mode == ExposureMode::Auto;
        if (ImGui::Checkbox("Auto", &autoOn))
        {
            const ExposureMode next = autoOn ? ExposureMode::Auto : ExposureMode::Manual;
            if (next == ExposureMode::Auto && app().m_autoExposure.mode != ExposureMode::Auto)
            {
                resetAutoExposure(app().m_autoExposureState);
                app().m_frame.scene().autoExposure().invalidateReadback();
            }
            app().m_autoExposure.mode = next;
            DE_LOG_INFO(LogCategory::Render, "Exposure: auto={}", autoOn);
        }
        ImGui::BeginDisabled(!autoOn);
        ImGui::SliderFloat("EV bias", &app().m_autoExposure.evBias, -2.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Max EV", &app().m_autoExposure.maxEv, 0.0f, 3.0f, "%.2f");
        ImGui::SliderFloat("Bright speed", &app().m_autoExposure.adaptBright, 0.0f, 20.0f, "%.2f /s");
        ImGui::SliderFloat("Dark speed", &app().m_autoExposure.adaptDark, 0.0f, 20.0f, "%.2f /s");
        ImGui::EndDisabled();
        ImGui::Text("measured %.3f   EV %+.2f", static_cast<double>(app().m_autoExposureResult.measuredLuma), static_cast<double>(app().m_autoExposureResult.evCorrection));
        ImGui::Text("artistic %.2f   final %.2f", static_cast<double>(app().m_env.exposure()), static_cast<double>(app().m_autoExposureResult.finalExposure));
        ImGui::TextDisabled("Auto adds a small correction on the artistic exposure. Bright views fall faster than dark views rise.");
        ImGui::PopID();
    }

    if (app().renderer().hasGBuffer() && ImGui::CollapsingHeader("Cloud LOD", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PushID("CloudLOD");
        CloudLodSettings& lod = app().m_cloudLod;
        if (ImGui::Checkbox("Enabled", &lod.enabled))
            DE_LOG_INFO(LogCategory::Render, "Cloud LOD: enabled={}", lod.enabled);
        ImGui::BeginDisabled(!lod.enabled);
        ImGui::SliderFloat("Detail distance", &lod.detailDistance, 1.0f, 4000.0f, "%.0f m");
        ImGui::SliderFloat("Fade distance", &lod.fadeDistance, 0.0f, 500.0f, "%.0f m");
        ImGui::SliderFloat("Near step", &lod.nearStep, 0.5f, 64.0f, "%.1f m");
        ImGui::SliderFloat("Far step", &lod.farStep, 0.5f, 256.0f, "%.1f m");
        ImGui::SliderInt("Max near steps", &lod.maxNearSteps, 1, 64);
        ImGui::SliderInt("Max far steps", &lod.maxFarSteps, 1, 64);
        ImGui::SliderInt("Near light steps", &lod.nearLightSteps, 1, 8);
        ImGui::SliderInt("Far light steps", &lod.farLightSteps, 1, 8);
        sanitizeCloudLod(lod);
        ImGui::EndDisabled();
        ImGui::TextDisabled("Rays that exit before the detail distance keep the fixed march: 40 steps, or 56 when the ray starts inside, always 5 sun taps.");
        ImGui::TextDisabled("Longer rays use the near shell, the fade, then the coarse tail. Near and far light steps apply only on that path.");
        ImGui::TextDisabled("The tail step grows past Far step when max far steps cannot cover the remaining distance.");
        ImGui::PopID();
    }

    if (ImGui::CollapsingHeader("Fog", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const Vector3f fc = app().m_env.fogColor();
        ImGui::ColorButton("##fogSwatch", ImVec4(fc.x, fc.y, fc.z, 1.0f), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(18.0f, 18.0f));
        ImGui::SameLine();
        ImGui::Text("dist %.3f  height %.3f  valley %.3f", static_cast<double>(app().m_env.fogDensity()), static_cast<double>(app().m_env.heightFogDensity()),
                    static_cast<double>(app().m_env.volumetricFogDensity()));

        bool autoFog = app().m_env.fogAuto;
        if (ImGui::Checkbox("Auto from weather / time", &autoFog))
        {
            app().m_env.fogAuto = autoFog;
            if (autoFog)
                app().m_env.evaluate();
        }

        if (app().m_env.fogAuto)
        {
            if (ImGui::SliderFloat("Distance scale", &app().m_env.fogDistanceScale, 0.0f, 6.0f, "%.2f"))
                app().m_env.evaluate();
            if (ImGui::SliderFloat("Height scale", &app().m_env.heightFogScale, 0.0f, 6.0f, "%.2f"))
                app().m_env.evaluate();
            if (ImGui::SliderFloat("Valley scale", &app().m_env.volumetricFogScale, 0.0f, 6.0f, "%.2f"))
                app().m_env.evaluate();
        }
        else
        {
            float dist = app().m_env.fogDensity();
            if (ImGui::SliderFloat("Distance density", &dist, 0.0f, 0.08f, "%.4f"))
                app().m_env.setFogDensity(dist);
            float height = app().m_env.heightFogDensity();
            if (ImGui::SliderFloat("Height density", &height, 0.0f, 0.12f, "%.4f"))
                app().m_env.setHeightFogDensity(height);
            float valley = app().m_env.volumetricFogDensity();
            if (ImGui::SliderFloat("Valley density", &valley, 0.0f, 0.12f, "%.4f"))
                app().m_env.setVolumetricFogDensity(valley);
            float col[3] = { fc.x, fc.y, fc.z };
            if (ImGui::ColorEdit3("Fog color", col, ImGuiColorEditFlags_Float))
                app().m_env.setFogColor(Vector3f(col[0], col[1], col[2]));
        }

        float falloff = app().m_env.heightFogFalloff();
        if (ImGui::SliderFloat("Height falloff", &falloff, 0.01f, 0.25f, "%.3f"))
            app().m_env.setHeightFogFalloff(falloff);
        float slab = app().m_env.volumetricFogHeight();
        if (ImGui::SliderFloat("Valley height (m)", &slab, 2.0f, 40.0f, "%.1f"))
            app().m_env.setVolumetricFogHeight(slab);

        if (ImGui::Button("Reset fog"))
        {
            app().m_env.resetFogTune();
            DE_LOG_INFO("Sky: fog tune reset");
        }
    }

    if (ImGui::CollapsingHeader("Water", ImGuiTreeNodeFlags_DefaultOpen))
    {
        WaterParams& wp = app().m_water.params();
        float baseAmp = 0.0f;
        for (int i = 0; i < kWaterWaveCount; ++i)
            baseAmp += wp.waves[i].amplitude;
        float heightM = baseAmp * (wp.amplitudeScale > 0.0f ? wp.amplitudeScale : 0.0f);
        if (ImGui::SliderFloat("Wave height", &heightM, 0.0f, 6.0f, "%.2f m"))
            wp.amplitudeScale = baseAmp > 1.0e-5f ? heightM / baseAmp : 0.0f;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How high the crests rise if every wave peaks together. The authored height is about %.2f m.", static_cast<double>(baseAmp));
        ImGui::SliderFloat("Wave speed", &wp.speedScale, 0.0f, 4.0f, "%.2fx");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How fast the waves travel. 1 is the authored speed. 0 holds them still.");
        ImGui::SliderFloat("Drift", &wp.flowSpeed, 0.0f, 6.0f, "%.2f m/s");
        ImGui::SliderFloat("Foam", &wp.foam, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Foam width", &wp.foamWidthScale, 0.5f, 2.0f, "%.2f");
        ImGui::SliderFloat("Detail", &wp.detailAmount, 0.0f, 1.0f, "%.2f");
        bool legacy = wp.legacyPull;
        if (ImGui::Checkbox("Legacy wave set", &legacy))
        {
            if (legacy)
            {
                copyLegacyHarmonicWaves(wp.waves);
                wp.legacyPull = true;
            }
            else
            {
                const WaterParams fresh = defaultWaterParams(wp.waterLevel);
                for (int i = 0; i < kWaterWaveCount; ++i)
                    wp.waves[i] = fresh.waves[i];
                wp.legacyPull = false;
            }
        }
        if (ImGui::Button("Reset waves"))
        {
            wp.amplitudeScale = 1.0f;
            wp.speedScale     = 1.0f;
        }
    }

    if (ImGui::CollapsingHeader("Procedural grass"))
    {
        Terrain::GrassParams grass = app().m_grassField.params();
        if (!app().m_grassPipeline.isValid())
            grass.enabled = false;
        bool edited = false;
        ImGui::BeginDisabled(!app().m_grassPipeline.isValid());
        edited |= ImGui::Checkbox("Enable", &grass.enabled);
        ImGui::EndDisabled();
        edited |= ImGui::SliderFloat("Height", &grass.heightMetres, 0.05f, 1.50f, "%.2f");
        edited |= ImGui::SliderFloat("Flexibility", &grass.flexibility, 0.0f, 1.0f, "%.2f");
        edited |= ImGui::SliderFloat("Density scale", &grass.densityScale, 0.0f, 1.5f, "%.2f");
        edited |= ImGui::SliderFloat("Wind speed", &grass.windTemporalFreq, 0.0f, 1.0f, "%.2f /s");
        edited |= ImGui::SliderFloat("Edge start", &grass.faceEdge, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Blades more edge-on than this turn toward the camera. 0 leaves them as they are.");
        edited |= ImGui::SliderFloat("Yaw blend", &grass.faceYaw, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How far an edge-on blade turns toward the camera. 1 faces it fully.");
        edited |= ImGui::SliderFloat("Tip lean", &grass.faceLean, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How far the tip leans toward the camera when you look down, as a fraction of blade height.");
        edited |= ImGui::SliderFloat("Steep start", &grass.faceSteep, 0.0f, 0.95f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How overhead the camera must be before the tip leans. 0 starts at the horizon. Saved values come from the scene file.");
        if (!app().m_grassPipeline.isValid())
            grass.enabled = false;
        if (edited)
            app().m_grassField.setParams(grass);
        uint32_t bladeCount = 0;
        for (int lod = 0; lod < Terrain::kGrassLodCount; ++lod)
            bladeCount += app().m_grassField.blades(lod).count;
        ImGui::Text("Blades %u   tiles %u", bladeCount, app().m_grassField.residentTileCount());
    }

    if (ImGui::CollapsingHeader("Falling leaves"))
        ImGui::SliderFloat("Shed scale", &app().m_leafShedScale, 0.0f, 1.0f, "%.2f");

    if (ImGui::CollapsingHeader("Jump attack"))
    {
        const Entity body = app().possessedBody();
        const JumpAttackComponent* jac = body.valid() ? app().world().get<JumpAttackComponent>(body) : nullptr;
        if (!jac)
            ImGui::TextUnformatted("no JumpAttack");
        else
        {
            const Combat::JumpAttack& jump = jac->jump;
            ImGui::Text("phase %s%s", jumpAttackPhaseName(jump.phase()), jump.busy() ? "  (busy)" : "");
            ImGui::Text("connect window %.2fs", static_cast<double>(jump.connectWindowLeft()));
            ImGui::Text("pound radius %.1f m", static_cast<double>(jump.def().poundRadius));
        }
        if (app().m_chaseOk)
        {
            const Entity token = app().m_chase.ai().jumpAttackToken();
            if (token.valid())
                ImGui::Text("pack token %u", token.id());
            else
                ImGui::TextUnformatted("pack token none");
        }
    }

    if (const Entity skillBody = app().possessedBody(); skillBody.valid())
    {
        if (const SkillComponent* sk = app().world().get<SkillComponent>(skillBody))
        {
            if (ImGui::CollapsingHeader("Skills"))
            {
                const int run   = sk->level(SkillId::Run);
                const int swim  = sk->level(SkillId::Swim);
                const int jump  = sk->level(SkillId::Jump);
                const int shoot = sk->level(SkillId::Shoot);
                const int hear  = sk->level(SkillId::Hear);
                const int see   = sk->level(SkillId::See);
                ImGui::Text("Run %d  runScale %.3f", run, static_cast<double>(skillScalar(SkillId::Run, SkillScalar::RunScale, run)));
                ImGui::Text("Swim %d  swimScale %.3f", swim, static_cast<double>(skillScalar(SkillId::Swim, SkillScalar::SwimScale, swim)));
                ImGui::Text("Jump %d  jumpScale %.3f", jump, static_cast<double>(skillScalar(SkillId::Jump, SkillScalar::JumpScale, jump)));
                ImGui::Text("Shoot %d  recoilScale %.3f  cooldownScale %.3f", shoot, static_cast<double>(skillScalar(SkillId::Shoot, SkillScalar::RecoilScale, shoot)),
                            static_cast<double>(skillScalar(SkillId::Shoot, SkillScalar::CooldownScale, shoot)));
                ImGui::Text("Hear %d  hearScale %.3f", hear, static_cast<double>(skillScalar(SkillId::Hear, SkillScalar::HearScale, hear)));
                ImGui::Text("See %d  seeRangeScale %.3f  seeConeScale %.3f", see, static_cast<double>(skillScalar(SkillId::See, SkillScalar::SeeRangeScale, see)),
                            static_cast<double>(skillScalar(SkillId::See, SkillScalar::SeeConeScale, see)));
            }
        }
    }

    if (ImGui::CollapsingHeader("Status"))
    {
        const Entity body = app().possessedBody();
        Combat::StatusEffectComponent* st = body.valid() ? app().world().get<Combat::StatusEffectComponent>(body) : nullptr;
        if (!st)
            ImGui::TextUnformatted("no StatusEffect");
        else
        {
            bool any = false;
            for (int i = 1; i < Combat::kStatusIdCount; ++i)
            {
                const auto id = static_cast<Combat::StatusId>(i);
                if (!st->has(id))
                    continue;
                const Combat::StatusDef* def = Combat::statusDef(id);
                if (!def || !def->name || !def->name[0])
                    continue;
                ImGui::Text("%s  %.2fs", def->name, static_cast<double>(st->remaining(id)));
                any = true;
            }
            if (!any)
                ImGui::TextDisabled("none");

            if (ImGui::Button("Apply Poison"))
                applyDebugStatus(app().world(), body, Combat::StatusId::Poison);
            ImGui::SameLine();
            if (ImGui::Button("Apply Bleed"))
                applyDebugStatus(app().world(), body, Combat::StatusId::Bleed);
            ImGui::SameLine();
            if (ImGui::Button("Apply Ignite"))
                applyDebugStatus(app().world(), body, Combat::StatusId::Ignite);
            if (ImGui::Button("Apply Chill"))
                applyDebugStatus(app().world(), body, Combat::StatusId::Chill);
            ImGui::SameLine();
            if (ImGui::Button("Apply Shock"))
                applyDebugStatus(app().world(), body, Combat::StatusId::Shock);
            ImGui::SameLine();
            if (ImGui::Button("Apply Stun"))
                applyDebugStatus(app().world(), body, Combat::StatusId::Stun);
        }
    }

    if (ImGui::CollapsingHeader("Hit reaction"))
    {
        if (HitReaction* playerHit = app().localHit())
        {
            HitReactionSettings player = playerHit->settings();
            ImGui::TextUnformatted("Player");
            ImGui::SliderFloat("Player stun (s)", &player.stunSeconds, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("Player knockback (m)", &player.knockbackDistance, 0.0f, 6.0f, "%.2f");
            ImGui::SliderFloat("Player knockback time (s)", &player.knockbackSeconds, 0.0f, 1.0f, "%.2f");
            ImGui::Checkbox("Player horizontal only", &player.horizontalOnly);
            playerHit->setSettings(player);
            if (playerHit->stunned())
                ImGui::TextDisabled("stunned %.2fs", static_cast<double>(playerHit->stunRemaining()));
        }

        if (app().m_chaseOk)
        {
            ImGui::Separator();
            HitReactionSettings hunter = app().m_chase.hunterHitReaction();
            ImGui::TextUnformatted("Hunters");
            const bool chStun = ImGui::SliderFloat("Hunter stun (s)", &hunter.stunSeconds, 0.0f, 2.0f, "%.2f");
            const bool chKb   = ImGui::SliderFloat("Hunter knockback (m)", &hunter.knockbackDistance, 0.0f, 8.0f, "%.2f");
            const bool chTime = ImGui::SliderFloat("Hunter knockback time (s)", &hunter.knockbackSeconds, 0.0f, 1.0f, "%.2f");
            const bool chFlat = ImGui::Checkbox("Hunter horizontal only", &hunter.horizontalOnly);
            if (chStun || chKb || chTime || chFlat)
                app().m_chase.setHunterHitReaction(hunter);
        }
    }

    if (ImGui::CollapsingHeader("Crouch"))
    {
        if (PlayerMotor* motor = app().localMotor())
        {
            ImGui::SliderFloat("Crouch speed", &motor->settings().crouchSpeed, 0.5f, motor->settings().walkSpeed, "%.1f");
        }
        if (PlayerStealthComponent* stealth = app().localStealth())
        {
            PlayerStealthSettings& s = stealth->settings;
            ImGui::SliderFloat("Still speed", &s.stillSpeed, 0.05f, 2.0f, "%.2f");
            ImGui::SliderFloat("Sight crouch walk", &s.sightCrouchWalk, 0.05f, 1.0f, "%.2f");
            ImGui::SliderFloat("Sight crouch still", &s.sightCrouchStill, 0.05f, 1.0f, "%.2f");
            ImGui::SliderFloat("Hear stand", &s.hearStand, 0.0f, 20.0f, "%.1f");
            ImGui::SliderFloat("Hear walk", &s.hearWalk, 0.0f, 40.0f, "%.1f");
            ImGui::SliderFloat("Hear sprint", &s.hearSprint, 0.0f, 50.0f, "%.1f");
            ImGui::SliderFloat("Hear crouch walk", &s.hearCrouchWalk, 0.0f, 30.0f, "%.1f");
            ImGui::SliderFloat("Hear crouch still", &s.hearCrouchStill, 0.0f, 20.0f, "%.1f");
            ImGui::SliderFloat("Notice distance", &s.standoff, 0.2f, 6.0f, "%.2f");
            ImGui::SliderFloat("Notice crouch walk", &s.standoffCrouchWalk, 0.1f, 6.0f, "%.2f");
            ImGui::SliderFloat("Notice crouch still", &s.standoffCrouchStill, 0.1f, 6.0f, "%.2f");
            ImGui::SliderFloat("Seen height stand", &s.standTargetHeight, 0.05f, 1.5f, "%.2f");
            ImGui::SliderFloat("Seen height crouch", &s.crouchTargetHeight, 0.05f, 1.5f, "%.2f");
        }
        else
            ImGui::TextDisabled("No player");
        const PlayerMotor* shown = app().localMotor();
        ImGui::TextDisabled("%s", (shown && shown->state() == PlayerMoveState::Crouch) ? "crouching" : "standing");
    }

    if (app().m_chaseOk && ImGui::CollapsingHeader("Guard awareness"))
    {
        app().m_chase.ai().ensureAwarenessSettings();
        AI::AwarenessSettings aware = app().m_chase.ai().awarenessSettings();
        bool ch = false;
        auto row = [&](const char* label, float& value, float lo, float hi, const char* fmt) {
            ch |= ImGui::SliderFloat(label, &value, lo, hi, fmt);
        };
        ImGui::TextDisabled("Meter");
        row("Fill near", aware.fillNear, 0.0f, 10.0f, "%.2f");
        row("Falloff", aware.falloff, 0.1f, 40.0f, "%.1f");
        row("Max range", aware.maxRange, 1.0f, 300.0f, "%.0f");
        row("Focus half-angle", aware.focusHalfDeg, 1.0f, 180.0f, "%.0f");
        row("Side half-angle", aware.sideHalfDeg, 1.0f, 180.0f, "%.0f");
        row("Side range", aware.sideRange, 0.0f, 80.0f, "%.1f");
        row("Side still", aware.sideStill, 0.0f, 2.0f, "%.2f");
        row("Side moving", aware.sideMoving, 0.0f, 2.0f, "%.2f");
        row("Near range", aware.nearRange, 0.0f, 40.0f, "%.1f");
        row("Clear rate", aware.clearRate, 0.0f, 10.0f, "%.2f");
        row("Clear exposure", aware.clearExposure, 0.0f, 1.0f, "%.2f");
        row("Clear body", aware.clearBody, 0.0f, 1.0f, "%.2f");
        row("Instant range", aware.instantRange, 0.0f, 30.0f, "%.1f");
        row("Instant exposure", aware.instantExposure, 0.0f, 1.0f, "%.2f");
        row("Instant exposure wary", aware.instantExposureWary, 0.0f, 1.0f, "%.2f");
        row("Proximity range", aware.proximityRange, 0.0f, 15.0f, "%.2f");
        row("Proximity rate", aware.proximityRate, 0.0f, 10.0f, "%.2f");
        row("Look boost", aware.lookBoost, 0.0f, 5.0f, "%.2f");
        row("Evidence grace", aware.evidenceGrace, 0.0f, 5.0f, "%.2f");
        row("Drain calm", aware.drainCalm, 0.0f, 2.0f, "%.2f");
        row("Drain curious", aware.drainCurious, 0.0f, 2.0f, "%.2f");
        row("Suspicious fill", aware.suspiciousFillScale, 0.0f, 6.0f, "%.2f");
        row("Investigate after", aware.investigateAfter, 0.0f, 10.0f, "%.1f");
        row("Investigate seconds", aware.investigateSeconds, 0.0f, 30.0f, "%.1f");
        row("Alert lose seconds", aware.alertLoseSeconds, 0.0f, 15.0f, "%.1f");
        row("Alert track seconds", aware.alertTrackSeconds, 0.0f, 5.0f, "%.1f");
        row("Alert catch seconds", aware.alertCatchSeconds, 0.0f, 30.0f, "%.1f");
        row("Search seconds", aware.searchSeconds, 0.0f, 40.0f, "%.1f");
        row("Search fill", aware.searchFillScale, 0.0f, 8.0f, "%.2f");
        ImGui::TextDisabled("Occlusion and noise");
        row("Alert share radius", aware.alertShareRadius, 0.0f, 40.0f, "%.1f");
        row("React meter", aware.reactMeter, 0.0f, 1.0f, "%.2f");
        row("Conceal shown", aware.concealShown, 0.0f, 1.0f, "%.2f");
        row("Wall scale", aware.wallScale, 0.0f, 1.0f, "%.2f");
        row("Noise base", aware.noiseBase, 0.0f, 2.0f, "%.2f");
        row("Noise falloff", aware.noiseFalloff, 0.0f, 2.0f, "%.2f");
        row("Stand sample high", aware.bodyStandHeight[0], -1.0f, 2.0f, "%.2f");
        row("Stand sample mid", aware.bodyStandHeight[1], -1.0f, 2.0f, "%.2f");
        row("Stand sample low", aware.bodyStandHeight[2], -1.0f, 2.0f, "%.2f");
        row("Crouch sample high", aware.bodyCrouchHeight[0], -1.0f, 2.0f, "%.2f");
        row("Crouch sample mid", aware.bodyCrouchHeight[1], -1.0f, 2.0f, "%.2f");
        row("Crouch sample low", aware.bodyCrouchHeight[2], -1.0f, 2.0f, "%.2f");
        row("Sample weight high", aware.bodyWeight[0], 0.0f, 1.0f, "%.2f");
        row("Sample weight mid", aware.bodyWeight[1], 0.0f, 1.0f, "%.2f");
        row("Sample weight low", aware.bodyWeight[2], 0.0f, 1.0f, "%.2f");
        ImGui::TextDisabled("Exposure and stride");
        row("Stance crouch walk", aware.stanceCrouchWalk, 0.0f, 3.0f, "%.2f");
        row("Stance crouch still", aware.stanceCrouchStill, 0.0f, 3.0f, "%.2f");
        row("Stance sprint", aware.stanceSprint, 0.0f, 3.0f, "%.2f");
        row("Stance walk", aware.stanceWalk, 0.0f, 3.0f, "%.2f");
        row("Stance stand", aware.stanceStand, 0.0f, 3.0f, "%.2f");
        row("Stance camouflage", aware.stanceCamouflage, 0.0f, 1.0f, "%.2f");
        row("Stride crouch", aware.strideCrouch, 0.1f, 6.0f, "%.2f");
        row("Stride sprint", aware.strideSprint, 0.1f, 6.0f, "%.2f");
        row("Stride walk", aware.strideWalk, 0.1f, 6.0f, "%.2f");
        if (ch)
            app().m_chase.ai().setAwarenessSettings(aware);
    }

    if (ImGui::CollapsingHeader("Shield"))
    {
        Combat::ShieldSettings& shield = app().m_play.runtime().offhand.shield.settings();
        ImGui::SliderFloat("Raise time (s)", &shield.raiseSeconds, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Move speed while up", &shield.moveSpeedScale, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Block arc (deg)", &shield.blockArcDeg, 0.0f, 360.0f, "%.0f");
        ImGui::SliderFloat("Attack charge window (s)", &app().m_play.runtime().chargeSettings.attackWindowSeconds, 0.0f, 1.5f, "%.2f");
        ImGui::SliderFloat("Charged attack damage", &app().m_play.runtime().chargeSettings.attackDamageScale, 1.0f, 4.0f, "%.2f");
        ImGui::SliderFloat("Block charge window (s)", &app().m_play.runtime().chargeSettings.blockWindowSeconds, 0.0f, 1.5f, "%.2f");
        ImGui::SliderFloat("Parry stun (s)", &app().m_play.runtime().chargeSettings.parryStunSeconds, 0.1f, 3.0f, "%.2f");
        ImGui::SliderFloat("Charged parry slam (s)", &app().m_play.runtime().chargeSettings.chargedParryStunSeconds, 0.2f, 3.0f, "%.2f");
        ImGui::TextDisabled("%s   %s   %s",
            app().m_play.runtime().offhand.shield.blocking() ? "shield up" : "shield down",
            app().m_play.runtime().blockCharge.isCharged() ? "block charged" : "block tap",
            app().m_play.runtime().attackCharge.isCharged() ? "attack charged" : "attack tap");
        const bool carriesLight = flashlightEquipped(app().possessedBody().valid() ? app().world().get<InventoryComponent>(app().possessedBody()) : nullptr);
        const char* lightLabel = !carriesLight ? "no flashlight" : (app().m_play.runtime().offhand.lightOn ? "flashlight on" : "flashlight off");
        ImGui::TextDisabled("%s", lightLabel);
        ImGui::TextDisabled("Hold RMB, V, or left trigger. The flashlight turns off while the shield is in use.");
        ImGui::TextDisabled("Night sight %s  (N)", app().m_vision.nightSightOn ? "on" : "off");
        ImGui::TextDisabled("Infrared %s  (H)", app().m_vision.infraredOn ? "on" : "off");
        ImGui::TextDisabled("Ultraviolet %s  (U)", app().m_vision.ultravioletOn ? "on" : "off");
        ImGui::TextDisabled("Echolocation %s  (B)", app().m_vision.echoOn ? "on" : "off");
        ImGui::SliderFloat("Refresh period (s)", &app().m_vision.echoChirp.periodSeconds, Dark::EchoChirpSettings::kPeriodMin, Dark::EchoChirpSettings::kPeriodMax, "%.2f");
        ImGui::SliderFloat("Shell speed (m/s)", &app().m_vision.echoChirp.speedMetersPerSec, Dark::EchoChirpSettings::kSpeedMin, Dark::EchoChirpSettings::kSpeedMax, "%.1f");
        ImGui::SliderFloat("Shell range (m)", &app().m_vision.echoChirp.rangeMeters, Dark::EchoChirpSettings::kRangeMin, Dark::EchoChirpSettings::kRangeMax, "%.1f");
        const float period = app().m_vision.echoChirp.periodSeconds;
        const float speed  = app().m_vision.echoChirp.speedMetersPerSec;
        const float range  = app().m_vision.echoChirp.rangeMeters;
        const float hz     = period > 0.0f ? (1.0f / period) : 0.0f;
        const float travel = speed > 0.0f ? (range / speed) : 0.0f;
        ImGui::TextDisabled("%.2f Hz. %.0f m in %.2f s. content/render/echolocation.json is the startup value.", static_cast<double>(hz), static_cast<double>(range), static_cast<double>(travel));
    }

    if (ImGui::CollapsingHeader("Rifle"))
    {
        if (WeaponLoadout* w = app().localWeapons())
        {
            ProjectileWeaponDesc d = w->projectile().desc();
            float pitch = d.recoilPitchDeg;
            float yaw   = d.recoilYawDeg;
            bool  ch    = false;
            ch |= ImGui::SliderFloat("Recoil pitch (deg)", &pitch, 0.0f, 15.0f, "%.2f");
            ch |= ImGui::SliderFloat("Recoil yaw (deg)", &yaw, 0.0f, 8.0f, "%.2f");
            if (ch)
                w->projectile().setRecoilDegrees(pitch, yaw);
            ImGui::TextDisabled("Kick after the shot. 0 = none.");
        }
    }

    if (app().m_chaseOk && ImGui::CollapsingHeader("Hunter pack"))
    {
        PathChase::PackSettings pack = app().m_chase.packSettings();
        bool ch = false;
        ch |= ImGui::SliderFloat("Help if ally within (m)", &pack.assistAllyRadius, 0.0f, 20.0f, "%.1f");
        ch |= ImGui::SliderFloat("Help sprint time (s)", &pack.assistSeconds, 0.5f, 12.0f, "%.1f");
        ch |= ImGui::SliderFloat("Flee after witnessed kill (s)", &pack.fleeSeconds, 0.5f, 12.0f, "%.1f");
        ch |= ImGui::SliderFloat("Attack yell range (m)", &pack.alertRange, 0.0f, 40.0f, "%.1f");
        const bool runCh  = ImGui::SliderFloat("Sprint speed", &pack.sprintSpeed, 2.0f, 28.0f, "%.1f");
        const bool walkCh = ImGui::SliderFloat("Walk speed", &pack.walkSpeed, 1.0f, 16.0f, "%.1f");
        if (ch || runCh || walkCh)
            app().m_chase.setPackSettings(pack);
        if (runCh || walkCh)
        {
            app().world().each<AiAgentComponent>([&](Entity, AiAgentComponent& ai) {
                if (!ai.authoredMove)
                {
                    ai.walkSpeed    = pack.walkSpeed;
                    ai.runSpeed     = pack.sprintSpeed;
                    ai.authoredMove = true;
                }
                if (walkCh)
                    ai.walkSpeed = pack.walkSpeed;
                if (runCh)
                    ai.runSpeed = pack.sprintSpeed;
            });
        }
        ImGui::TextDisabled("Session override. Authored in content/entities/*.entity.movement.json.");
    }

    if (ImGui::CollapsingHeader("Network", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const NetRole role = app().network().role();
        ImGui::Text("Role: %s   peers %u   rtt %.0f ms", roleLabel(role), app().network().peerCount(), static_cast<double>(app().network().rttMs(app().network().localClientId())));
        if (ImGui::Button("Host", ImVec2(88.0f, 0.0f)))
            app().devNetHost();
        ImGui::SameLine();
        if (ImGui::Button("Disconnect", ImVec2(100.0f, 0.0f)))
            app().devNetDisconnect();
        ImGui::SameLine();
        if (ImGui::Button("Browse LAN", ImVec2(100.0f, 0.0f)))
            app().devNetBrowse();

        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputText("##joinHost", app().m_joinHost, sizeof(app().m_joinHost));
        ImGui::SameLine();
        if (ImGui::Button("Join"))
        {
            Address addr{};
            addr.port = kNetDefaultPort;
            if (parseIPv4(app().m_joinHost, addr))
                app().devNetJoin(addr);
            else
                DE_LOG_WARN(LogCategory::Networking, "Sandbox: bad join address '{}'", app().m_joinHost);
        }

        const uint32_t n = app().network().sessionCount();
        if (app().m_netBrowsing)
            ImGui::Text("LAN sessions: %u", n);
        for (uint32_t i = 0; i < n; ++i)
        {
            NetSessionInfo s{};
            if (!app().network().sessionAt(i, s))
                continue;
            char ip[48];
            formatIPv4(ip, sizeof(ip), s.address);
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(s.name[0] ? s.name : "(unnamed)"))
                app().devNetJoin(s.address);
            ImGui::SameLine();
            ImGui::TextDisabled("%s  peers %u", ip, s.peerCount);
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Debugger", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const bool listening = app().debug().isListening();
        ImGui::Text("Visual Debugger: %s", listening ? "listening" : "off");
        if (listening)
            ImGui::Text("TCP %u   client %s", app().debug().boundAddress().port, app().debug().hasClient() ? "connected" : "waiting");
        if (ImGui::Button(listening ? "Stop listen" : "Listen"))
            app().devToggleListen();
        ImGui::SameLine();
        ImGui::TextDisabled("port %u", kDebugDefaultPort);
    }

    ImGui::Separator();
    ImGui::TextDisabled("M hides this panel and recaptures the mouse.");
    ImGui::End();
}
