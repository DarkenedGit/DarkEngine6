#include "Play/PhysicsGround.h"

#include "Core/Log.h"
#include "Physics/PhysicsWorld.h"
#include "Terrain/HeightMap.h"

namespace Dark
{

    Physics::PhysicsBodyId bindHeightFieldGround(Physics::PhysicsWorld& physics, const Terrain::HeightMap& coarse, const char* logTag)
    {
        const char* tag = (logTag && logTag[0]) ? logTag : "Play";
        if (!physics.valid() || !coarse.valid())
            return Physics::kNullPhysicsBody;

        Physics::PhysicsHeightFieldDesc hf;
        hf.heights     = coarse.samples();
        hf.countX      = coarse.width();
        hf.countZ      = coarse.height();
        hf.cellSize    = coarse.cellSize();
        hf.heightScale = coarse.heightScale();
        hf.origin = coarse.origin();

        const Physics::PhysicsBodyId body = physics.createHeightField(hf);
        if (body != Physics::kNullPhysicsBody)
            DE_LOG_INFO(LogCategory::Collision, "{}: physics ground = terrain height field {}x{} (cell {:.2f} m)", tag, hf.countX, hf.countZ, hf.cellSize);
        return body;
    }

    float raisedGroundY(const Physics::PhysicsWorld& physics, Physics::PhysicsBodyId ignore, Physics::PhysicsBodyId groundBody, float terrainY, float x, float z, float probeY)
    {
        if (!physics.valid())
            return terrainY;

        Physics::PhysicsWorld::GroundProbe probe;
        probe.from       = Math::Vector3f(x, probeY, z);
        probe.radius     = 0.3f;
        probe.maxDrop    = (probeY - terrainY) + 1.0f;
        probe.ignore     = ignore;
        probe.ignore2    = groundBody;
        probe.staticOnly = true;

        float surfaceY = 0.0f;
        if (probe.maxDrop > 0.0f && physics.probeGround(probe, surfaceY) && surfaceY > terrainY)
            return surfaceY;
        return terrainY;
    }

} // namespace Dark
