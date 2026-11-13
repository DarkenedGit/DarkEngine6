#pragma once

#include "Scene/SceneTypes.h"
#include "Ui/Icons.h"

#include <imgui.h>

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
