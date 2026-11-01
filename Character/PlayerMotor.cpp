#include "Character/PlayerMotor.h"
#include "Character/SkillLimits.h"

#include "Math/MathHelper.h"

namespace Dark
{
    using Math::Vector3f;
    using Math::Vect3f;

    PlayerMotor::PlayerMotor(PlayerMotorSettings settings)
        : m_settings(settings)
        , m_velocity{ 0.0f, 0.0f, 0.0f }
    {
    }

    void PlayerMotor::reset()
    {
        m_state         = PlayerMoveState::Grounded;
        m_velocity      = Vect3f::ZERO;
        m_airTime       = 0.0f;
        m_coyote        = 0.0f;
        m_jumpBuffer    = 0.0f;
        m_didFirstJump  = false;
        m_didDoubleJump = false;
        m_pendingDouble = false;
        m_lastTap       = MoveCardinal::None;
        m_tapAge        = 1.0f;
        m_crouchBlend   = 0.0f;
        m_dodgeLeft     = 0.0f;
        m_dodgeWish     = Vect3f::ZERO;
        m_depth         = 0.0f;
        m_breath        = 1.0f;
        m_autoDescend   = false;
        clearSwimPose();
    }

    void PlayerMotor::clearJumpBuffer()
    {
        m_jumpBuffer = 0.0f;
    }

    void PlayerMotor::approachCrouch(float dt)
    {
        const float target = (m_state == PlayerMoveState::Crouch) ? 1.0f : 0.0f;
        constexpr float kBlend = 0.16f; // matches the Crouch blend in human.anim.json
        if (dt <= 0.0f)
            return;
        const float step = dt / kBlend;
        if (m_crouchBlend < target)
            m_crouchBlend = Math::Min(target, m_crouchBlend + step);
        else if (m_crouchBlend > target)
            m_crouchBlend = Math::Max(target, m_crouchBlend - step);
    }

    void PlayerMotor::setHorizontalVelocity(float x, float z)
    {
        m_velocity.x = x;
        m_velocity.z = z;
    }

    float PlayerMotor::sampleGround(const PlayerGroundQuery& ground, float x, float z) const
    {
        if (!ground.heightAt)
            return 0.0f;
        return ground.heightAt(ground.user, x, z);
    }

    bool PlayerMotor::columnDeep(float groundY, float waterY) const
    {
        return (waterY - groundY) > m_settings.swimDepth;
    }

    bool PlayerMotor::columnShallow(float groundY, float waterY) const
    {
        return (waterY - groundY) < m_settings.swimDepth;
    }

    void PlayerMotor::clearSwimPose()
    {
        m_posture    = SwimPosture::None;
        m_pose       = 0.0f;
        m_diveLeft   = 0.0f;
        m_standLeft  = 0.0f;
        m_pushing    = false;
    }

    void PlayerMotor::beginStand(const Vector3f& position)
    {
        m_standFromY = position.y;
        m_standPose0 = m_pose;
        m_standLeft  = m_settings.standSeconds;
        m_posture    = SwimPosture::Standing;
        m_state      = PlayerMoveState::Grounded;
        m_depth      = 0.0f;
        m_velocity.y = 0.0f;
        m_diveLeft   = 0.0f;
        m_pushing    = false;
        m_airTime    = 0.0f;
        m_coyote     = m_settings.coyoteTime;
        m_didFirstJump  = false;
        m_didDoubleJump = false;
        m_pendingDouble = false;
    }

    void PlayerMotor::swimFrame(Vector3f& position, const PlayerMotorInput& in, float dt, const PlayerGroundQuery& ground, bool slide)
    {
        const float wishMag = Vector3f{ in.wish.x, 0.0f, in.wish.z }.Magnitude();
        m_pushing           = wishMag >= m_settings.swimWish;

        if (slide)
        {
            const float under     = m_depth >= m_settings.submergeDepth ? m_settings.underwaterSwimScale : 1.0f;
            const float swimScale = clampSkill(in.swimScale, kSwimScaleMin, kSwimScaleMax);
            const float speedScale = Math::Clamp(in.speedScale, 0.0f, 1.0f);
            moveHorizontal(position, in.wish, m_settings.swimSpeed * swimScale * speedScale * under, dt, 0.0f);
        }

        if (m_diveLeft > 0.0f)
        {
            m_diveLeft -= dt;
            if (m_diveLeft < 0.0f)
                m_diveLeft = 0.0f;
        }
        if (m_diveLeft > 0.0f)
        {
            m_posture = SwimPosture::Diving;
            const float dur = m_settings.diveSeconds > 0.05f ? m_settings.diveSeconds : 0.05f;
            m_pose          = approachSpeed(m_pose, 1.0f, 1.0f / dur, dt);
        }
        else
        {
            m_posture       = m_pushing ? SwimPosture::Horizontal : SwimPosture::Treading;
            const float dur = m_settings.strokeBlendSeconds > 0.05f ? m_settings.strokeBlendSeconds : 0.05f;
            m_pose          = approachSpeed(m_pose, m_pushing ? 1.0f : 0.0f, 1.0f / dur, dt);
        }
        m_pose = Math::Clamp(m_pose, 0.0f, 1.0f);

        const float groundY = sampleGround(ground, position.x, position.z);
        const float rise    = in.crouch ? -m_settings.diveSpeed : (in.jumpHeld ? m_settings.surfaceSpeed : m_settings.floatSpeed);
        const float drop    = m_settings.treadHipDrop + (m_settings.swimHipDrop - m_settings.treadHipDrop) * m_pose;
        const float ceiling = ground.waterY - drop;
        const float bed     = groundY + m_settings.groundOffset;
        const float maxDepth = Math::Max(0.0f, ceiling - bed);
        m_depth              = Math::Clamp(m_depth - rise * dt, 0.0f, maxDepth);
        const float floatHip = ceiling - m_depth;
        if (m_posture == SwimPosture::Diving)
            position.y = m_diveFromY + (floatHip - m_diveFromY) * m_pose;
        else
            position.y = floatHip;
        m_velocity.y = 0.0f;
        m_jumpBuffer = 0.0f;
    }

    float PlayerMotor::standHeight(float feetY, float dt)
    {
        const float dur = m_settings.standSeconds > 0.05f ? m_settings.standSeconds : 0.05f;
        m_standLeft -= dt;
        float u = 1.0f - m_standLeft / dur;
        if (u < 0.0f)
            u = 0.0f;
        if (m_standLeft <= 0.0f || u >= 1.0f)
        {
            m_posture   = SwimPosture::None;
            m_pose      = 0.0f;
            m_standLeft = 0.0f;
            return feetY;
        }
        m_pose = m_standPose0 * (1.0f - u);
        return m_standFromY + (feetY - m_standFromY) * u;
    }

    void PlayerMotor::moveHorizontal(Vector3f& position, Vector3f wish, float speed, float dt, float acceleration)
    {
        wish.y          = 0.0f;
        const float mag = wish.Magnitude();
        if (mag > 1.0f)
            wish *= (1.0f / mag);

        Vector3f desired{ 0.0f, 0.0f, 0.0f };
        if (mag >= 1.0e-8f)
        {
            desired.x = wish.x * speed;
            desired.z = wish.z * speed;
        }

        // Zero acceleration is the old snap, including an instant stop when the stick is released.
        if (!(acceleration > 0.0f))
        {
            m_velocity.x = desired.x;
            m_velocity.z = desired.z;
            if (mag < 1.0e-8f)
                return;
        }
        else
        {
            Vector3f delta{ desired.x - m_velocity.x, 0.0f, desired.z - m_velocity.z };
            const float dist = delta.Magnitude();
            const float step = acceleration * ((dt > 0.0f) ? dt : 0.0f);
            if (dist > step && dist > 1.0e-8f)
                delta *= step / dist;
            m_velocity.x += delta.x;
            m_velocity.z += delta.z;
            if (mag < 1.0e-8f && (m_velocity.x * m_velocity.x + m_velocity.z * m_velocity.z) < 1.0e-8f)
            {
                m_velocity.x = 0.0f;
                m_velocity.z = 0.0f;
                return;
            }
        }
        position.x += m_velocity.x * dt;
        position.z += m_velocity.z * dt;
    }

    void PlayerMotor::applyAirControl(const Vector3f& wish, float dt, float airControlScale)
    {
        Vector3f dir = wish;
        dir.y        = 0.0f;
        float mag    = dir.Magnitude();
        if (mag < 1.0e-8f)
            return;
        if (mag > 1.0f)
        {
            dir *= (1.0f / mag);
            mag = 1.0f;
        }
        else
            dir *= (1.0f / mag);

        const float scale   = airControlScale < 0.0f ? 0.0f : airControlScale;
        const float maxAir  = m_settings.airSpeed * scale * mag;
        const float current = m_velocity.x * dir.x + m_velocity.z * dir.z;
        const float add     = maxAir - current;
        if (add <= 0.0f)
            return;

        float accel = m_settings.airAccel * scale * dt;
        if (accel > add)
            accel = add;
        m_velocity.x += dir.x * accel;
        m_velocity.z += dir.z * accel;
    }

    void PlayerMotor::enterGrounded(Vector3f& position, float groundY)
    {
        clearSwimPose();
        m_state         = PlayerMoveState::Grounded;
        m_velocity.y    = 0.0f;
        position.y      = groundY + m_settings.groundOffset;
        m_autoDescend   = false;
        m_airTime       = 0.0f;
        m_coyote        = m_settings.coyoteTime;
        m_didFirstJump  = false;
        m_didDoubleJump = false;
        m_pendingDouble = false;
    }

    void PlayerMotor::enterSwim(Vector3f& position, float waterY)
    {
        (void)waterY;
        m_state         = PlayerMoveState::Swimming;
        m_depth         = Math::Max(0.0f, -m_velocity.y * m_settings.plungeScale);
        m_velocity.y    = 0.0f;
        m_diveFromY     = position.y;
        m_diveLeft      = m_settings.diveSeconds;
        m_posture       = SwimPosture::Diving;
        m_pose          = 0.0f;
        m_standLeft     = 0.0f;
        m_airTime       = 0.0f;
        m_coyote        = 0.0f;
        m_jumpBuffer    = 0.0f;
        m_didFirstJump  = false;
        m_didDoubleJump = false;
        m_pendingDouble = false;
    }

    void PlayerMotor::beginJump(float jumpScale, PlayerMotorResult& result)
    {
        clearSwimPose();
        m_state         = PlayerMoveState::Jumping;
        m_velocity.y    = m_settings.jumpSpeed * jumpScale;
        m_airTime       = 0.0f;
        m_coyote        = 0.0f;
        m_didFirstJump  = true;
        m_didDoubleJump = false;
        m_pendingDouble = false;
        result.jumped   = true;
    }

    void PlayerMotor::beginFalling()
    {
        clearSwimPose();
        m_state         = PlayerMoveState::Falling;
        m_velocity.y    = 0.0f;
        m_airTime       = 0.0f;
        m_didFirstJump  = false;
        m_didDoubleJump = false;
        m_pendingDouble = false;
        m_autoDescend   = false;
    }

    void PlayerMotor::noteDodgeTap(const PlayerMotorInput& in, PlayerMotorResult& result)
    {
        if (in.dodgeTap == MoveCardinal::None)
            return;
        const bool second = m_lastTap == in.dodgeTap && m_tapAge <= m_settings.dodgeTapWindow;
        m_lastTap          = in.dodgeTap;
        m_tapAge           = 0.0f;
        if (!second || !in.allowDodge || (m_state != PlayerMoveState::Grounded && m_state != PlayerMoveState::Crouch) || m_settings.dodgeDuration <= 0.0f)
            return;

        Vector3f wish = in.dodgeTapWish;
        wish.y        = 0.0f;
        const float mag = wish.Magnitude();
        if (mag < 1.0e-4f)
            return;
        wish *= (1.0f / mag);
        m_dodgeWish = wish;
        m_dodgeLeft = m_settings.dodgeDuration;
        m_state     = PlayerMoveState::Dodge;
        result.dodged = true;
    }

    bool PlayerMotor::tryDoubleJump(bool jumpPressed, bool allowDoubleJump, float jumpScale, PlayerMotorResult& result)
    {
        if (!allowDoubleJump)
            return false;
        if (m_didDoubleJump || !m_didFirstJump)
            return false;

        if (jumpPressed && m_airTime < m_settings.doubleJumpMinDelay)
            m_pendingDouble = true;

        const bool want = jumpPressed || m_pendingDouble;
        if (!want)
            return false;

        if (m_airTime < m_settings.doubleJumpMinDelay)
            return false;
        if (m_airTime > m_settings.doubleJumpWindow)
        {
            m_pendingDouble = false;
            return false;
        }

        m_velocity.y    = m_settings.doubleJumpSpeed * jumpScale;
        m_didDoubleJump = true;
        m_pendingDouble = false;
        m_state         = PlayerMoveState::Jumping;
        result.jumped   = true;
        result.doubleJumped = true;
        return true;
    }

    bool PlayerMotor::findWall(const PlayerGroundQuery& ground, const Vector3f& position, const Vector3f& dir, float reach, float& dist) const
    {
        return ground.wallAt && dir.MagnitudeSqrd() > 1.0e-6f && ground.wallAt(ground.wallUser, position, dir, reach, dist);
    }

    bool PlayerMotor::findLedge(const PlayerGroundQuery& ground, const Vector3f& position, const Vector3f& dir, float wallDist, float& topY) const
    {
        if (!ground.ledgeAt)
            return false;
        const float feetY = position.y - m_settings.groundOffset;
        const float x     = position.x + dir.x * (wallDist + 0.35f);
        const float z     = position.z + dir.z * (wallDist + 0.35f);
        return ground.ledgeAt(ground.wallUser, x, z, feetY + m_settings.handReach + 0.8f, topY) && topY > feetY + 0.4f;
    }

    // Shoulder probes sit at the play-capsule radius. The wall cast itself is a
    // 0.3 m capsule, so the center still hits for about that far past a vertical
    // edge. A shoulder that misses, or lands much farther than the center, is
    // not this face.
    constexpr float kWallShoulder      = 0.45f;
    constexpr float kWallShoulderSlack = 0.12f;
    constexpr float kWallEdgePush      = 0.35f;
    constexpr float kWallLeaveNudge    = 0.22f;
    constexpr float kWallLandingReach  = 0.35f;
    constexpr float kWallLipGrab       = 0.15f;

    static Vector3f wallTangentOf(const Vector3f& wallDir)
    {
        Vector3f tangent{ -wallDir.z, 0.0f, wallDir.x };
        if (tangent.MagnitudeSqrd() < 1.0e-6f)
            return Vector3f{ 1.0f, 0.0f, 0.0f };
        tangent.Normalize();
        return tangent;
    }

    static Vector3f planarUnit(Vector3f dir)
    {
        dir.y = 0.0f;
        if (dir.MagnitudeSqrd() < 1.0e-6f)
            return Vector3f{ 0.0f, 0.0f, 1.0f };
        dir.Normalize();
        return dir;
    }

    bool PlayerMotor::shoulderOnFace(const PlayerGroundQuery& ground, const Vector3f& position, const Vector3f& side, float centerDist, float reach) const
    {
        float dist = 0.0f;
        if (!findWall(ground, position + side * kWallShoulder, m_wallDir, reach, dist))
            return false;
        return dist <= centerDist + kWallShoulderSlack;
    }

    bool PlayerMotor::releasedByCorner(const PlayerGroundQuery& ground, const Vector3f& position, const Vector3f& wishIn, Vector3f& shove) const
    {
        const float reach = m_settings.wallReach + 0.3f;
        float       centerDist = 0.0f;
        if (!findWall(ground, position, m_wallDir, reach, centerDist))
            return false;

        const Vector3f tangent{ -m_wallDir.z, 0.0f, m_wallDir.x };
        const bool     posFace = shoulderOnFace(ground, position, tangent, centerDist, reach);
        const bool     negFace = shoulderOnFace(ground, position, -tangent, centerDist, reach);
        const Vector3f wish{ wishIn.x, 0.0f, wishIn.z };
        const float    along = wish.Dot(tangent);

        if (!posFace && !negFace)
        {
            // A corner or post. Back off when the wish still points into it.
            if (wish.MagnitudeSqrd() > 0.04f && wish.Dot(m_wallDir) < 0.5f)
                shove = wish;
            else
                shove = -m_wallDir;
            return true;
        }
        if (!posFace && along > kWallEdgePush)
        {
            shove = tangent;
            return true;
        }
        if (!negFace && along < -kWallEdgePush)
        {
            shove = -tangent;
            return true;
        }
        return false;
    }

    bool PlayerMotor::alignWallFace(const PlayerGroundQuery& ground, const Vector3f& position)
    {
        const Vector3f forward = planarUnit(m_wallDir);
        const Vector3f tangent = wallTangentOf(forward);
        const Vector3f samples[5] = {
            forward,
            planarUnit(forward + tangent * 0.5f),
            planarUnit(forward - tangent * 0.5f),
            planarUnit(forward + tangent),
            planarUnit(forward - tangent),
        };
        const float reach    = m_settings.wallReach + 0.3f;
        float       bestDist = reach + 1.0f;
        Vector3f    best     = forward;
        bool        found    = false;
        for (const Vector3f& dir : samples)
        {
            float dist = 0.0f;
            if (findWall(ground, position, dir, reach, dist) && dist < bestDist)
            {
                bestDist = dist;
                best     = dir;
                found    = true;
            }
        }
        if (!found)
            return false;
        m_wallDir = best;
        return true;
    }

    void PlayerMotor::slideOnWall(Vector3f& position, const Vector3f& wishIn, float dt, float intoScale, const PlayerGroundQuery& ground)
    {
        const Vector3f tangent = wallTangentOf(m_wallDir);
        const Vector3f wish{ wishIn.x, 0.0f, wishIn.z };
        float          along    = Math::Clamp(wish.Dot(tangent), -1.0f, 1.0f);
        float          vertical = Math::Clamp(intoScale, -1.0f, 1.0f);
        const float    mag2     = along * along + vertical * vertical;
        if (mag2 > 1.0f)
        {
            const float inv = 1.0f / sqrtf(mag2);
            along *= inv;
            vertical *= inv;
        }

        const float speed = m_settings.wallRunSpeed * (0.6f + 0.4f * Math::Max(0.0f, m_stamina));
        m_velocity         = tangent * (along * speed);
        m_velocity.y       = vertical * speed;

        if (intoScale < 0.0f)
        {
            const float groundY = sampleGround(ground, position.x, position.z);
            const float floorY  = groundY + m_settings.groundOffset;
            if (position.y + m_velocity.y * dt <= floorY)
            {
                position.x += m_velocity.x * dt;
                position.z += m_velocity.z * dt;
                enterGrounded(position, groundY);
                m_velocity.x = tangent.x * along * speed;
                m_velocity.z = tangent.z * along * speed;
                return;
            }
        }

        position.x += m_velocity.x * dt;
        position.y += m_velocity.y * dt;
        position.z += m_velocity.z * dt;
    }

    void PlayerMotor::leaveWall(Vector3f& position, const Vector3f& shoveIn)
    {
        Vector3f shove{ shoveIn.x, 0.0f, shoveIn.z };
        if (shove.MagnitudeSqrd() < 1.0e-6f)
            shove = Vector3f{ -m_wallDir.x, 0.0f, -m_wallDir.z };
        if (shove.MagnitudeSqrd() < 1.0e-6f)
            return;
        shove.Normalize();
        beginFalling();
        m_velocity.x   = shove.x * m_settings.walkSpeed;
        m_velocity.z   = shove.z * m_settings.walkSpeed;
        position.x    += shove.x * kWallLeaveNudge;
        position.z    += shove.z * kWallLeaveNudge;
        m_wallCooldown = m_settings.wallJumpCooldown;
    }

    bool PlayerMotor::nearestWall(const PlayerGroundQuery& ground, const Vector3f& position, const Vector3f& wish, Vector3f& dir) const
    {
        Vector3f ahead{ wish.x, 0.0f, wish.z };
        if (ahead.MagnitudeSqrd() < 1.0e-4f)
            ahead = Vector3f{ m_velocity.x, 0.0f, m_velocity.z };
        if (ahead.MagnitudeSqrd() < 1.0e-4f)
            ahead = Vector3f{ 0.0f, 0.0f, 1.0f };
        ahead.Normalize();
        const Vector3f dirs[4] = { ahead, Vector3f{ -ahead.z, 0.0f, ahead.x }, Vector3f{ ahead.z, 0.0f, -ahead.x }, -ahead };
        float          best    = m_settings.wallReach;
        bool           found   = false;
        for (const Vector3f& d : dirs)
        {
            float dist = 0.0f;
            if (findWall(ground, position, d, m_settings.wallReach, dist) && dist <= best)
            {
                best  = dist;
                dir   = d;
                found = true;
            }
        }
        return found;
    }

    void PlayerMotor::wallJump(const Vector3f& awayFromWall, PlayerMotorResult& result)
    {
        clearSwimPose();
        m_velocity      = awayFromWall * m_settings.wallJumpOut;
        m_velocity.y    = m_settings.wallJumpUp;
        m_state         = PlayerMoveState::Jumping;
        m_didFirstJump  = true;
        m_didDoubleJump = false;
        m_wallCooldown  = m_settings.wallJumpCooldown;
        m_autoDescend   = false;
        m_stamina       = Math::Max(0.0f, m_stamina - m_settings.wallJumpCost);
        m_jumpBuffer    = 0.0f;
        result.jumped   = true;
    }

    void PlayerMotor::enterWallRun(const Vector3f& dir)
    {
        clearSwimPose();
        m_wallDir  = dir;
        m_state    = PlayerMoveState::WallRun;
        m_velocity = Vector3f{ 0.0f, m_settings.wallRunSpeed, 0.0f };
        m_jumpBuffer  = 0.0f;
        m_autoDescend = false;
    }

    void PlayerMotor::beginClimb(Vector3f& position, const ClimbGrab& grab)
    {
        if (!grab.valid)
            return;
        enterWallRun(grab.wallDir);
        if (!grab.downward)
            return;
        const Vector3f out{ -grab.wallDir.x, 0.0f, -grab.wallDir.z };
        position.x      = grab.point.x + out.x * 0.40f;
        position.z      = grab.point.z + out.z * 0.40f;
        position.y     -= 0.70f;
        m_autoDescend   = true;
        m_velocity.y    = -m_settings.wallRunSpeed * 0.5f;
    }

    float PlayerMotor::wallDrainRate(float into, float along) const
    {
        if (into > 0.2f)
            return m_settings.wallRunDrain;
        if (into < -0.2f || along > 0.2f || along < -0.2f)
            return m_settings.wallSlideDrain;
        return m_settings.hangDrain;
    }

    ClimbGrab PlayerMotor::findClimbGrab(const PlayerGroundQuery& ground, const Vector3f& position) const
    {
        ClimbGrab grab{};
        if (!ground.wallAt || m_stamina <= 0.15f)
            return grab;
        if (m_state == PlayerMoveState::WallRun || m_state == PlayerMoveState::Hang || m_state == PlayerMoveState::Swimming || m_state == PlayerMoveState::Dodge)
            return grab;

        const Vector3f dirs[8] = {
            Vector3f{ 1.0f, 0.0f, 0.0f }, Vector3f{ -1.0f, 0.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 1.0f }, Vector3f{ 0.0f, 0.0f, -1.0f },
            Vector3f{ 0.70710678f, 0.0f, 0.70710678f }, Vector3f{ 0.70710678f, 0.0f, -0.70710678f },
            Vector3f{ -0.70710678f, 0.0f, 0.70710678f }, Vector3f{ -0.70710678f, 0.0f, -0.70710678f },
        };
        float best = 1.0e9f;
        auto  take = [&](const Vector3f& from, const Vector3f& dir, float reach, bool downward) {
            float dist = 0.0f;
            if (!findWall(ground, from, dir, reach, dist) || dist >= best)
                return;
            best           = dist;
            grab.valid     = true;
            grab.downward  = downward;
            grab.wallDir   = dir;
            const Vector3f hit = from + dir * dist;
            grab.point     = Vector3f{ hit.x, position.y + 0.35f, hit.z };
        };

        const float reach = m_settings.wallReach;
        for (const Vector3f& dir : dirs)
            take(position, dir, reach, false);
        if (grab.valid)
            return grab;

        Vector3f low = position;
        low.y -= 0.85f;
        const float outward = reach + 0.25f;
        for (const Vector3f& dir : dirs)
            take(low + dir * outward, -dir, reach + 0.45f, true);
        return grab;
    }

    void PlayerMotor::climbOnto(Vector3f& position, float topY, const PlayerGroundQuery& ground, PlayerMotorResult& result)
    {
        // Planting through a corner puts the body in the next face. A clear roof
        // is open ahead and to either side; the wall we climbed stays behind.
        const Vector3f landing{ position.x + m_wallDir.x * 0.8f, topY + m_settings.groundOffset, position.z + m_wallDir.z * 0.8f };
        const Vector3f tangent{ -m_wallDir.z, 0.0f, m_wallDir.x };
        const Vector3f dirs[3] = { m_wallDir, tangent, -tangent };
        for (const Vector3f& dir : dirs)
        {
            float dist = 0.0f;
            if (findWall(ground, landing, dir, kWallLandingReach, dist))
            {
                leaveWall(position, -m_wallDir);
                return;
            }
        }

        position.x += m_wallDir.x * 0.8f;
        position.z += m_wallDir.z * 0.8f;
        position.y  = topY + m_settings.groundOffset;
        clearSwimPose();
        m_velocity  = Vector3f{ 0.0f, 0.0f, 0.0f };
        m_state     = PlayerMoveState::Grounded;
        m_airTime   = 0.0f;
        m_didFirstJump  = false;
        m_didDoubleJump = false;
        result.landed   = true;
        result.climbed  = true;
    }

    void PlayerMotor::tickWall(Vector3f& position, const PlayerMotorInput& in, float dt, const PlayerGroundQuery& ground, PlayerMotorResult& result)
    {
        if (!alignWallFace(ground, position))
        {
            beginFalling();
            return;
        }

        const Vector3f wish{ in.wish.x, 0.0f, in.wish.z };
        const float    into  = wish.Dot(m_wallDir);
        const float    feetY = position.y - m_settings.groundOffset;

        if (m_state == PlayerMoveState::Hang)
        {
            Vector3f shove{};
            if (releasedByCorner(ground, position, in.wish, shove))
            {
                leaveWall(position, shove);
                return;
            }
            const float along = wish.Dot(wallTangentOf(m_wallDir));
            m_stamina -= wallDrainRate(0.0f, along) * dt;
            if (in.crouch || m_stamina <= 0.0f)
            {
                beginFalling();
                return;
            }
            if (in.jumpPressed && into < -0.5f)
            {
                wallJump(-m_wallDir, result);
                return;
            }
            if (in.jumpPressed || into > 0.5f)
                climbOnto(position, m_ledgeY, ground, result);
            else
                slideOnWall(position, wish, dt, 0.0f, ground);
            return;
        }

        if (in.jumpPressed)
        {
            wallJump(-m_wallDir, result);
            return;
        }

        Vector3f shove{};
        if (releasedByCorner(ground, position, in.wish, shove))
        {
            leaveWall(position, shove);
            return;
        }

        if (m_autoDescend && wish.MagnitudeSqrd() > 0.04f)
            m_autoDescend = false;
        const float along    = wish.Dot(wallTangentOf(m_wallDir));
        const float moveInto = m_autoDescend ? -1.0f : into;
        m_stamina -= wallDrainRate(moveInto, along) * dt;

        float wallDist = 0.0f;
        const bool wall = findWall(ground, position, m_wallDir, m_settings.wallReach + 0.3f, wallDist);
        float topY = 0.0f;
        const bool ledge = findLedge(ground, position, m_wallDir, wall ? wallDist : 0.3f, topY) && topY <= feetY + m_settings.handReach;
        if (ledge && into > 0.5f)
        {
            m_ledgeY = topY;
            climbOnto(position, topY, ground, result);
            return;
        }
        if (ledge && into > kWallLipGrab)
        {
            m_ledgeY   = topY;
            m_state    = PlayerMoveState::Hang;
            m_velocity = Vector3f{ 0.0f, 0.0f, 0.0f };
            position.y = topY - m_settings.handReach + m_settings.groundOffset;
            return;
        }
        if (!wall || m_stamina <= 0.0f)
        {
            beginFalling();
            return;
        }
        slideOnWall(position, wish, dt, moveInto, ground);
    }

    PlayerMotorResult PlayerMotor::tick(Vector3f& position, const PlayerMotorInput& in, float dt, const PlayerGroundQuery& ground)
    {
        PlayerMotorResult result{};
        if (dt < 0.0f)
            dt = 0.0f;
        approachCrouch(dt);
        const float speedScale = Math::Clamp(in.speedScale, 0.0f, 1.0f);
        const float runScale   = clampSkill(in.runScale, kRunScaleMin, kRunScaleMax);
        const float jumpScale  = clampSkill(in.jumpScale, kJumpScaleMin, kJumpScaleMax);

        if (m_jumpBuffer > 0.0f)
            m_jumpBuffer = Math::Max(0.0f, m_jumpBuffer - dt);
        if (in.jumpPressed && in.allowJumpBuffer)
            m_jumpBuffer = m_settings.jumpBuffer;

        m_tapAge += dt;
        noteDodgeTap(in, result);
        if (submerged())
            m_breath = Math::Max(0.0f, m_breath - dt / Math::Max(m_settings.breathSeconds, 0.1f));
        else
            m_breath = Math::Min(1.0f, m_breath + m_settings.breathRegen * dt);
        if (m_wallCooldown > 0.0f)
            m_wallCooldown -= dt;
        if (m_state == PlayerMoveState::Grounded || m_state == PlayerMoveState::Crouch || m_state == PlayerMoveState::Dodge)
            m_stamina = Math::Min(1.0f, m_stamina + m_settings.staminaRegen * dt);
        if (m_state == PlayerMoveState::WallRun || m_state == PlayerMoveState::Hang)
        {
            tickWall(position, in, dt, ground, result);
            return result;
        }

        const float waterY  = ground.waterY;
        float       groundY = sampleGround(ground, position.x, position.z);

        if (m_state == PlayerMoveState::Swimming)
        {
            if (columnShallow(groundY, waterY))
            {
                beginStand(position);
                result.stood = true;
            }
            else
            {
                const float surfaceY = waterY + m_settings.swimOffset;
                if (in.jumpPressed && m_depth <= m_settings.leapDepth)
                {
                    clearSwimPose();
                    m_depth         = 0.0f;
                    position.y      = surfaceY;
                    m_state         = PlayerMoveState::Jumping;
                    m_velocity.y    = m_settings.waterLeapSpeed * jumpScale;
                    m_airTime       = 0.0f;
                    m_coyote        = 0.0f;
                    m_jumpBuffer    = 0.0f;
                    m_didFirstJump  = true;
                    m_didDoubleJump = true;
                    result.jumped   = true;
                    result.leapt    = true;
                    return result;
                }
                swimFrame(position, in, dt, ground, true);
                return result;
            }
        }

        if (m_state == PlayerMoveState::Dodge && (m_dodgeLeft <= 0.0f || !in.allowDodge))
            m_state = PlayerMoveState::Grounded;

        if (m_state == PlayerMoveState::Grounded || m_state == PlayerMoveState::Dodge || m_state == PlayerMoveState::Crouch)
        {
            if (columnDeep(groundY, waterY))
            {
                enterSwim(position, waterY);
                result.splashed = true;
                swimFrame(position, in, dt, ground, true);
                return result;
            }

            if (in.climbPressed && m_state != PlayerMoveState::Dodge && m_stamina > 0.15f)
            {
                const ClimbGrab grab = findClimbGrab(ground, position);
                if (grab.valid)
                {
                    beginClimb(position, grab);
                    return result;
                }
            }

            Vector3f moveWish = in.wish;
            float    speed    = (in.sprint ? m_settings.sprintSpeed : m_settings.walkSpeed) * runScale * speedScale;
            bool     dodging  = false;
            if (m_state == PlayerMoveState::Dodge)
            {
                m_dodgeLeft -= dt;
                if (m_dodgeLeft <= 0.0f || !in.allowDodge)
                    m_state = in.crouch ? PlayerMoveState::Crouch : PlayerMoveState::Grounded;
                else
                {
                    dodging  = true;
                    moveWish = m_dodgeWish;
                    speed    = m_settings.dodgeSpeed * speedScale;
                }
            }
            if (!dodging && in.crouch)
            {
                m_state = PlayerMoveState::Crouch;
                speed   = m_settings.crouchSpeed * speedScale;
            }
            else if (!dodging && m_state == PlayerMoveState::Crouch)
                m_state = PlayerMoveState::Grounded;
            moveHorizontal(position, moveWish, speed, dt, dodging ? 0.0f : m_settings.acceleration);
            groundY               = sampleGround(ground, position.x, position.z);
            const float feetY     = groundY + m_settings.groundOffset - m_settings.crouchDrop * m_crouchBlend;

            if (columnDeep(groundY, waterY))
            {
                enterSwim(position, waterY);
                result.splashed = true;
                swimFrame(position, in, dt, ground, false);
                return result;
            }

            if (m_posture != SwimPosture::Standing && position.y - feetY > m_settings.stepDown)
            {
                beginFalling();
                m_velocity.y -= m_settings.gravity * dt;
                position.y += m_velocity.y * dt;
                m_airTime += dt;
                return result;
            }
            else
            {
                position.y   = m_posture == SwimPosture::Standing ? standHeight(feetY, dt) : feetY;
                m_velocity.y = 0.0f;
                m_coyote     = m_settings.coyoteTime;
                if (m_jumpBuffer > 0.0f && in.allowJumpBuffer)
                {
                    m_jumpBuffer = 0.0f;
                    beginJump(jumpScale, result);
                    m_velocity.y -= m_settings.gravity * dt;
                    position.y += m_velocity.y * dt;
                    m_airTime += dt;
                }
                return result;
            }
        }

        applyAirControl(in.wish, dt, in.airControlScale * speedScale);
        position.x += m_velocity.x * dt;
        position.z += m_velocity.z * dt;
        m_velocity.y -= m_settings.gravity * dt;
        position.y += m_velocity.y * dt;
        m_airTime += dt;
        if (m_coyote > 0.0f)
            m_coyote = Math::Max(0.0f, m_coyote - dt);

        Vector3f wallDir{};
        if (!m_didFirstJump && m_jumpBuffer > 0.0f && m_coyote > 0.0f && in.allowJumpBuffer)
        {
            m_jumpBuffer = 0.0f;
            beginJump(jumpScale, result);
        }
        else if (in.climbPressed && m_stamina > 0.15f)
        {
            const ClimbGrab grab = findClimbGrab(ground, position);
            if (grab.valid)
            {
                beginClimb(position, grab);
                return result;
            }
        }
        else if (in.jumpPressed && m_wallCooldown <= 0.0f && m_stamina > 0.0f && nearestWall(ground, position, in.wish, wallDir))
            wallJump(-wallDir, result);
        else
            tryDoubleJump(in.jumpPressed, in.allowDoubleJump, jumpScale, result);

        if (m_state == PlayerMoveState::Jumping && m_velocity.y <= 0.0f)
            m_state = PlayerMoveState::Falling;

        groundY           = sampleGround(ground, position.x, position.z);
        const float feetY = groundY + m_settings.groundOffset;

        if (columnDeep(groundY, waterY) && position.y <= waterY + m_settings.swimOffset)
        {
            enterSwim(position, waterY);
            result.splashed = true;
            swimFrame(position, in, dt, ground, false);
            return result;
        }

        if (m_velocity.y <= 0.0f && position.y <= feetY)
        {
            result.landed = true;
            enterGrounded(position, groundY);
            if (m_jumpBuffer > 0.0f && in.allowJumpBuffer && !columnDeep(groundY, waterY))
            {
                m_jumpBuffer = 0.0f;
                beginJump(jumpScale, result);
                m_velocity.y -= m_settings.gravity * dt;
                position.y += m_velocity.y * dt;
                m_airTime += dt;
            }
        }

        return result;
    }

} // namespace Dark
