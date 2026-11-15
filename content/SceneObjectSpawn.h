#pragma once

#include "Content/ArchetypeSpawn.h"
#include "Math/Vector2f.h"
#include "Particles/ParticleTypes.h"
#include "Play/Spawner.h"
#include "Scene/SceneTypes.h"

#include <string>
#include <string_view>
#include <vector>

namespace Dark
{

    // Archetype id stored on the scene record. Empty when the record has none.
    std::string sceneArchetypeId(const SceneObjectData& object);

    // True when the archetype is empty or not in the loaded content database.
    bool sceneIsPlaceholder(const SceneObjectData& object);
    // True when that archetype is loaded. False while the content database is not loaded.
    bool sceneIsDataDriven(const SceneObjectData& object);

    // Component objects from the bag. instantiateArchetype ignores keys the archetype does not list.
    std::string sceneComponentOverrides(const SceneObjectData& object);

    // Archetype params with the bag's object for that component laid on top. "{}" when neither has it.
    std::string sceneMergedComponent(const SceneObjectData& object, std::string_view component);
    // Archetype params for one component. "{}" when that archetype does not list it.
    std::string sceneArchetypeComponentJson(const SceneObjectData& object, std::string_view component);
    // True when the archetype lists the component or the bag stores an object for it.
    bool sceneCarriesComponent(const SceneObjectData& object, std::string_view component);
    void sceneSetComponentJson(SceneObjectData& object, std::string_view component, std::string_view jsonText);

    // Copies an editor particle desc onto the bag.
    void assignSceneParticle(SceneObjectData& object, const ParticleEmitterDesc& desc);
    // Merged ParticleEmitter params. Missing keys keep ParticleEmitterDesc defaults.
    void sceneReadParticle(const SceneObjectData& object, ParticleEmitterDesc& desc);

    // True when the loaded archetype lists the component. An unknown id is false.
    bool sceneHasComponent(const SceneObjectData& object, std::string_view component);
    bool sceneHasBehavior(const SceneObjectData& object, std::string_view behavior);

    // "", "point", "spot", "ambient", or "directional". Bag light, then the archetype.
    std::string sceneLightKind(const SceneObjectData& object);
    bool        sceneHasLight(const SceneObjectData& object);
    // Kind defaults apply when the merged light JSON omits the key. An authored number wins.
    float sceneLightFloat(const SceneObjectData& object, const char* key, float fallback);
    bool  sceneLightBool(const SceneObjectData& object, const char* key, bool fallback);
    bool  sceneSearchlight(const SceneObjectData& object);
    float sceneSweepDeg(const SceneObjectData& object);

    struct SceneLightFields
    {
        std::string kind;
        float       intensity     = 0.0f;
        float       range         = 8.0f;
        float       innerDeg      = 12.0f;
        float       outerDeg      = 25.0f;
        float       sourceRadius  = 0.05f;
        bool        enabled       = true;
        bool        castShadow    = false;
        bool        searchlight   = false;
        float       sweepDeg      = 60.0f;
        std::string circuit;
    };
    void sceneWriteLight(SceneObjectData& object, const SceneLightFields& light);

    std::string sceneModelPath(const SceneObjectData& object);
    void        sceneWriteModelPath(SceneObjectData& object, std::string path);
    float       sceneEmissive(const SceneObjectData& object);
    void        sceneWriteEmissive(SceneObjectData& object, float emissive);

    std::string sceneMoviePath(const SceneObjectData& object);
    float       sceneMovieRadius(const SceneObjectData& object);
    bool        sceneMovieLoop(const SceneObjectData& object);
    void        sceneWriteMovie(SceneObjectData& object, std::string path, float radius, bool loop);

    bool sceneReadStream(const SceneObjectData& object, float& width, float& flowSpeed, std::vector<Math::Vector2f>& points);
    void sceneWriteStream(SceneObjectData& object, float width, float flowSpeed, const std::vector<Math::Vector2f>& points);

    bool sceneReadSpawner(const SceneObjectData& object, SpawnerDesc& desc);
    void sceneWriteSpawner(SceneObjectData& object, const SpawnerDesc& desc);

    // Writes live keys that differ from archetypeJson. Equal keys are removed.
    // Keys already stored and absent from liveJson stay. A null live value removes that key.
    // An empty result removes the component object.
    void sceneWriteComponentDiff(SceneObjectData& object, std::string_view component, std::string_view liveJson, std::string_view archetypeJson);

    // Editor save. Omitted archetype keys compare against the component default, so an unedited
    // placement does not gain a component object. Searchlight, sweep, and circuit stay on the bag.
    void sceneWriteLightDiff(SceneObjectData& object, const SceneLightFields& light);
    void sceneWriteParticleDiff(SceneObjectData& object, const ParticleEmitterDesc& desc);
    void sceneWriteStreamDiff(SceneObjectData& object, float width, float flowSpeed, const std::vector<Math::Vector2f>& points);
    void sceneWriteSpawnerDiff(SceneObjectData& object, const SpawnerDesc& desc);
    void sceneWriteMovieDiff(SceneObjectData& object, std::string path, float radius, bool loop);
    void sceneWriteModelDiff(SceneObjectData& object, std::string path);
    void sceneWriteEmissiveDiff(SceneObjectData& object, float emissive);

    // Placement markers. Absent keys use the file defaults (block true, pickupCount 1, emissive mesh -1).
    bool                        sceneConceal(const SceneObjectData& object);
    bool                        sceneGoal(const SceneObjectData& object);
    bool                        sceneBlock(const SceneObjectData& object);
    std::string                 sceneZone(const SceneObjectData& object);
    std::string                 sceneCircuit(const SceneObjectData& object);
    std::string                 scenePickup(const SceneObjectData& object);
    int                         scenePickupCount(const SceneObjectData& object);
    std::string                 sceneDialogue(const SceneObjectData& object);
    std::string                 sceneSquad(const SceneObjectData& object);
    std::vector<Math::Vector2f> scenePatrol(const SceneObjectData& object);
    int                         sceneEmissiveMeshIndex(const SceneObjectData& object);

    void sceneSetBool(SceneObjectData& object, const char* key, bool value);
    void sceneSetString(SceneObjectData& object, const char* key, std::string value);
    void sceneSetInt(SceneObjectData& object, const char* key, int value);
    void sceneSetPatrol(SceneObjectData& object, const std::vector<Math::Vector2f>& points);
    void sceneSetLocalLightBool(SceneObjectData& object, const char* key, bool value);
    void sceneSetLocalLightFloat(SceneObjectData& object, const char* key, float value);

    // Splits the bag for the scene writer. raw means emit rawJson unchanged.
    void sceneSaveParts(const SceneObjectData& object, bool& raw, std::string& rawJson, std::string& placementJson, std::string& componentOverrides);

    // "cube", "sphere", or "" from the archetype Mesh primitive, then the bare archetype id.
    std::string sceneMeshPrimitive(const SceneObjectData& object);

    // Cube and sphere props that block walking. Platforms are not obstacles. primitive is "cube" or "sphere".
    bool sceneWalkObstacle(const SceneObjectData& object, std::string& primitive);

    // One spawn. Passes the bag's component objects to instantiateArchetype.
    Entity instantiateSceneObject(const ArchetypeSpawnHost& host, const SceneObjectData& object, ContentError* err = nullptr);

} // namespace Dark
