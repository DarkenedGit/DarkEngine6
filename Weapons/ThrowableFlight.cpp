#include "Weapons/ThrowableFlight.h"

#include "Collision/Collision.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/ThrowableBelt.h"
#include "Math/MathDefines.h"
#include "Weapons/WeaponLoadout.h"

#include <cmath>
#include <utility>

namespace Dark
{

    namespace
    {

        bool finiteVec(const Math::Vector3f& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        bool launchAccepted(const ThrowableLaunch& launch)
        {
            if (launch.itemId.empty() || !finiteVec(launch.origin) || !finiteVec(launch.direction))
                return false;
            if (!(launch.weight > 0.0f) || !std::isfinite(launch.weight))
                return false;
            if (!(launch.throwDistance > 0.0f) || !std::isfinite(launch.throwDistance))
                return false;
            if (launch.direction.MagnitudeSqrd() < 1.0e-8f)
                return false;
            if (!(throwableChargedSpeed(launch.throwDistance) > 0.0f))
                return false;
            if (launch.maxLife > 0.0f && !std::isfinite(launch.maxLife))
                return false;
            return true;
        }

        Math::Vector3f unitOrUp(const Math::Vector3f& normal)
        {
            if (normal.MagnitudeSqrd() < 1.0e-8f)
                return Math::Vector3f{ 0.0f, 1.0f, 0.0f };
            Math::Vector3f n = normal;
            n.Normalize();
            return n;
        }

        bool overlapsBody(const Math::Vector3f& position, const ThrowableBody& body)
        {
            const Math::Sphere3f ball{ position, kThrowableRadius };
            const Math::AABox3f  box = Math::AABox3f::FromCenterExtents(body.center, body.halfExtents);
            return Collision::Intersects(ball, box);
        }

        // y(t) = y0 + vy t - 0.5 g t^2 meets the ground. A launch that starts on the ground and rises is not a hit.
        bool groundCrossTime(float y, float vy, float gravity, float groundY, float dt, float& outT)
        {
            if (!(gravity > 0.0f) || !(dt > 0.0f))
                return false;
            const float clearance = y - kThrowableRadius - groundY;
            if (clearance <= 1.0e-4f && vy <= 0.0f)
            {
                outT = 0.0f;
                return true;
            }
            const float A    = 0.5f * gravity;
            const float B    = -vy;
            const float C    = -clearance;
            const float disc = B * B - 4.0f * A * C;
            if (disc < 0.0f)
                return false;
            const float s   = std::sqrt(disc);
            const float inv = 0.5f / A;
            const float t0  = (-B - s) * inv;
            const float t1  = (-B + s) * inv;
            float       best = -1.0f;
            auto        take = [&](float t) {
                if (t <= 1.0e-4f || t > dt + 1.0e-4f)
                    return;
                if (best < 0.0f || t < best)
                    best = t;
            };
            take(t0);
            take(t1);
            if (best < 0.0f)
                return false;
            outT = best > dt ? dt : best;
            return true;
        }

        struct SweepChoice
        {
            bool           any = false;
            float          t   = 1.0f;
            Math::Vector3f point{};
            Math::Vector3f normal{ 0.0f, 1.0f, 0.0f };
            Entity         entity{};
            bool           body    = false;
            bool           solid   = false;
            bool           terrain = false;
        };

        void takeHit(SweepChoice& best, float t, const Math::Vector3f& point, const Math::Vector3f& normal, Entity entity, bool body, bool solid, bool terrain)
        {
            if (!(t >= 0.0f) || t > 1.0f)
                return;
            if (best.any && t >= best.t)
                return;
            best.any     = true;
            best.t       = t;
            best.point   = point;
            best.normal  = unitOrUp(normal);
            best.entity  = entity;
            best.body    = body;
            best.solid   = solid;
            best.terrain = terrain;
        }

    } // namespace

    float throwableChargedSpeed(float throwDistance)
    {
        if (!(throwDistance > 0.0f) || !std::isfinite(throwDistance))
            return 0.0f;
        return std::sqrt(throwDistance * kThrowableBaseGravity);
    }

    float throwableGravity(float weight)
    {
        if (!(weight > 0.0f) || !std::isfinite(weight))
            return 0.0f;
        return kThrowableBaseGravity * weight;
    }

    bool throwableChargedPitch(float throwDistance, float weight, bool higherAim, float& outPitchRad)
    {
        if (!(throwDistance > 0.0f) || !(weight > 0.0f))
            return false;
        if (!std::isfinite(throwDistance) || !std::isfinite(weight))
            return false;
        // sin(2θ) = weight when speed comes from base gravity and weight only scales gravity.
        // Above 1, no pitch reaches the authored distance.
        if (weight > 1.0f + 1.0e-4f)
            return false;
        const float s    = weight > 1.0f ? 1.0f : weight;
        const float low  = std::asin(s) * 0.5f;
        const float high = Math::HalfPi - low;
        outPitchRad      = higherAim ? high : low;
        return true;
    }

    bool ThrowableFlights::launch(const ThrowableLaunch& launch)
    {
        if (!launchAccepted(launch))
            return false;
        Math::Vector3f dir = launch.direction;
        dir.Normalize();
        const float scale = launch.charged ? 1.0f : kThrowableTapSpeedScale;
        Live        live;
        live.itemId        = launch.itemId;
        live.impactClip    = launch.impactClip;
        live.weight        = launch.weight;
        live.throwDistance = launch.throwDistance;
        live.position      = launch.origin;
        live.velocity   = dir * (throwableChargedSpeed(launch.throwDistance) * scale);
        live.gravity    = throwableGravity(launch.weight);
        live.age        = 0.0f;
        live.maxLife    = launch.maxLife > 0.0f ? launch.maxLife : kThrowableMaxLife;
        live.thrower    = launch.thrower;
        live.clearOfThrower = false;
        m_live.push_back(std::move(live));
        return true;
    }

    bool ThrowableFlights::stepLive(Live& flight, float dt, const ThrowableWorld& world)
    {
        float step = dt;
        bool  last = false;
        const float remain = flight.maxLife - flight.age;
        auto landExpired = [&]()
        {
            ThrowableLanding landing;
            landing.itemId           = flight.itemId;
            landing.impactClip       = flight.impactClip;
            landing.position         = flight.position;
            landing.expired          = true;
            landing.damage           = 0.0f;
            landing.emittedWeaponHit = false;
            m_landings.push_back(std::move(landing));
        };
        if (!(remain > 0.0f))
        {
            landExpired();
            return true;
        }
        if (step >= remain)
        {
            step = remain;
            last = true;
        }

        if (!flight.clearOfThrower)
        {
            bool inside = false;
            if (flight.thrower.valid() && world.bodies)
            {
                for (int i = 0; i < world.bodyCount; ++i)
                {
                    const ThrowableBody& body = world.bodies[i];
                    if (!body.alive || body.entity != flight.thrower)
                        continue;
                    if (overlapsBody(flight.position, body))
                        inside = true;
                }
            }
            if (!inside)
                flight.clearOfThrower = true;
        }
        const bool ignoreThrower = !flight.clearOfThrower;

        const Math::Vector3f accel{ 0.0f, -flight.gravity, 0.0f };
        const Math::Vector3f delta = flight.velocity * step + accel * (0.5f * step * step);
        const float          dist  = delta.Magnitude();
        const Math::Sphere3f ball{ flight.position, kThrowableRadius };

        SweepChoice best;
        if (world.bodies)
        {
            for (int i = 0; i < world.bodyCount; ++i)
            {
                const ThrowableBody& body = world.bodies[i];
                if (!body.alive)
                    continue;
                if (ignoreThrower && body.entity == flight.thrower)
                    continue;
                const Math::AABox3f       box = Math::AABox3f::FromCenterExtents(body.center, body.halfExtents);
                const Collision::SweptHit3D hit = Collision::SweptIntersects(ball, delta, box);
                if (!hit.hit)
                    continue;
                takeHit(best, hit.t, hit.point, hit.normal, body.entity, true, false, false);
            }
        }
        if (world.cubes)
        {
            for (int i = 0; i < world.cubeCount; ++i)
            {
                const Collision::SweptHit3D hit = Collision::SweptIntersects(ball, delta, world.cubes[i]);
                if (!hit.hit)
                    continue;
                takeHit(best, hit.t, hit.point, hit.normal, Entity{}, false, true, false);
            }
        }
        if (world.spheres)
        {
            for (int i = 0; i < world.sphereCount; ++i)
            {
                const Collision::SweptHit3D hit = Collision::SweptIntersects(ball, delta, world.spheres[i]);
                if (!hit.hit)
                    continue;
                takeHit(best, hit.t, hit.point, hit.normal, Entity{}, false, true, false);
            }
        }
        if (world.hasTerrain && world.raycastTerrain && dist > 1.0e-6f)
        {
            Math::Vector3f dir = delta * (1.0f / dist);
            const Collision::RayHit3D hit = world.raycastTerrain(world.terrainUser, Math::Ray3f{ flight.position, dir }, dist);
            if (hit.hit && hit.t > 1.0e-4f && hit.t <= dist)
                takeHit(best, hit.t / dist, hit.point, hit.normal, Entity{}, false, false, true);
        }
        if (world.hasTerrain && world.heightAt && step > 0.0f)
        {
            const float groundY = world.heightAt(world.terrainUser, flight.position.x, flight.position.z);
            float       groundT = 0.0f;
            if (groundCrossTime(flight.position.y, flight.velocity.y, flight.gravity, groundY, step, groundT))
            {
                const Math::Vector3f at = flight.position + flight.velocity * groundT + accel * (0.5f * groundT * groundT);
                const float          y  = world.heightAt(world.terrainUser, at.x, at.z);
                takeHit(best, groundT / step, Math::Vector3f{ at.x, y, at.z }, Math::Vector3f{ 0.0f, 1.0f, 0.0f }, Entity{}, false, false, true);
            }
        }

        if (best.any)
        {
            ThrowableLanding landing;
            landing.itemId           = flight.itemId;
            landing.impactClip       = flight.impactClip;
            landing.position         = best.point;
            landing.normal           = best.normal;
            landing.hitEntity        = best.entity;
            landing.hitBody          = best.body;
            landing.hitSolid         = best.solid;
            landing.hitTerrain       = best.terrain;
            landing.expired          = false;
            landing.damage           = 0.0f;
            landing.emittedWeaponHit = false;
            m_landings.push_back(std::move(landing));
            return true;
        }

        flight.position += delta;
        flight.velocity += accel * step;
        flight.age += step;
        if (!last && flight.age < flight.maxLife)
            return false;

        landExpired();
        return true;
    }

    void ThrowableFlights::tick(float dt, const ThrowableWorld& world)
    {
        if (!(dt > 0.0f) || !std::isfinite(dt))
            return;
        constexpr float kStep = 1.0f / 60.0f;
        int             i     = 0;
        while (i < static_cast<int>(m_live.size()))
        {
            float left = dt;
            bool  done = false;
            while (left > 0.0f && !done)
            {
                const float h = left > kStep ? kStep : left;
                done = stepLive(m_live[static_cast<size_t>(i)], h, world);
                left -= h;
            }
            if (done)
                m_live.erase(m_live.begin() + i);
            else
                ++i;
        }
    }

    int ThrowableFlights::liveCount() const
    {
        return static_cast<int>(m_live.size());
    }

    bool ThrowableFlights::livePosition(int index, Math::Vector3f& out) const
    {
        if (index < 0 || index >= static_cast<int>(m_live.size()))
            return false;
        out = m_live[static_cast<size_t>(index)].position;
        return true;
    }

    std::string_view ThrowableFlights::liveItemId(int index) const
    {
        if (index < 0 || index >= static_cast<int>(m_live.size()))
            return {};
        return m_live[static_cast<size_t>(index)].itemId;
    }

    const std::vector<ThrowableLanding>& ThrowableFlights::landings() const
    {
        return m_landings;
    }

    void ThrowableFlights::clearLandings()
    {
        m_landings.clear();
    }

    void ThrowableFlights::clear()
    {
        m_live.clear();
        m_landings.clear();
    }

    void ThrowableFlights::copyLive(std::vector<ThrowableFlightSnapshot>& out) const
    {
        out.clear();
        out.reserve(m_live.size());
        for (const Live& flight : m_live)
        {
            ThrowableFlightSnapshot snap;
            snap.itemId         = flight.itemId;
            snap.impactClip     = flight.impactClip;
            snap.weight         = flight.weight;
            snap.throwDistance  = flight.throwDistance;
            snap.position       = flight.position;
            snap.velocity       = flight.velocity;
            snap.gravity        = flight.gravity;
            snap.age            = flight.age;
            snap.life           = flight.maxLife;
            snap.clearOfThrower = flight.clearOfThrower;
            out.push_back(std::move(snap));
        }
    }

    void ThrowableFlights::restoreLive(const std::vector<ThrowableFlightSnapshot>& snaps)
    {
        m_live.clear();
        m_landings.clear();
        for (const ThrowableFlightSnapshot& snap : snaps)
        {
            if (snap.itemId.empty() || !finiteVec(snap.position) || !finiteVec(snap.velocity))
                continue;
            if (!std::isfinite(snap.gravity) || snap.gravity < 0.0f)
                continue;
            if (!std::isfinite(snap.age) || !std::isfinite(snap.life) || snap.life < 0.0f)
                continue;
            if (!std::isfinite(snap.weight) || !std::isfinite(snap.throwDistance))
                continue;
            Live live;
            live.itemId         = snap.itemId;
            live.impactClip     = snap.impactClip;
            live.weight         = snap.weight;
            live.throwDistance  = snap.throwDistance;
            live.position       = snap.position;
            live.velocity       = snap.velocity;
            live.gravity        = snap.gravity;
            live.age            = snap.age > 0.0f ? snap.age : 0.0f;
            live.maxLife        = snap.life;
            live.thrower        = {};
            live.clearOfThrower = snap.clearOfThrower;
            m_live.push_back(std::move(live));
        }
    }

    bool beginThrowableThrow(ThrowableFlights& flights, WeaponLoadout& loadout, ThrowableBelt& belt, const ThrowableLaunch& launch)
    {
        if (loadout.activeKind() != WeaponKind::Throwable)
            return false;
        if (belt.hand() != launch.itemId || belt.count(launch.itemId) <= 0)
            return false;
        if (!launchAccepted(launch))
            return false;
        if (!loadout.spendThrowable(belt))
            return false;
        if (!flights.launch(launch))
            return false;
        return true;
    }

    Entity placeThrowablePickup(World& world, std::string_view id, const Math::Vector3f& position)
    {
        const ItemDef* def = throwablePickupDef(id);
        if (!def)
            return {};
        const Entity e = world.createEntity();
        world.emplace<TransformComponent>(e, position, Math::Quaternion::IDENTITY, Math::Vector3f{ 0.3f, 0.3f, 0.3f });
        world.emplace<ItemPickupComponent>(e, ItemPickupComponent{ ItemStack{ def, 1 } });
        return e;
    }

} // namespace Dark
