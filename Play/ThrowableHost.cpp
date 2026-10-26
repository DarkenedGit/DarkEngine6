#include "Play/ThrowableHost.h"

#include "AI/AiSystem.h"
#include "Assets/AssetManager.h"
#include "Audio/AudioSystem.h"
#include "Character/ItemView.h"
#include "Character/ThrowableView.h"
#include "Core/AssetPinTable.h"
#include "Core/Log.h"
#include "ECS/World.h"
#include "Gameplay/ThrowableBelt.h"
#include "Gameplay/ThrowableCatalog.h"
#include "Physics/PhysicsWorld.h"
#include "Play/WeaponTargets.h"
#include "Render/Renderer.h"
#include "Weapons/Weapon.h"

#include <vector>

namespace Dark
{

    void tickThrowableFlightHost(ThrowableFlights& flights, float dt, const ThrowableFlightHost& host)
    {
        if (!host.world || !host.pins || !host.assets || !host.renderer || !host.physics || !host.audio || !host.ai)
            return;

        if (flights.liveCount() == 0 && flights.landings().empty())
        {
            syncThrowableFlightVisuals(*host.world, *host.pins, *host.assets, *host.renderer, flights);
            return;
        }

        if (flights.liveCount() > 0)
        {
            WeaponTargetList bodies;
            bodies.gather(*host.world, Entity{});
            std::vector<ThrowableBody> list;
            list.reserve(bodies.targets().size());
            for (const WeaponTarget& target : bodies.targets())
            {
                ThrowableBody body;
                body.entity      = target.entity;
                body.center      = target.center;
                body.halfExtents = target.halfExtents;
                body.alive       = target.alive;
                list.push_back(body);
            }
            ThrowableWorld query = host.solids;
            query.bodies         = list.empty() ? nullptr : list.data();
            query.bodyCount      = static_cast<int>(list.size());
            flights.tick(dt, query);
        }

        for (const ThrowableLanding& landing : flights.landings())
        {
            const ItemDef* def = throwablePickupDef(landing.itemId);
            if (!def)
                DE_LOG_WARN("Throwable: could not spawn '{}'", landing.itemId);
            else if (!spawnItemPickup(*host.world, *host.pins, *host.assets, *host.renderer, *host.physics, ItemStack{ def, 1 }, landing.position, Math::Vector3f::ZERO).valid())
                DE_LOG_WARN("Throwable: could not spawn '{}'", landing.itemId);
            if (!landing.impactClip.empty())
            {
                if (const auto clip = host.audio->loadWav(*host.assets, landing.impactClip.c_str()))
                    host.audio->play3D(clip, landing.position, 1.0f);
            }
            if (!landing.expired)
            {
                float callDistance = 0.0f;
                if (const ThrowableDef* item = throwableCatalog().find(landing.itemId))
                    callDistance = item->callDistance;
                host.ai->postThrowableLanding(*host.world, landing, callDistance, host.solids.cubes, host.solids.cubeCount);
            }
        }
        flights.clearLandings();
        syncThrowableFlightVisuals(*host.world, *host.pins, *host.assets, *host.renderer, flights);
    }

    bool throwFromLoadout(ThrowableFlights& flights, WeaponLoadout& weapons, ThrowableBelt& belt, const WeaponFireRequest& aim, Entity thrower, bool charged, std::string* thrownId)
    {
        const ThrowableDef* def = throwableCatalog().find(belt.hand());
        if (!def)
            return false;
        ThrowableLaunch launch;
        launch.itemId        = def->id;
        launch.impactClip    = def->impactSound;
        launch.weight        = def->weight;
        launch.throwDistance = def->throwDistance;
        launch.origin        = aim.origin;
        launch.direction     = aim.direction;
        launch.charged       = charged;
        launch.thrower       = thrower;
        if (!beginThrowableThrow(flights, weapons, belt, launch))
            return false;
        if (thrownId)
            *thrownId = launch.itemId;
        return true;
    }

} // namespace Dark
