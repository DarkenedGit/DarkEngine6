#pragma once

#include "Math/Quaternion.h"
#include "Math/Vector3f.h"
#include "Scene/SceneTypes.h"

namespace Dark
{

    // Editor-only live metadata stored on World entities.
    // SceneFile / SceneObjectData remain serialization DTOs — not a parallel runtime world.
    struct EditorObjectComponent
    {
        static constexpr const char* kTypeName = "EditorObject";

        SceneObjectType type = SceneObjectType::Cube;
        float           color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
        // Expanded from a placed building. The save writes the placement, not these objects.
        bool            generated = false;
    };

    // Authored pose for Play/Stop. Written when entering Play; restored when stopping.
    struct EditorAuthoredPoseComponent
    {
        static constexpr const char* kTypeName = "EditorAuthoredPose";

        Math::Vector3f   position{};
        Math::Quaternion rotation{};
        Math::Vector3f   scale{ 1.0f, 1.0f, 1.0f };
    };

} // namespace Dark
