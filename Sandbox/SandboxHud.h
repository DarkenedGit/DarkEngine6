#pragma once

#include "ECS/Entity.h"
#include "Play/QuestBanner.h"
#include "Render/Texture2D.h"
#include "Ui/ImGuiHost.h"
#include "Ui/MainMenu.h"
#include "Ui/NpcInfoOverlay.h"

#include <cstdint>
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
    void drawMainMenu();
    void loadMenuBackground();

    float&                         goalShown() { return m_goalShown; }
    Dark::NpcInfoOverlaySettings&  npcInfo() { return m_npcInfo; }

    std::vector<Dark::QuestBanner> m_questBanners;
    float                          m_goalShown = 0.0f;
    Dark::NpcInfoOverlaySettings   m_npcInfo{};
    bool                           m_showJournal      = false;
    bool                           m_showUpgrades     = false;
    const char*                    m_defenseText      = "";
    float                          m_defenseTextLeft  = 0.0f;
    bool                           m_defenseParry     = false;
    Dark::Entity                   m_lootTarget{};
    // Target the loot card already drew. The pickup key takes all only after that first draw.
    Dark::Entity                   m_lootShown{};
    ImGuiHost                      m_imgui;
    Dark::MainMenu                 m_menu;
    Dark::MainMenuPage             m_menuPage = Dark::MainMenuPage::Front;
    Dark::Texture2D                m_menuBackground;
    uint64_t                       m_menuBackgroundId = 0;

private:
    SandboxApp& app() const;
    SandboxApp* m_app = nullptr;
};
