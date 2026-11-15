#pragma once

#include <string>

namespace Dark
{

    // Spawn-time notes filled by component readers. Not a save record.
    struct ArchetypeParamsComponent
    {
        static constexpr const char* kTypeName = "ArchetypeParams";

        std::string archetype;
        std::string modelGltf;
        std::string modelAnim;
        std::string physicsFile;
        std::string combatAttacks;
        std::string dialogue;
        std::string hsm;
        float       healthMax = -1.0f;
        bool        replicate = false;
    };

} // namespace Dark
