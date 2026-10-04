#pragma once

#include "Scene/SceneTypes.h"
#include "Sky/Environment.h"
#include "Water/WaterWaves.h"

#include <filesystem>
#include <string>

namespace Dark
{

    // JSON scene I/O (versioned). No exceptions — returns false + fills errorOut on failure.
    bool saveSceneToJson(const std::filesystem::path& path, const SceneFileData& scene, std::string* errorOut = nullptr);

    bool loadSceneFromJson(const std::filesystem::path& path, SceneFileData& outScene, std::string* errorOut = nullptr);

    // Copy sky and fog onto an Environment. A missing block resets that group to Environment defaults.
    void applySceneAtmosphere(Sky::Environment& env, const SceneFileData& scene);

    // Read the live Environment back into the scene sky and fog blocks.
    void captureSceneAtmosphere(const Sky::Environment& env, SceneFileData& scene);

    // WaterParams for a level sheet. Omitted waves stay on the built-in Gerstner set.
    WaterParams sceneWaterParams(const WaterSceneDesc& water, float waterLevel);

    // Resolve a default path: <authoring-content>/scenes/<name>.
    // Skips the content copy next to the executable when a source content/ directory exists.
    std::filesystem::path defaultScenePath(const std::filesystem::path& preferredName = "level.json");

} // namespace Dark
