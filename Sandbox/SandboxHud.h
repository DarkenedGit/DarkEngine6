#pragma once

#include "ECS/Entity.h"
#include "Play/QuestBanner.h"
#include "Ui/ImGuiHost.h"
#include "Ui/MainMenu.h"

#include <vector>

class SandboxApp;

// Dev HUD drawn by Sandbox: quest banners, dialogue and combat readouts, the journal,
// the upgrade panel, ImGui, and the main menu. SandboxApp keeps one SandboxHud.
class SandboxHud
{
public:
    void bind(SandboxApp* app) { m_app = app; }

    void drawDevTools();
    void drawProfiler();
    void drawInventory();
    void drawArmorHud();
    void drawPickupLabels();
    void drawLoot();
    void drawDefenseText();
    void drawTargetMarker();
    void drawGuardAwareness();
    void drawGuardSpeech();
    void drawDialogue();
    void drawStaminaBar();
    void drawQuestHud();
    void drawJournal();
    void drawBreathBar();
    void drawPauseOverlay();
    void drawNpcInfoOverlay();
    void drawAlarm();
    void drawUpgrades();
    void drawUpgradeHint();

    std::vector<Dark::QuestBanner> m_questBanners;
    bool                           m_showJournal      = false;
    bool                           m_showUpgrades     = false;
    const char*                    m_defenseText      = "";
    float                          m_defenseTextLeft  = 0.0f;
    bool                           m_defenseParry     = false;
    Dark::Entity                   m_lootTarget{};
    ImGuiHost                      m_imgui;
    Dark::MainMenu                 m_menu;

private:
    SandboxApp& app() const;
    SandboxApp* m_app = nullptr;
};
