#pragma once

#include "Assets/AssetHandle.h"
#include "Audio/SoundClip.h"
#include "ECS/Entity.h"
#include "Gameplay/Upgrades.h"
#include "Math/AABox3f.h"
#include "Math/Vector3f.h"
#include "Play/Interaction.h"
#include "Scene/Building.h"

#include <deque>
#include <string>
#include <vector>

namespace Dark
{

    class AiSystem;
    class AssetManager;
    class AssetPinTable;
    class Renderer;
    class World;

    namespace Audio
    {
        class AudioSystem;
    }

    // Filled by PlaySession from the app host. Pointers are not owned.
    struct FacilityHost
    {
        World*              world    = nullptr;
        AiSystem*           ai       = nullptr;
        Audio::AudioSystem* audio    = nullptr;
        AssetManager*       assets   = nullptr;
        Entity              player{};
        Entity              listener{};
        const Math::AABox3f* cubes     = nullptr;
        int                  cubeCount = 0;
        float (*heightAt)(void* user, float x, float z) = nullptr;
        void* heightUser                                 = nullptr;
        bool  submerged                                  = false;
    };

    // Doors, hiding, cameras, the alarm, and session upgrades. Owned by PlaySession.
    struct FacilityState
    {
        Entity  hiddenIn{};
        float   hideFound = 0.0f;
        Entity  hideWitness{};
        std::string doorMsg;
        float       doorMsgTime = 0.0f;
        float       alarmTime   = 0.0f;
        float       alarmBeep   = 0.0f;
        AssetRef<Audio::SoundClip> alarmTone{};

        std::vector<UpgradeDef> defs;
        std::vector<std::string> owned;
        UpgradeEffects           fx{};
        Entity                   upgradeBody{};
        float                    baseBreath = 0.0f;
        float                    baseWalk   = 0.0f;
        float                    baseSprint = 0.0f;
        float                    baseMaxHp  = 0.0f;
        bool                     upgradesDirty = true;

        // Hunter and camera solids. Points at the host cubes while every door is open enough.
        const Math::AABox3f*     blocks     = nullptr;
        int                      blockCount = 0;
        std::vector<EntityID>    shutIds;
        std::vector<Math::AABox3f> solids;
        const Math::AABox3f*     solidsSource      = nullptr;
        int                      solidsSourceCount = -1;

        // Loot labels. LootComponent::label points into these strings.
        std::deque<std::string> labels;
    };

    struct BuildingFixtureSpawn
    {
        World*               world    = nullptr;
        Renderer*            renderer = nullptr;
        AssetManager*        assets   = nullptr;
        AssetPinTable*       pins     = nullptr;
        AiSystem*            ai       = nullptr;
        std::deque<std::string>* labels = nullptr;
    };

    void tickFacility(FacilityState& state, const FacilityHost& host, float dt);
    // Hide and door use. Returns true when the press was consumed.
    bool handleFacilityUse(FacilityState& state, const FacilityHost& host, const PlayInteractPick& pick);
    // Restores motor and health bases, then drops owned upgrades, the alarm, and the hide.
    void clearFacilitySession(FacilityState& state, World* world, Entity player);
    void refreshFacilityUpgrades(FacilityState& state, const FacilityHost& host);

    void spawnBuildingFixtures(const BuildingFixtureSpawn& spawn, const std::vector<BuildingContainer>& containers,
                               const std::vector<BuildingHideSpot>& hides, const std::vector<BuildingDoor>& doors,
                               const std::vector<BuildingCamera>& cameras);

} // namespace Dark
