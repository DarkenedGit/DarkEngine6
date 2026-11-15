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
        float       healthMax        = -1.0f;
        float       healthRegen      = -1.0f;
        float       healthRegenDelay = -1.0f;
        bool        replicate = false;

        // Component params after the spawn override is merged. Empty means that component was not read.
        std::string meshJson;
        std::string localLightJson;
        std::string ambientJson;
        std::string directionalJson;
        std::string particleJson;
        std::string cloudJson;
        std::string streamJson;
        std::string spawnerJson;
        std::string movieJson;
        std::string pickupItem;
        int         pickupCount = 1;

        std::string meshPrimitive;
        float       emissive = 0.0f;
        float       color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
        bool        lightUsesObjectColor = false;
        std::string circuit;
        bool        hasWater = false;
    };

} // namespace Dark
