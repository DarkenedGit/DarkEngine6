#pragma once

#include "AI/HsmGraph.h"
#include "Assets/AssetManager.h"

#include <string>
#include <vector>

// ImGui panel for loading, editing, and saving hierarchical state machine JSON (*.hsm.json).
class HsmEditorPanel
{
public:
    void draw(Dark::AssetManager& assets, bool* open = nullptr);

private:
    void rebuildPreview();
    void loadVirtual(Dark::AssetManager& assets, const std::string& virtualPath);
    void loadTemplate(const Dark::HsmGraphDef& templ, const char* virtualPath);
    bool save(Dark::AssetManager& assets);
    void drawEvents();
    void drawStates();
    void drawTransitions();
    void drawPreview();
    void drawParams();

    Dark::HsmGraphDef      m_def;
    Dark::HsmGraphInstance m_preview;
    std::string            m_virtualPath = "ai/hunter.hsm.json";
    int                    m_selectedState      = 0;
    int                    m_selectedTransition = -1;
    int                    m_selectedEvent      = 0;
    char                   m_status[256]        = {};
    bool                   m_previewDirty       = true;
};
