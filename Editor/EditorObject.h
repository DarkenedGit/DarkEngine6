#pragma once

#include "Content/SceneObjectSpawn.h"
#include "ECS/Entity.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"
#include "Network/NetTypes.h"
#include "Scene/SceneTypes.h"

#include <string>

namespace Dark
{

    // Editor-only live metadata stored on World entities.
    // SceneFile / SceneObjectData remain serialization DTOs — not a parallel runtime world.
    struct EditorObjectComponent
    {
        static constexpr const char* kTypeName = "EditorObject";

        float           color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
        // Expanded from a placed building. The save writes the placement, not these objects.
        bool            generated = false;
        // Scene pickup id. Play spawns the item once physics is live.
        std::string     pickup;
        int             pickupCount = 1;
        // Content path of a dialogue graph, for example dialogue/hunter_gate.dlg.json.
        std::string     dialogue;
        // Loaded scene record. Save starts here so fields the inspector does not edit are written back.
        SceneObjectData source;
        // Runtime only. Not written to the scene file.
        Entity          playPickup{};
        bool            playPickupSpent = false;
    };

    // Authored pose for Play/Stop. Written when entering Play; restored when stopping.
    struct EditorAuthoredPoseComponent
    {
        static constexpr const char* kTypeName = "EditorAuthoredPose";

        Math::Vector3f   position{};
        Math::Quaternion rotation{};
        Math::Vector3f   scale{ 1.0f, 1.0f, 1.0f };
    };

    inline bool editorPointOrSpot(const SceneObjectData& object)
    {
        if (sceneHasComponent(object, "ParticleEmitter"))
            return false;
        const std::string kind = sceneLightKind(object);
        return kind == "point" || kind == "spot";
    }

    inline bool editorGlobalLight(const SceneObjectData& object)
    {
        const std::string kind = sceneLightKind(object);
        return kind == "ambient" || kind == "directional";
    }

    inline bool editorPawn(const SceneObjectData& object)
    {
        return sceneHasBehavior(object, "player.controller") || sceneHasBehavior(object, "ai.hunter") || sceneHasBehavior(object, "ai.wolf");
    }

    inline bool editorShownInMode(const SceneObjectData& object, std::string_view mode)
    {
        const std::string id = sceneArchetypeId(object);
        if (const Archetype* archetype = findArchetype(id))
            return archetypeVisibleInMode(*archetype, mode);
        const auto colon = id.rfind(':');
        const std::string bare = (colon == std::string::npos) ? id : id.substr(colon + 1);
        const bool twoD = bare == "platform" || bare == "coin" || bare == "spawn";
        return mode == "2d" ? twoD : !twoD;
    }

    inline std::string editorArchetypeLabel(std::string_view id)
    {
        if (const Archetype* archetype = findArchetype(id))
        {
            if (!archetype->editor.label.empty())
                return archetype->editor.label;
            if (!archetype->name.empty())
                return archetype->name;
        }
        if (id.empty())
            return "Object";
        const auto colon = id.rfind(':');
        return colon == std::string_view::npos ? std::string(id) : std::string(id.substr(colon + 1));
    }

    inline NetPrefab prefabFromName(std::string_view name)
    {
        if (name == "sphere")
            return NetPrefab::Sphere;
        if (name == "platform")
            return NetPrefab::Platform;
        if (name == "coin")
            return NetPrefab::Coin;
        if (name == "cube")
            return NetPrefab::Cube;
        return NetPrefab::Unknown;
    }

    inline bool isNetPrefabName(std::string_view name)
    {
        return prefabFromName(name) != NetPrefab::Unknown;
    }

    inline std::string archetypeForPrefab(NetPrefab prefab)
    {
        switch (prefab)
        {
        case NetPrefab::Sphere:    return "core:sphere";
        case NetPrefab::Platform:  return "core:platform";
        case NetPrefab::Coin:      return "core:coin";
        case NetPrefab::Player2D:  return "core:spawn";
        case NetPrefab::PlayerPawn: return "core:player";
        case NetPrefab::Cube:
        default:                   return "core:cube";
        }
    }

    inline std::string editorNetPrefabName(const SceneObjectData& object)
    {
        if (const Archetype* archetype = findArchetype(sceneArchetypeId(object)))
            return archetype->netPrefab;
        return {};
    }

    inline bool editorDefaultScale(std::string_view id, Math::Vector3f& out)
    {
        const Archetype* archetype = findArchetype(id);
        if (!archetype || !archetype->editor.hasDefaultScale)
            return false;
        out = Math::Vector3f{ archetype->editor.defaultScale[0], archetype->editor.defaultScale[1], archetype->editor.defaultScale[2] };
        return true;
    }

    inline bool editorDefaultColor(std::string_view id, float out[4])
    {
        const Archetype* archetype = findArchetype(id);
        if (!archetype || !archetype->editor.hasDefaultColor)
            return false;
        for (int i = 0; i < 4; ++i)
            out[i] = archetype->editor.defaultColor[i];
        return true;
    }

} // namespace Dark
