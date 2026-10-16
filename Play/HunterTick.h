#pragma once

#include "AI/AiSystem.h"
#include "Character/PlayerMotor.h"
#include "Terrain/TerrainGrid.h"

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
        HunterGroundView view;
        if (!grid)
            return view;
        view.height = grid->editableWorking();
        if (!view.height || !view.height->valid())
            view.height = &grid->coarse();
        const Terrain::SplatMap* splat = grid->editableWorkingSplat();
        view.splat = splat && splat->valid() ? splat : nullptr;
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

    // Sphere count stays zero for cube-only bakes. Empty lists store null.
    inline void setHunterBakeObstacles(AI::WalkabilityDesc& desc, const Math::AABox3f* cubes, int cubeCount, const Math::Sphere3f* spheres, int sphereCount)
    {
        desc.cubes       = (cubes && cubeCount > 0) ? cubes : nullptr;
        desc.cubeCount   = desc.cubes ? cubeCount : 0;
        desc.spheres     = (spheres && sphereCount > 0) ? spheres : nullptr;
        desc.sphereCount = desc.spheres ? sphereCount : 0;
    }

} // namespace Dark
