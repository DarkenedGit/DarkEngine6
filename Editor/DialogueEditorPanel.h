#pragma once

#include "Play/DialogueGraph.h"

#include <imgui.h>

#include <filesystem>
#include <string>
#include <vector>

class DialogueEditorPanel
{
public:
    void draw(bool* open = nullptr);

private:
    bool validate(std::string& error) const;
    bool save();
    void loadPath(const std::filesystem::path& path);
    void drawBank(const char* label, std::vector<Dark::DialogueLine>& bank);
    void drawNode();

    Dark::DialogueGraphDef m_graph;
    std::string            m_virtualPath = "dialogue/hunter_gate.dlg.json";
    int                    m_selectedNode   = 0;
    int                    m_selectedChoice = 0;
    int                    m_selectedLine   = 0;
    char                   m_status[256]    = {};
    ImVec2                 m_pan{ 24.0f, 24.0f };
    float                  m_zoom = 1.0f;
};
