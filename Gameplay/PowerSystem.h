#pragma once

#include "ECS/Entity.h"
#include "Math/Vector3f.h"

namespace Dark
{

    class QuestTracker;
    class World;

    namespace AI
    {
        class Walkability;
    }

    using PowerClickFn = void (*)(void* user);

    // alertGuards searches for a fixer only when turning the circuit off.
    void setPowerCircuit(World& world, Entity box, bool on, bool alertGuards, const AI::Walkability* walk, float time, QuestTracker* quest, PowerClickFn onClick, void* user);
    void tickPower(World& world, float dt, const AI::Walkability* walk, float time, QuestTracker* quest, PowerClickFn onClick, void* user);

    // After a load, authored lights are back on. A saved breaker that is off turns them off again.
    void applyDisabledCircuits(World& world);
    Entity nearestPowerBox(World& world, const Math::Vector3f& pos, float range);

} // namespace Dark
