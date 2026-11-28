#pragma once

#include <string>
#include <vector>

namespace Dark
{
class Input;
class Renderer;
}

// One row of the player front menu. level rows are the debug jump column.
struct RmlFrontRow
{
    std::string id;
    std::string title;
    std::string subtitle;
    bool        level = false;
};

// What the host should show. An empty show leaves the document hidden.
struct RmlFrontMenu
{
    bool                     show          = false;
    bool                     loading       = false;
    float                    dpi           = 1.0f;
    std::string              title;
    std::string              loadingLabel;
    std::string              selectedId;
    std::vector<RmlFrontRow> rows;
};

// Result of the last update while the front menu was up. pollFront clears the one-shot flags.
struct RmlFrontEvent
{
    bool        activate = false;
    bool        cancel   = false;
    std::string focusId;
    std::string activateId;
};

// One read-only settings row. keys is the device label, what is the action.
struct RmlControlRow
{
    std::string keys;
    std::string what;
};

// Sandbox settings page. masterVolume is 0..1. An empty show leaves the document hidden.
struct RmlSettingsMenu
{
    bool                       show            = false;
    bool                       loading         = false;
    bool                       controlsLoaded  = false;
    float                      dpi             = 1.0f;
    float                      masterVolume    = 1.0f;
    std::string                title;
    std::vector<RmlControlRow> game;
    std::vector<RmlControlRow> engine;
};

// pollSettings clears the one-shot flags. masterVolume is 0..1 when volumeChanged is set.
struct RmlSettingsEvent
{
    bool  back          = false;
    bool  cancel        = false;
    bool  volumeChanged = false;
    float masterVolume  = 1.0f;
};

// Sandbox pause card. show is gameplay paused with the main menu and movies out of the way.
struct RmlPauseMenu
{
    bool  show = false;
    float dpi  = 1.0f;
};

// pollPause clears the one-shot flags.
struct RmlPauseEvent
{
    bool resume = false;
    bool step   = false;
};

// Play stealth readout. suspicion is 0..1. The document does not take the pointer or the keyboard.
struct RmlStealthHud
{
    bool        show          = false;
    float       dpi           = 1.0f;
    float       suspicion     = 0.0f;
    int         nearbyAlerts  = 0;
    bool        hidden        = false;
    std::string prompt;
};

// Player-facing RmlUi context shared by Sandbox and Editor. Dear ImGui stays the
// editor and debug layer and draws after this. DarkEngine does not link RmlUi.
class RmlUiHost
{
public:
    RmlUiHost() = default;
    ~RmlUiHost();

    RmlUiHost(const RmlUiHost&)            = delete;
    RmlUiHost& operator=(const RmlUiHost&) = delete;

    bool init(Dark::Renderer& renderer);
    void shutdown(Dark::Renderer& renderer);

    // Loads ui/menus/front.rml on the first show. The open class is added after the
    // closed style has been committed, so the slide and fade come from RCSS.
    void presentFront(const RmlFrontMenu& menu);

    // Loads ui/menus/settings.rml on the first show. Same open transition as the front page.
    // Left and right step master volume. Up and down scroll a control list. Back and cancel
    // return to the front page; they do not quit.
    void presentSettings(const RmlSettingsMenu& menu);

    // Loads ui/menus/pause.rml on init. Modal, with the same open transition as the other pages.
    // This card does not capture keys or the pointer: fly cam, mouse look, and F3 stay live.
    // Up and down arrows and the D-pad move between Resume and Step. Enter and A activate.
    void presentPause(const RmlPauseMenu& menu);

    // Loads ui/hud/stealth.rml on init. Values flow through the "hud" data model.
    // The document stays pointer-events: none and does not capture input.
    void presentStealth(const RmlStealthHud& hud);

    // Esc, B, and Back while the front page or settings page is up set cancel and ateCancel for this
    // frame so that key does not also quit or pause. A, Enter, and a click set activate.
    void update(Dark::Renderer& renderer, Dark::Input& input, bool imguiMouse, bool imguiKeyboard, float dt);
    void render(Dark::Renderer& renderer);

    RmlFrontEvent pollFront();
    RmlSettingsEvent pollSettings();
    RmlPauseEvent pollPause();

    bool ready() const { return m_ready; }
    bool frontActive() const;
    bool settingsActive() const;
    bool pauseActive() const;
    bool stealthActive() const;
    // Front page and settings only. The pause card and the stealth HUD leave this false.
    bool capturesInput() const { return m_nav || m_mouseOver; }
    bool ateCancel() const { return m_ateCancel; }
    // One frame. Gamepad A on the pause card confirms the focused button. Sandbox ORs this
    // into uiKeys so that press does not also jump.
    bool ateAction() const { return m_ateAction; }

private:
    struct Impl;

    void focusFrontSelection();
    void setFrontOpen(bool open);
    void rebuildFront(const RmlFrontMenu& menu);
    void loadSettings();
    void loadPause();
    void loadStealth();
    void dirtyStealth();
    void setPauseOpen(bool open);
    void setStealthOpen(bool open);
    void setSettingsOpen(bool open);
    void rebuildSettings(const RmlSettingsMenu& menu);
    void updateSettingsInput(Dark::Input& input, float dt);
    void stepSettingsVolume(float deltaPercent);
    // Rml nav:auto will not enter #level-list (it is a scroll container). Left and
    // right move to the remembered row in the other column instead. key is an
    // Rml::Input::KeyIdentifier value; the header does not include RmlUi.
    void switchFrontColumn(int key);
    void rememberFrontFocus();

    Impl* m_impl       = nullptr;
    bool  m_ready      = false;
    bool  m_nav        = false;
    bool  m_mouseOver  = false;
    bool  m_ateCancel  = false;
    bool  m_ateAction  = false;
};
