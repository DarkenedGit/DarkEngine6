#pragma once

#include "Scene/SceneTypes.h"

namespace Dark
{

    // Editor-only live metadata stored on World entities.
    // SceneFile / SceneObjectData remain serialization DTOs — not a parallel runtime world.
    struct EditorObjectComponent
    {
        static constexpr const char* kTypeName = "EditorObject";

        SceneObjectType type         = SceneObjectType::Cube;
        float           color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
        // Index into EditorApp emitter list when type == ParticleEmitter; -1 otherwise.
        int             emitterIndex = -1;
    };

} // namespace Dark
