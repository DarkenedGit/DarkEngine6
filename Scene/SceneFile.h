#pragma once

#include "Core/JobSystem.h"
#include "Scene/SceneTypes.h"
#include "Sky/Environment.h"
#include "Water/WaterWaves.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Dark
{

    class AssetManager;

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

    struct SceneLoadRequest
    {
        std::filesystem::path path;
        uint64_t              generation = 0;
    };

    struct SceneLoadResult
    {
        JobStatus     status = JobStatus::NotReady;
        SceneFileData scene;
        std::string   error;
    };

    // `assets` is required because mounts live on one AssetManager, not the process.
    bool enqueueSceneLoad(JobSystem& jobs, const AssetManager& assets, const SceneLoadRequest& request);

    // The load applies only when it still belongs to the document that was current when it started.
    inline bool sceneLoadMayApply(uint64_t loadDocument, uint64_t documentGeneration)
    {
        return loadDocument != 0 && loadDocument == documentGeneration;
    }

    // Supersede this generation. A finished record is dropped. An in-flight load publishes Cancelled.
    void cancelSceneLoad(uint64_t generation);

    // true: out.status is terminal (Ok, Failed, or Cancelled) and out is written.
    // false and out.status == NotReady: poll again next pump.
    // false and out.status == Failed: the scheduler is stopped or the generation is unknown.
    bool pollSceneLoad(JobSystem& jobs, uint64_t generation, SceneLoadResult& out);

} // namespace Dark
