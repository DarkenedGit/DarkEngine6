#pragma once

#include "Character/PlayerMotor.h"
#include "Collision/SweptCollision.h"
#include "Combat/JumpAttack.h"
#include "Math/MathHelper.h"
#include "Math/Vector3f.h"
#include "Physics/PhysicsWorld.h"

#include <cstdint>

namespace Dark
{

    inline constexpr float kPlayBodyRadius = 0.45f;

    struct PlayMotorEdges
    {
        bool sprintDown    = false;
        bool crouchPressed = false;
        bool crouchDown    = false;
        bool jumpPressed   = false;
        bool jumpDown      = false;
        bool forward       = false;
        bool back          = false;
        bool left          = false;
        bool right         = false;
    };

    // Fills steer, crouch latch, jump, and dodge. speedScale and the skill scales stay with the caller.
    // sprintAllowed already includes the sandbox swing lock. Pass a fresh motorIn; this does not read the old one.
    inline void buildPlayMotorInput(PlayerMotorInput& motorIn, bool& crouchLatch, const Math::Vector3f& wish, const Math::Vector3f& flat, const Math::Vector3f& right, bool canSteer, bool jumpBusy, bool uiKeys, bool inAirCommit, const Combat::JumpAttack* jump, bool sprintAllowed, const PlayMotorEdges& edges)
    {
        motorIn.wish            = canSteer ? wish : Math::Vector3f{ 0.0f, 0.0f, 0.0f };
        motorIn.sprint          = sprintAllowed && edges.sprintDown;
        if (crouchLatch && edges.crouchPressed)
            crouchLatch = false;
        motorIn.crouch          = canSteer && !jumpBusy && !uiKeys && (crouchLatch || edges.crouchDown);
        motorIn.jumpPressed     = canSteer && !jumpBusy && !uiKeys && edges.jumpPressed;
        motorIn.jumpHeld        = canSteer && !uiKeys && edges.jumpDown;
        motorIn.allowDoubleJump = !inAirCommit;
        motorIn.allowJumpBuffer = !jumpBusy;
        motorIn.airControlScale = (inAirCommit && jump) ? jump->def().airControlScale : 1.0f;
        motorIn.allowDodge      = canSteer && !jumpBusy && !uiKeys;
        if (!motorIn.allowDodge)
            return;
        motorIn.dodgeTap     = moveCardinalFromEdges(edges.forward, edges.back, edges.left, edges.right);
        motorIn.dodgeTapWish = moveCardinalWish(motorIn.dodgeTap, flat, right);
    }

    struct PlanarContact
    {
        bool           hit       = false;
        float          t         = 1.0f;
        Math::Vector3f normal    = Math::Vector3f::ZERO;
        bool           hasNormal = false;
    };

    inline constexpr float kPlanarSkinFraction    = 0.02f;
    inline constexpr int   kPlanarSlideIterations = 3;
    inline constexpr float kPlanarMinWallXZ       = 0.5f;
    inline constexpr float kSameSolidMetres       = 0.02f;
    inline constexpr bool  kPlanarSlideCounters   = false;

    struct PlanarSlideCounts
    {
        uint32_t substeps  = 0;
        uint32_t hardStops = 0;
    };

    inline PlanarContact contactSphereSolid(const Math::Sphere3f& sphere, const Math::Vector3f& step, const Math::AABox3f& box)
    {
        PlanarContact out;
        const Collision::SweptHit3D swept = Collision::SweptIntersects(sphere, step, box);
        if (!swept.hit)
            return out;
        out.hit = true;
        out.t   = swept.t;
        const Math::Vector3f center = sphere.Center + step * swept.t;
        const Math::Vector3f closest{
            Math::Clamp(center.x, box.Min.x, box.Max.x),
            Math::Clamp(center.y, box.Min.y, box.Max.y),
            Math::Clamp(center.z, box.Min.z, box.Max.z),
        };
        Math::Vector3f n = center - closest;
        if (!(n.MagnitudeSqrd() > 0.0f))
            return out;
        n.Normalize();
        out.normal    = n;
        out.hasNormal = true;
        return out;
    }

    inline PlanarContact contactSphereSolid(const Math::Sphere3f& sphere, const Math::Vector3f& step, const Math::Sphere3f& solid)
    {
        PlanarContact out;
        const Collision::SweptHit3D swept = Collision::SweptIntersects(sphere, step, solid);
        if (!swept.hit)
            return out;
        out.hit = true;
        out.t   = swept.t;
        const Math::Vector3f center = sphere.Center + step * swept.t;
        if (solid.Contains(center))
            return out;
        Math::Vector3f n = center - solid.Center;
        if (!(n.MagnitudeSqrd() > 0.0f))
            return out;
        n.Normalize();
        out.normal    = n;
        out.hasNormal = true;
        return out;
    }

    inline PlanarContact preferPlanarContact(const PlanarContact& host, const Physics::PhysicsWorld::MoverHit& phys, float stepLength)
    {
        const bool hostHit = host.hit;
        const bool physHit = phys.fraction < 1.0f;
        if (!hostHit && !physHit)
        {
            PlanarContact miss;
            miss.hit = false;
            miss.t   = 1.0f;
            return miss;
        }
        if (hostHit && physHit)
        {
            const float gapM = (host.t - phys.fraction) * stepLength;
            if (gapM <= kSameSolidMetres)
            {
                PlanarContact out = host;
                out.hit           = true;
                out.t             = Math::Min(host.t, phys.fraction);
                return out;
            }
            PlanarContact out;
            out.hit = true;
            out.t   = phys.fraction;
            if (phys.hasNormal)
            {
                out.hasNormal = true;
                out.normal    = phys.normal;
            }
            return out;
        }
        if (hostHit)
        {
            PlanarContact out = host;
            out.hit           = true;
            return out;
        }
        PlanarContact out;
        out.hit = true;
        out.t   = phys.fraction;
        if (phys.hasNormal)
        {
            out.hasNormal = true;
            out.normal    = phys.normal;
        }
        return out;
    }

    template <typename Sweep>
    inline void slidePlanar(Math::Vector3f& position, const Math::Vector3f& before, Math::Vector3f& delta, float dt, PlayerMotor* motor, float sweepY, float minWallXZ, Sweep&& sweep,
                            PlanarSlideCounts* counts = nullptr)
    {
        Math::Vector3f rest{ delta.x, 0.0f, delta.z };
        float          posX     = before.x;
        float          posZ     = before.z;
        Math::Vector3f applied{ 0.0f, 0.0f, 0.0f };
        bool           hardStop = false;
        bool           blocked  = false;
        Math::Vector3f planes[kPlanarSlideIterations];
        int            planeCount = 0;

        for (int iter = 0; iter < kPlanarSlideIterations; ++iter)
        {
            if (rest.MagnitudeSqrd() <= 1.0e-10f)
                break;

            const Math::Vector3f origin{ posX, sweepY, posZ };
            const PlanarContact  contact = sweep(origin, rest);
            if constexpr (kPlanarSlideCounters)
            {
                if (counts)
                    ++counts->substeps;
            }

            if (!contact.hit || !(contact.t < 1.0f))
            {
                posX += rest.x;
                posZ += rest.z;
                applied += rest;
                break;
            }

            blocked = true;
            if (!(contact.t > 0.0f))
            {
                hardStop = true;
                if constexpr (kPlanarSlideCounters)
                {
                    if (counts)
                        ++counts->hardStops;
                }
                break;
            }

            const float          keep    = Math::Max(0.0f, contact.t - kPlanarSkinFraction);
            const Math::Vector3f advance = rest * keep;
            posX += advance.x;
            posZ += advance.z;
            applied += advance;
            Math::Vector3f left = rest * (1.0f - keep);

            Math::Vector3f n = contact.normal;
            n.y              = 0.0f;
            const float xzLen = n.Magnitude();
            if (!contact.hasNormal || !(xzLen > 0.0f) || !(xzLen >= minWallXZ))
            {
                hardStop = true;
                if constexpr (kPlanarSlideCounters)
                {
                    if (counts)
                        ++counts->hardStops;
                }
                break;
            }

            n.Normalize();
            if (!(rest.Dot(n) < 0.0f))
            {
                hardStop = true;
                if constexpr (kPlanarSlideCounters)
                {
                    if (counts)
                        ++counts->hardStops;
                }
                break;
            }

            const float intoLeft = left.Dot(n);
            left -= n * intoLeft;
            if (planeCount < kPlanarSlideIterations)
                planes[planeCount++] = n;

            if (iter + 1 >= kPlanarSlideIterations)
                break;
            rest = left;
        }

        position.x = posX;
        position.z = posZ;
        delta.x    = applied.x;
        delta.y    = 0.0f;
        delta.z    = applied.z;

        if (blocked && motor && dt > 1.0e-4f)
        {
            if (hardStop)
            {
                motor->setHorizontalVelocity(0.0f, 0.0f);
                return;
            }
            Math::Vector3f v{ applied.x / dt, 0.0f, applied.z / dt };
            for (int i = 0; i < planeCount; ++i)
            {
                const float into = v.Dot(planes[i]);
                if (into < 0.0f)
                    v -= planes[i] * into;
            }
            motor->setHorizontalVelocity(v.x, v.z);
        }
    }

} // namespace Dark
