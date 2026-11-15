#pragma once

#include "Scene/SceneTypes.h"
#include "Ui/Icons.h"

#include <imgui.h>

#include <string_view>

namespace Dark
{

    inline const char* iconForType(SceneObjectType type)
    {
        switch (type)
        {
        case SceneObjectType::Sphere:
        case SceneObjectType::Coin:
            return ICON_FA_CIRCLE;
        case SceneObjectType::ParticleEmitter:
            return ICON_FA_BOLT;
        case SceneObjectType::PointLight:
        case SceneObjectType::SpotLight:
            return ICON_FA_LIGHTBULB;
        case SceneObjectType::Player:
        case SceneObjectType::Spawn:
            return ICON_FA_CIRCLE_PLUS;
        case SceneObjectType::Hunter:
        case SceneObjectType::Wolf:
            return ICON_FA_BUG;
        case SceneObjectType::Platform:
        case SceneObjectType::Water:
        case SceneObjectType::Stream:
            return ICON_FA_LAYER_GROUP;
        case SceneObjectType::CloudVolume:
            return ICON_FA_CLOUD;
        case SceneObjectType::Spawner:
            return ICON_FA_FORWARD_STEP;
        case SceneObjectType::Movie:
            return ICON_FA_CUBE;
        default:
            return ICON_FA_CUBE;
        }
    }

    // Archetype editor.icon names. Unknown names use the cube glyph already in the atlas.
    inline const char* iconForContentName(std::string_view name)
    {
        if (name == "circle")
            return ICON_FA_CIRCLE;
        if (name == "bolt")
            return ICON_FA_BOLT;
        if (name == "lightbulb" || name == "sun")
            return ICON_FA_LIGHTBULB;
        if (name == "bug" || name == "crow")
            return ICON_FA_BUG;
        if (name == "cloud")
            return ICON_FA_CLOUD;
        if (name == "forward" || name == "forward-step")
            return ICON_FA_FORWARD_STEP;
        if (name == "circle-plus" || name == "user")
            return ICON_FA_CIRCLE_PLUS;
        if (name == "layer-group" || name == "water")
            return ICON_FA_LAYER_GROUP;
        if (name == "film" || name == "file")
            return ICON_FA_FILE;
        if (name == "xmark" || name == "missing")
            return ICON_FA_XMARK;
        return ICON_FA_CUBE;
    }

    // PNG when the Editor loaded one. Returns false so the caller can prefix iconForType().
    inline bool drawTypeIconImage(uint64_t iconId)
    {
        if (iconId == 0)
            return false;
        const float px = ImGui::GetTextLineHeight();
        ImGui::Image(static_cast<ImTextureID>(iconId), ImVec2(px, px));
        ImGui::SameLine();
        return true;
    }

} // namespace Dark
