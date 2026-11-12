#pragma once

#include "AI/AiSystem.h"
#include "Character/HitReaction.h"
#include "Character/PlayerMotor.h"
#include "Scene/SceneTypes.h"
#include "Terrain/HeightMap.h"
#include "Terrain/TerrainGrid.h"

#include <vector>

namespace Dark
{

    struct HunterGroundView
    {
        const Terrain::HeightMap* height = nullptr;
        const Terrain::SplatMap*  splat  = nullptr;
    };

    // A null grid leaves both maps empty so hunters keep full speed.
    inline HunterGroundView hunterGroundView(const Terrain::TerrainGrid* grid)
    {
        const Terrain::GridSurfaceMaps maps = Terrain::gridSurfaceMaps(grid);
        HunterGroundView view;
        view.height = maps.height;
        view.splat  = maps.splat;
        return view;
    }

    inline void bindHunterGround(AiSystem& ai, const Terrain::TerrainGround* ground, const Terrain::TerrainGrid* grid)
    {
        const HunterGroundView view = hunterGroundView(grid);
        ai.setGroundSurface(ground, view.height, view.splat);
    }

    // Null motor counts as standing still. Crouch suppresses sprint.
    inline void applyPreyFromMotor(AiSystem& ai, const PlayerStealthSettings& stealth, const PlayerMotor* motor, bool sprintDown)
    {
        const float speed     = motor ? Math::Vector3f{ motor->velocity().x, 0.0f, motor->velocity().z }.Magnitude() : 0.0f;
        const bool  crouched  = motor && motor->state() == PlayerMoveState::Crouch;
        const bool  moving    = speed >= stealth.stillSpeed;
        const bool  sprinting = moving && !crouched && sprintDown;
        ai.setPreySense(preySenseFor(stealth, crouched, moving, sprinting));
    }

    // Built-in prey radii, then any hear overrides on the scene.
    inline void applyScenePrey(PlayerStealthSettings& stealth, const StealthSceneDesc& scene)
    {
        stealth = PlayerStealthSettings{};
        if (scene.hasHearStand)
            stealth.hearStand = scene.hearStand;
        if (scene.hasHearWalk)
            stealth.hearWalk = scene.hearWalk;
        if (scene.hasHearSprint)
            stealth.hearSprint = scene.hearSprint;
        if (scene.hasHearCrouchWalk)
            stealth.hearCrouchWalk = scene.hearCrouchWalk;
        if (scene.hasHearCrouchStill)
            stealth.hearCrouchStill = scene.hearCrouchStill;
    }

    // Guard tuning starts from content/ai/awareness.json, then scene settings overlay authored keys.
    inline void applySceneAwareness(AiSystem& ai, const StealthSceneDesc& scene)
    {
        AI::AwarenessSettings aware{};
        AI::loadAwarenessSettings(aware);
        if (scene.hasSettings)
            AI::applyAwarenessMask(aware, scene.settings, scene.settingsMask);
        ai.setAwarenessSettings(aware);
        ai.setAwarenessEnabled(scene.awareness);
    }

    inline void applySceneStealth(PlayerStealthSettings& stealth, AiSystem& ai, const StealthSceneDesc& scene)
    {
        applyScenePrey(stealth, scene);
        applySceneAwareness(ai, scene);
    }

    // Sandbox live values. PathChase::init and both apps set this so a later write cannot fight it.
    inline HitReactionSettings defaultHunterHitReaction()
    {
        HitReactionSettings settings;
        settings.stunSeconds       = 0.45f;
        settings.knockbackDistance = 0.6f;
        settings.knockbackSeconds  = 0.18f;
        settings.horizontalOnly    = true;
        return settings;
    }

    // Sphere count stays zero for cube-only bakes. Empty lists store null.
    inline void setHunterBakeObstacles(AI::WalkabilityDesc& desc, const Math::AABox3f* cubes, int cubeCount, const Math::Sphere3f* spheres, int sphereCount)
    {
        desc.cubes       = (cubes && cubeCount > 0) ? cubes : nullptr;
        desc.cubeCount   = desc.cubes ? cubeCount : 0;
        desc.spheres     = (spheres && sphereCount > 0) ? spheres : nullptr;
        desc.sphereCount = desc.spheres ? sphereCount : 0;
    }

    // Height map, water, and the obstacle lists the caller already gathered. Does not add an origin cube.
    inline bool bakeHunterWalk(AiSystem& ai, const Terrain::HeightMap* height, float waterLevel, float agentRadius, const std::vector<Math::AABox3f>& cubes,
                               const std::vector<Math::Sphere3f>& spheres)
    {
        if (!height || !height->valid())
            return false;
        AI::WalkabilityDesc desc;
        desc.heightMap   = height;
        desc.waterLevel  = waterLevel;
        desc.agentRadius = agentRadius;
        setHunterBakeObstacles(desc, cubes.empty() ? nullptr : cubes.data(), static_cast<int>(cubes.size()), spheres.empty() ? nullptr : spheres.data(),
                               static_cast<int>(spheres.size()));
        return ai.bake(desc);
    }

} // namespace Dark
