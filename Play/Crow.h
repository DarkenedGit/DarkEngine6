#pragma once

#include "Assets/AssetHandle.h"
#include "Assets/MeshData.h"
#include "Assets/Model.h"
#include "ECS/Entity.h"
#include "Math/Vector3f.h"
#include "Play/Spawner.h"

#include <cstdint>
#include <vector>

namespace Dark
{

    class AssetManager;
    class AssetPinTable;
    class Renderer;
    class World;

    namespace Audio
    {
        class AudioSystem;
    }

    namespace Terrain
    {
        class TerrainGrid;
    }

    // Flock is the boid. Perch sits in a tree. Forage hops on the ground.
    // Flee is the short flight after a scare, or a voluntary trip to a tree.
    enum class CrowState : uint8_t
    {
        Flock = 0,
        Perch,
        Forage,
        Flee
    };

    struct CrowPerch
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct CrowNeighbor
    {
        Math::Vector3f position{ 0.0f, 0.0f, 0.0f };
        Math::Vector3f velocity{ 0.0f, 0.0f, 0.0f };
        bool           flying = false;
    };

    // Session brain. Not saved. The host copies position onto the transform after each tick.
    struct CrowBrain
    {
        CrowState      state = CrowState::Flock;
        Math::Vector3f position{ 0.0f, 0.0f, 0.0f };
        Math::Vector3f velocity{ 0.0f, 0.0f, 0.0f };
        Math::Vector3f goal{ 0.0f, 0.0f, 0.0f };
        float          yaw         = 0.0f;
        float          pitch       = 0.0f;
        float          wingPhase   = 0.0f;
        float          stateTime   = 0.0f;
        float          decision    = 12.0f;
        float          homeX       = 0.0f;
        float          homeZ       = 0.0f;
        float          cruise      = 8.0f;
        int            wingPose    = 0; // 0 folded, 1 down, 2 up
        bool           alarmLatched = false;
        bool           fleeToTree   = false;
        uint32_t       rng          = 1;
        // Impact divert. Not a CrowState. lureHome is the perch or goal they leave.
        bool           lure       = false;
        CrowState      lureFrom   = CrowState::Flock;
        Math::Vector3f lurePoint{};
        Math::Vector3f lureHome{};
        float          lureLeft   = 0.0f;
    };

    struct CrowComponent
    {
        static constexpr const char* kTypeName = "Crow";

        CrowBrain brain{};
    };

    // Three rigid poses. The bird has no skeleton, so the wing flap swaps meshes.
    // The keeps are the lifetime pins. An ID alone is collected by AssetManager::collectGarbage.
    struct CrowModels
    {
        AssetID         folded = NULL_ASSET;
        AssetID         down   = NULL_ASSET;
        AssetID         up     = NULL_ASSET;
        AssetRef<Model> foldedKeep;
        AssetRef<Model> downKeep;
        AssetRef<Model> upKeep;
        bool            ready = false;
    };

    // wingLift is radians about the shoulders. Negative droops, positive lifts.
    bool buildCrowMesh(MeshData& body, MeshData& beak, MeshData& eyes, float wingLift);
    bool ensureCrowModels(Renderer& renderer, AssetManager& assets, CrowModels& models);

    void initCrow(CrowBrain& brain, float x, float z, float groundY, float homeX, float homeZ, uint32_t seed, int slot, const CrowPerch* perches, int perchCount);

    // Fly to a copied world point, then resume the flock, perch, or forage they left.
    void beginCrowLure(CrowBrain& brain, const Math::Vector3f& impact);

    // True on the single tick that starts an alarm call.
    bool tickCrow(CrowBrain& brain, float groundY, float dt, const CrowNeighbor* neighbors, int neighborCount, const Math::Vector3f* threats, int threatCount, const CrowPerch* perches, int perchCount);

    // Trees within radius of (x, z). Perch height is mid-canopy.
    // Only tiles that overlap the disc are read.
    void gatherCrowPerches(const Terrain::TerrainGrid& grid, float x, float z, float radius, std::vector<CrowPerch>& out);

    // Living hunters and the player. Wolves are not threats.
    void gatherCrowThreats(World& world, std::vector<Math::Vector3f>& out);

    // Moves every living crow and plays the alarm when audio is set. Dead crows fall to the ground.
    // pins is required to change the pose mesh. A null pin table leaves the current mesh in place.
    void simulateCrows(World& world, const Terrain::TerrainGrid* terrain, CrowModels* models, AssetManager* assets, AssetPinTable* pins, Audio::AudioSystem* audio, float dt);

    // No persistent id and no editor object. A failed model upload returns a null entity.
    // The pose mesh is pinned on the entity so destroy/unpin matches a normal model.
    Entity spawnCrow(World& world, Renderer& renderer, AssetManager& assets, AssetPinTable& pins, CrowModels& models,
        const Terrain::TerrainGrid* terrain, Entity spawner, const SpawnerRequest& req, float groundY, float homeX, float homeZ);

} // namespace Dark
