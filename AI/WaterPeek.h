#pragma once

#include "Math/Vector3f.h"

namespace Dark::AI
{

    // Ground height at x, z. Water lies wherever the ground is below the level's water surface.
    using GroundHeightFn = float (*)(void* user, float x, float z);

    // True when water is within a few metres of at. outDir is the flat unit direction toward it.
    bool findWaterBeside(GroundHeightFn heightAt, void* user, const Math::Vector3f& at, float waterLevel, Math::Vector3f& outDir);

    // Close, shallow, and inside the forward cone. Walls, doors, cover, and bridges are tested by the hunter tick.
    bool peerSpotsSwimmer(const Math::Vector3f& guardPos, const Math::Vector3f& forward, const Math::Vector3f& playerPos, float playerDepth);

} // namespace Dark::AI
