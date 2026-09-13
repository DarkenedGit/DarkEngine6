#include "Gameplay/HealthPack.h"

#include "Character/HealthComponent.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/MathDefines.h"
#include "Math/Quaternion.h"

#include <cmath>

namespace Dark
{
    using namespace Math;

    Matrix4f healthPackWorldMatrix(const Vector3f& pos, float spin, float bob)
    {
        const Quaternion rot = Quaternion::FromAxisAngle(Vector3f::Y_AXIS, spin);
        const float      y   = pos.y + 0.08f * std::sinf(bob * 2.6f);
        return Matrix4f::ScaleMatrixXYZ(0.9f, 0.9f, 0.9f) * rot.ToMatrix4() * Matrix4f::TranslationMatrix(pos.x, y, pos.z);
    }

    int tickHealthPacks(World& world, Entity player, float dt)
    {
        Health* health = nullptr;
        Vector3f playerPos{};
        if (player.valid())
        {
            if (HealthComponent* hc = world.get<HealthComponent>(player))
                health = &hc->health;
            if (const TransformComponent* xf = world.get<TransformComponent>(player))
                playerPos = xf->position;
        }

        const bool canPickup = health && health->alive() && health->hp() < health->maxHp();
        const float r2       = HealthPackComponent::kPickupR * HealthPackComponent::kPickupR;
        int         taken    = 0;

        world.each<HealthPackComponent>([&](Entity e, HealthPackComponent& p) {
            p.spin += 1.85f * dt;
            if (p.spin > TwoPi)
                p.spin -= TwoPi;
            p.bob += dt;

            if (!p.active)
            {
                p.respawnIn -= dt;
                if (p.respawnIn <= 0.0f)
                    p.active = true;
            }
            else if (canPickup)
            {
                const float dx = playerPos.x - p.restPos.x;
                const float dy = playerPos.y - p.restPos.y;
                const float dz = playerPos.z - p.restPos.z;
                if (dx * dx + dy * dy + dz * dz <= r2)
                {
                    health->heal(HealthPackComponent::kHeal);
                    p.active    = false;
                    p.respawnIn = HealthPackComponent::kRespawn;
                    ++taken;
                    DE_LOG_INFO("Player: health pack +{:.0f} ({:.0f}/{:.0f})", HealthPackComponent::kHeal, health->hp(), health->maxHp());
                }
            }

            if (TransformComponent* xf = world.get<TransformComponent>(e))
            {
                const Matrix4f m = healthPackWorldMatrix(p.restPos, p.spin, p.bob);
                xf->position     = m.GetTranslation();
                xf->rotation     = Quaternion::FromAxisAngle(Vector3f::Y_AXIS, p.spin);
                xf->scale        = Vector3f{ 0.9f, 0.9f, 0.9f };
            }
            if (MeshComponent* mc = world.get<MeshComponent>(e))
                mc->primitive = p.active ? PrimitiveMesh::Cross : PrimitiveMesh::None;
        });

        return taken;
    }

} // namespace Dark
