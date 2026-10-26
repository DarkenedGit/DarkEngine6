#include "Play/Carry.h"

#include "AI/AiComponents.h"
#include "Character/HealthComponent.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"

namespace Dark
{
    using namespace Math;

    namespace
    {

        Vector3f flatLook(const Vector3f& look)
        {
            Vector3f forward{ look.x, 0.0f, look.z };
            if (forward.MagnitudeSqrd() < 1.0e-6f)
                forward = Vector3f::Z_AXIS;
            else
                forward.Normalize();
            return forward;
        }

    } // namespace

    Entity carriedBody(World& world)
    {
        Entity found{};
        world.each<CarriedByComponent>([&](Entity e, CarriedByComponent&) { found = e; });
        return found;
    }

    void gatherConcealVolumes(World& world, std::vector<AABox3f>& out)
    {
        out.clear();
        world.each<ConcealVolumeComponent>([&](Entity, ConcealVolumeComponent& vol) { out.push_back(vol.box); });
    }

    void syncCarriedAgents(World& world)
    {
        world.each<CarriedByComponent>([&](Entity e, CarriedByComponent&) {
            if (AiAgentComponent* ai = world.get<AiAgentComponent>(e))
                ai->carried = true;
        });
    }

    void dropCarriedBody(CarryView& view)
    {
        if (!view.world)
            return;
        const Entity body = carriedBody(*view.world);
        if (!body.valid())
            return;
        TransformComponent* bxf = view.world->alive(body) ? view.world->get<TransformComponent>(body) : nullptr;
        AiAgentComponent*   ai  = bxf ? view.world->get<AiAgentComponent>(body) : nullptr;
        const CarriedByComponent* carried = view.world->get<CarriedByComponent>(body);
        const TransformComponent* pxf = (view.carrier.valid()) ? view.world->get<TransformComponent>(view.carrier) : nullptr;
        if (bxf && ai && pxf && view.heightAt)
        {
            const Vector3f forward = flatLook(view.look);
            bxf->position          = pxf->position + forward * 0.9f;
            bxf->position.y        = view.heightAt(view.heightUser, bxf->position.x, bxf->position.z) + (carried ? carried->floor : 0.0f);
            ai->carried            = false;
        }
        else if (ai)
            ai->carried = false;
        if (bxf && view.conceal)
        {
            for (int i = 0; i < view.concealCount; ++i)
            {
                const AABox3f& patch = view.conceal[i];
                if (bxf->position.x > patch.Min.x && bxf->position.x < patch.Max.x && bxf->position.z > patch.Min.z && bxf->position.z < patch.Max.z)
                {
                    if (view.onHidden)
                        view.onHidden(view.user);
                    break;
                }
            }
        }
        view.world->remove<CarriedByComponent>(body);
        DE_LOG_INFO("Player: dropped body");
    }

    bool takeCarriedBody(World& world, Entity carrier, Entity body, float (*heightAt)(void*, float, float), void* heightUser)
    {
        const TransformComponent* bxf = world.get<TransformComponent>(body);
        AiAgentComponent*         ai  = world.get<AiAgentComponent>(body);
        if (!bxf || !ai || !heightAt)
            return false;
        CarriedByComponent carried{};
        carried.carrier = carrier;
        carried.floor   = bxf->position.y - heightAt(heightUser, bxf->position.x, bxf->position.z);
        ai->carried     = true;
        if (CarriedByComponent* existing = world.get<CarriedByComponent>(body))
            *existing = carried;
        else
            world.emplace<CarriedByComponent>(body, carried);
        return true;
    }

    void tickCarriedBody(CarryView& view)
    {
        if (!view.world)
            return;
        const Entity body = carriedBody(*view.world);
        if (!body.valid())
            return;
        const TransformComponent* pxf = view.carrier.valid() ? view.world->get<TransformComponent>(view.carrier) : nullptr;
        const HealthComponent*    php = view.carrier.valid() ? view.world->get<HealthComponent>(view.carrier) : nullptr;
        TransformComponent*       bxf = view.world->alive(body) ? view.world->get<TransformComponent>(body) : nullptr;
        if (!bxf || !pxf || !php || !php->health.alive())
        {
            dropCarriedBody(view);
            return;
        }
        const Vector3f forward = flatLook(view.look);
        bxf->position          = pxf->position - forward * 0.35f + Vector3f{ 0.0f, 0.55f, 0.0f };
    }

} // namespace Dark
