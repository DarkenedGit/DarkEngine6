#pragma once

#include "ECS/Entity.h"
#include "Math/AABox3f.h"
#include "Math/Vector3f.h"

namespace Dark
{

    class AiSystem;
    class World;

    namespace AI
    {

        // Sun at the chest, darkened when a cube blocks the ray, plus nearby point and spot lamps.
        // ignoreLight skips one lamp (the muzzle flash).
        float sampleLightLevel(World& world, const Math::Vector3f& chest, float sunElevation, const Math::Vector3f& sunDir, const Math::AABox3f* cubes, int cubeCount, Entity ignoreLight);

        // Writes PlayerStealthComponent light, exposure, and stride, and posts footstep noise.
        // A player with no stealth component is left alone. Returns false in that case.
        bool tickPlayerExposure(World& world, Entity player, AiSystem& ai, float dt, bool sprintDown, bool camouflage, float sunElevation,
                                const Math::Vector3f& sunDir, const Math::AABox3f* cubes, int cubeCount, Entity ignoreLight);

    } // namespace AI

} // namespace Dark
