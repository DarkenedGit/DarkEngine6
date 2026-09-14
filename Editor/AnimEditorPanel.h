#pragma once

#include "Animation/AnimGraphComponent.h"
#include "Assets/AssetManager.h"

// ImGui panel for previewing clips through the animation state machine and editing *.anim.json.
class AnimEditorPanel
{
public:
    void draw(Dark::AnimGraphComponent& ag, Dark::AssetManager& assets, bool* open = nullptr);
    void applyPlayback(Dark::AnimGraphInstance& graph) const;

    bool paused() const { return m_paused; }
    float speedScale() const { return m_speedScale; }

private:
    void drawPlayback(Dark::AnimGraphComponent& ag);
    void drawStatePicker(Dark::AnimGraphComponent& ag);
    void drawParameters(Dark::AnimGraphInstance& graph);
    void drawStateEditor(Dark::AnimGraphComponent& ag);
    void drawTransitionEditor(Dark::AnimGraphComponent& ag);
    void drawNotifyEditor(Dark::AnimGraphDef& def);
    void onParamEdited(Dark::AnimGraphInstance& graph);

    bool saveGraph(Dark::AnimGraphComponent& ag, Dark::AssetManager& assets);
    bool reloadGraph(Dark::AnimGraphComponent& ag, Dark::AssetManager& assets);
    bool createGraphFromClips(Dark::AnimGraphComponent& ag, Dark::AssetManager& assets);

    int   m_selectedTransition = -1;
    int   m_selectedNotify     = -1;
    bool  m_paused             = false;
    float m_speedScale         = 1.0f;
    char  m_status[256]        = {};
};
