#pragma once

namespace Dark
{

    class World;

    // Bit-identical extract of the Editor particle sim loop: ensure + setTransform + update.
    void tickParticleEmitters(World& world, float dt);

} // namespace Dark
