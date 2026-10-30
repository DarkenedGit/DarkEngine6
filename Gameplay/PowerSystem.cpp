#include "Gameplay/PowerSystem.h"

#include "Gameplay/PowerComponents.h"
#include "Gameplay/Quest.h"

#include "AI/AiComponents.h"
#include "AI/Awareness.h"
#include "AI/Walkability.h"
#include "Character/HealthComponent.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/PowerCheck.h"
#include "Play/Interaction.h"

#include <cmath>

namespace Dark
{
    namespace
    {
        using namespace Math;

        bool openAt(const AI::Walkability* walk, float x, float z)
        {
            return !walk || !walk->valid() || walk->walkableWorld(x, z);
        }

        Vector3f boxPos(World& world, Entity e, const PowerBoxComponent& box)
        {
            if (const TransformComponent* xf = world.get<TransformComponent>(e))
                return xf->position;
            return box.pos;
        }

    } // namespace

    void setPowerCircuit(World& world, Entity boxEntity, bool on, bool alertGuards, const AI::Walkability* walk, float time, QuestTracker* quest, PowerClickFn onClick, void* user)
    {
        PowerBoxComponent* box = boxEntity.valid() ? world.get<PowerBoxComponent>(boxEntity) : nullptr;
        if (!box)
            return;
        const PowerTuning& tune = powerTuning();
        const Vector3f     pos  = boxPos(world, boxEntity, *box);
        box->on     = on;
        box->fixFor = 0.0f;
        if (on && box->fixer.valid())
        {
            box->checker   = box->fixer;
            box->checkIdx  = 0;
            box->checkFor  = 0.0f;
            box->checkTime = 0.0f;
            const float a0 = std::fmod(time * 1.37f, 6.2831853f);
            for (int k = 0; k < PowerBoxComponent::kCheckCount; ++k)
            {
                box->checks[k] = box->service;
                for (int t = 0; t < 12; ++t)
                {
                    const float a = a0 + static_cast<float>(k) * 2.09f + static_cast<float>(t) * 0.55f;
                    const float r = 3.0f + std::fmod(time * 0.73f + static_cast<float>(k) * 1.9f + static_cast<float>(t) * 1.3f, 4.0f);
                    const float x = pos.x + std::sin(a) * r;
                    const float z = pos.z + std::cos(a) * r;
                    if (!openAt(walk, x, z))
                        continue;
                    box->checks[k] = Vector3f{ x, pos.y, z };
                    break;
                }
            }
            box->fixer = {};
        }
        world.each<CircuitMemberComponent>([&](Entity light, CircuitMemberComponent& member) {
            if (member.circuit != box->circuit)
                return;
            if (LocalLightComponent* l = world.alive(light) ? world.get<LocalLightComponent>(light) : nullptr)
                l->enabled = on;
        });
        if (onClick)
            onClick(user);
        DE_LOG_INFO("Power: circuit {} {}", box->circuit, on ? "on" : "off");
        if (!on && alertGuards && quest)
            quest->event("power_off");
        if (on || !alertGuards)
            return;
        float bestD = tune.alertRadius * tune.alertRadius;
        world.each<AiAgentComponent>([&](Entity e, AiAgentComponent& ai) {
            const TransformComponent* gxf = world.get<TransformComponent>(e);
            const HealthComponent*    hp  = world.get<HealthComponent>(e);
            if (!gxf || !hp || !hp->health.alive() || !isInteractGuard(world, e) || ai.aware.alertness == AI::Alertness::Alert)
                return;
            const float d = Vector3f{ gxf->position.x - pos.x, 0.0f, gxf->position.z - pos.z }.MagnitudeSqrd();
            if (d > bestD)
                return;
            bestD      = d;
            box->fixer = e;
        });
        if (!box->fixer.valid())
        {
            DE_LOG_INFO("Power: no guard near circuit {}", box->circuit);
            return;
        }
        if (box->checker == box->fixer)
            box->checker = {};
        const TransformComponent* gxf = world.get<TransformComponent>(box->fixer);
        if (!gxf)
        {
            box->fixer = {};
            return;
        }
        Vector3f side{ gxf->position.x - pos.x, 0.0f, gxf->position.z - pos.z };
        side             = side.MagnitudeSqrd() > 1.0e-4f ? side * (1.0f / std::sqrt(side.MagnitudeSqrd())) : Vector3f::Z_AXIS;
        const float base = std::atan2(side.x, side.z);
        box->service     = Vector3f{ pos.x + side.x * 1.6f, pos.y, pos.z + side.z * 1.6f };
        bool found       = false;
        for (float r = 1.4f; r < 3.5f && !found; r += 0.5f)
        {
            for (int t = 0; t < 8 && !found; ++t)
            {
                const float a = base + static_cast<float>((t + 1) / 2) * 0.785f * ((t & 1) ? 1.0f : -1.0f);
                const float x = pos.x + std::sin(a) * r;
                const float z = pos.z + std::cos(a) * r;
                if (!openAt(walk, x, z))
                    continue;
                box->service = Vector3f{ x, pos.y, z };
                found        = true;
            }
        }
        DE_LOG_INFO("Power: guard {} going to fix circuit {}", box->fixer.id(), box->circuit);
    }

    void tickPower(World& world, float dt, const AI::Walkability* walk, float time, QuestTracker* quest, PowerClickFn onClick, void* user)
    {
        const PowerTuning& tune = powerTuning();
        world.each<PowerBoxComponent>([&](Entity e, PowerBoxComponent& box) {
            const Vector3f pos = boxPos(world, e, box);
            if (box.checker.valid())
            {
                AiAgentComponent*         ai  = world.alive(box.checker) ? world.get<AiAgentComponent>(box.checker) : nullptr;
                const TransformComponent* gxf = ai ? world.get<TransformComponent>(box.checker) : nullptr;
                const HealthComponent*    hp  = ai ? world.get<HealthComponent>(box.checker) : nullptr;
                if (!ai || !gxf || !hp || !hp->health.alive() || powerCheckerShouldClear(ai->aware.alertness, ai->aware.heardOnly))
                    box.checker = {};
                else if (box.checkIdx < 0 || box.checkIdx >= PowerBoxComponent::kCheckCount)
                    box.checker = {};
                else
                {
                    AI::AwarenessState& aw = ai->aware;
                    const Vector3f      at = box.checks[box.checkIdx];
                    AI::alertToInvestigate(aw, at);
                    aw.alertness = AI::Alertness::Investigating;
                    aw.stateTime = 0.0f;
                    box.checkTime += dt;
                    if (Vector3f{ gxf->position.x - at.x, 0.0f, gxf->position.z - at.z }.MagnitudeSqrd() <= tune.serviceRange * tune.serviceRange)
                        box.checkFor += dt;
                    if (box.checkFor >= tune.checkDwell || box.checkTime >= tune.checkLimit)
                    {
                        box.checkFor  = 0.0f;
                        box.checkTime = 0.0f;
                        if (++box.checkIdx >= PowerBoxComponent::kCheckCount)
                        {
                            aw.alertness = AI::Alertness::Calm;
                            aw.meter     = 0.0f;
                            box.checker  = {};
                            DE_LOG_INFO("Power: guard done checking circuit {}", box.circuit);
                        }
                    }
                }
            }
            if (box.on)
                return;
            AiAgentComponent*         ai  = box.fixer.valid() && world.alive(box.fixer) ? world.get<AiAgentComponent>(box.fixer) : nullptr;
            const TransformComponent* gxf = ai ? world.get<TransformComponent>(box.fixer) : nullptr;
            const HealthComponent*    hp  = ai ? world.get<HealthComponent>(box.fixer) : nullptr;
            if (!ai || !gxf || !hp || !hp->health.alive())
            {
                box.fixer  = {};
                box.fixFor = 0.0f;
                return;
            }
            AI::AwarenessState& aw = ai->aware;
            const bool onTask = aw.alertness == AI::Alertness::Calm
                             || ((aw.alertness == AI::Alertness::Suspicious || aw.alertness == AI::Alertness::Investigating) && aw.heardOnly);
            if (onTask)
            {
                AI::alertToInvestigate(aw, box.service);
                aw.alertness = AI::Alertness::Investigating;
                aw.stateTime = 0.0f;
            }
            const float toBox  = Vector3f{ gxf->position.x - pos.x, 0.0f, gxf->position.z - pos.z }.MagnitudeSqrd();
            const float toSpot = Vector3f{ gxf->position.x - box.service.x, 0.0f, gxf->position.z - box.service.z }.MagnitudeSqrd();
            const bool  fixing = aw.alertness != AI::Alertness::Alert && (toBox <= tune.fixRange * tune.fixRange || toSpot <= tune.serviceRange * tune.serviceRange);
            box.fixFor         = fixing ? box.fixFor + dt : 0.0f;
            if (box.fixFor >= tune.fixSeconds)
            {
                DE_LOG_INFO("Power: guard {} fixed circuit {}", box.fixer.id(), box.circuit);
                setPowerCircuit(world, e, true, false, walk, time, quest, onClick, user);
            }
        });
    }

    void applyDisabledCircuits(World& world)
    {
        world.each<PowerBoxComponent>([&](Entity, PowerBoxComponent& box) {
            if (box.on)
                return;
            world.each<CircuitMemberComponent>([&](Entity light, CircuitMemberComponent& member) {
                if (member.circuit != box.circuit)
                    return;
                if (LocalLightComponent* l = world.alive(light) ? world.get<LocalLightComponent>(light) : nullptr)
                    l->enabled = false;
            });
        });
    }

    Entity nearestPowerBox(World& world, const Math::Vector3f& pos, float range)
    {
        Entity best{};
        float  bestD = range * range;
        world.each<PowerBoxComponent>([&](Entity e, PowerBoxComponent& box) {
            const Vector3f at = boxPos(world, e, box);
            const float    d  = Vector3f{ at.x - pos.x, 0.0f, at.z - pos.z }.MagnitudeSqrd();
            if (d > bestD)
                return;
            bestD = d;
            best  = e;
        });
        return best;
    }

} // namespace Dark
