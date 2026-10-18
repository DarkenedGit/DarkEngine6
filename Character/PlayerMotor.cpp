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
        m_dodgeLeft     = 0.0f;
        m_dodgeWish     = Vect3f::ZERO;
    }

    void PlayerMotor::clearJumpBuffer()
    {
        m_jumpBuffer = 0.0f;
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

    bool PlayerMotor::terrainWet(float groundY, float waterY) const
    {
        return groundY < waterY - m_settings.enterWaterDepth;
    }

    bool PlayerMotor::terrainDry(float groundY, float waterY) const
    {
        return groundY > waterY + m_settings.leaveWaterHeight;
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
        m_state         = PlayerMoveState::Grounded;
        m_velocity.y    = 0.0f;
        position.y      = groundY + m_settings.groundOffset;
        m_airTime       = 0.0f;
        m_coyote        = m_settings.coyoteTime;
        m_didFirstJump  = false;
        m_didDoubleJump = false;
        m_pendingDouble = false;
    }

    void PlayerMotor::enterSwim(Vector3f& position, float waterY)
    {
        m_state         = PlayerMoveState::Swimming;
        m_velocity.y    = 0.0f;
        position.y      = waterY + m_settings.swimOffset;
        m_airTime       = 0.0f;
        m_coyote        = 0.0f;
        m_jumpBuffer    = 0.0f;
        m_didFirstJump  = false;
        m_didDoubleJump = false;
        m_pendingDouble = false;
    }

    void PlayerMotor::beginJump(float jumpScale, PlayerMotorResult& result)
    {
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
        m_state         = PlayerMoveState::Falling;
        m_velocity.y    = 0.0f;
        m_airTime       = 0.0f;
        m_didFirstJump  = false;
        m_didDoubleJump = false;
        m_pendingDouble = false;
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
        m_velocity      = awayFromWall * m_settings.wallJumpOut;
        m_velocity.y    = m_settings.wallJumpUp;
        m_state         = PlayerMoveState::Jumping;
        m_didFirstJump  = true;
        m_didDoubleJump = false;
        m_wallCooldown  = m_settings.wallJumpCooldown;
        m_stamina       = Math::Max(0.0f, m_stamina - m_settings.wallJumpCost);
        m_jumpBuffer    = 0.0f;
        result.jumped   = true;
    }

    void PlayerMotor::enterWallRun(const Vector3f& dir)
    {
        m_wallDir  = dir;
        m_state    = PlayerMoveState::WallRun;
        m_velocity = Vector3f{ 0.0f, m_settings.wallRunSpeed, 0.0f };
        m_jumpBuffer = 0.0f;
    }

    void PlayerMotor::climbOnto(Vector3f& position, float topY, PlayerMotorResult& result)
    {
        position.x += m_wallDir.x * 0.8f;
        position.z += m_wallDir.z * 0.8f;
        position.y  = topY + m_settings.groundOffset;
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
        const float into  = Vector3f{ in.wish.x, 0.0f, in.wish.z }.Dot(m_wallDir);
        const float feetY = position.y - m_settings.groundOffset;

        if (m_state == PlayerMoveState::Hang)
        {
            m_stamina -= m_settings.hangDrain * dt;
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
                climbOnto(position, m_ledgeY, result);
            return;
        }

        if (in.jumpPressed)
        {
            wallJump(-m_wallDir, result);
            return;
        }
        m_stamina -= m_settings.wallRunDrain * dt;

        float wallDist = 0.0f;
        const bool wall = findWall(ground, position, m_wallDir, m_settings.wallReach + 0.3f, wallDist);
        float topY = 0.0f;
        if (findLedge(ground, position, m_wallDir, wall ? wallDist : 0.3f, topY) && topY <= feetY + m_settings.handReach)
        {
            m_ledgeY = topY;
            if (into > 0.5f)
                climbOnto(position, topY, result);
            else
            {
                m_state      = PlayerMoveState::Hang;
                m_velocity   = Vector3f{ 0.0f, 0.0f, 0.0f };
                position.y   = topY - m_settings.handReach + m_settings.groundOffset;
            }
            return;
        }
        if (!wall || m_stamina <= 0.0f)
        {
            beginFalling();
            return;
        }
        const float climb = m_settings.wallRunSpeed * (0.6f + 0.4f * Math::Max(0.0f, m_stamina));
        m_velocity         = Vector3f{ 0.0f, climb, 0.0f };
        position.y        += climb * dt;
    }

    PlayerMotorResult PlayerMotor::tick(Vector3f& position, const PlayerMotorInput& in, float dt, const PlayerGroundQuery& ground)
    {
        PlayerMotorResult result{};
        if (dt < 0.0f)
            dt = 0.0f;
        const float speedScale = Math::Clamp(in.speedScale, 0.0f, 1.0f);
        const float runScale   = clampSkill(in.runScale, kRunScaleMin, kRunScaleMax);
        const float swimScale  = clampSkill(in.swimScale, kSwimScaleMin, kSwimScaleMax);
        const float jumpScale  = clampSkill(in.jumpScale, kJumpScaleMin, kJumpScaleMax);

        if (m_jumpBuffer > 0.0f)
            m_jumpBuffer = Math::Max(0.0f, m_jumpBuffer - dt);
        if (in.jumpPressed && in.allowJumpBuffer)
            m_jumpBuffer = m_settings.jumpBuffer;

        m_tapAge += dt;
        noteDodgeTap(in, result);
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
            if (terrainDry(groundY, waterY))
                enterGrounded(position, groundY);
            else
            {
                moveHorizontal(position, in.wish, m_settings.swimSpeed * swimScale * speedScale, dt, 0.0f);
                position.y   = waterY + m_settings.swimOffset;
                m_velocity.y = 0.0f;
                m_jumpBuffer = 0.0f;
                return result;
            }
        }

        if (m_state == PlayerMoveState::Dodge && (m_dodgeLeft <= 0.0f || !in.allowDodge))
            m_state = PlayerMoveState::Grounded;

        if (m_state == PlayerMoveState::Grounded || m_state == PlayerMoveState::Dodge || m_state == PlayerMoveState::Crouch)
        {
            if (terrainWet(groundY, waterY))
            {
                enterSwim(position, waterY);
                result.splashed = true;
                moveHorizontal(position, in.wish, m_settings.swimSpeed * swimScale * speedScale, dt, 0.0f);
                return result;
            }

            float wallDist = 0.0f;
            if (in.jumpPressed && m_state != PlayerMoveState::Dodge && m_stamina > 0.15f && in.wish.MagnitudeSqrd() > 0.25f
                && findWall(ground, position, Vector3f{ in.wish.x, 0.0f, in.wish.z }, m_settings.wallReach, wallDist))
            {
                Vector3f dir{ in.wish.x, 0.0f, in.wish.z };
                dir.Normalize();
                enterWallRun(dir);
                result.jumped = true;
                return result;
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
            const float feetY     = groundY + m_settings.groundOffset;

            if (terrainWet(groundY, waterY))
            {
                enterSwim(position, waterY);
                result.splashed = true;
                return result;
            }

            if (position.y - feetY > m_settings.stepDown)
            {
                beginFalling();
                m_velocity.y -= m_settings.gravity * dt;
                position.y += m_velocity.y * dt;
                m_airTime += dt;
                return result;
            }
            else
            {
                position.y   = feetY;
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
        float    wallDist = 0.0f;
        if (!m_didFirstJump && m_jumpBuffer > 0.0f && m_coyote > 0.0f && in.allowJumpBuffer)
        {
            m_jumpBuffer = 0.0f;
            beginJump(jumpScale, result);
        }
        else if (in.jumpPressed && m_wallCooldown <= 0.0f && m_stamina > 0.0f && nearestWall(ground, position, in.wish, wallDir))
            wallJump(-wallDir, result);
        else if (m_wallCooldown <= 0.0f && m_stamina > 0.15f && m_velocity.y > -8.0f && in.wish.MagnitudeSqrd() > 0.25f
                 && findWall(ground, position, Vector3f{ in.wish.x, 0.0f, in.wish.z }, 0.5f, wallDist))
        {
            Vector3f dir{ in.wish.x, 0.0f, in.wish.z };
            dir.Normalize();
            enterWallRun(dir);
            return result;
        }
        else
            tryDoubleJump(in.jumpPressed, in.allowDoubleJump, jumpScale, result);

        if (m_state == PlayerMoveState::Jumping && m_velocity.y <= 0.0f)
            m_state = PlayerMoveState::Falling;

        groundY           = sampleGround(ground, position.x, position.z);
        const float feetY = groundY + m_settings.groundOffset;

        if (terrainWet(groundY, waterY) && position.y <= waterY + m_settings.swimOffset)
        {
            enterSwim(position, waterY);
            result.splashed = true;
            return result;
        }

        if (m_velocity.y <= 0.0f && position.y <= feetY)
        {
            result.landed = true;
            enterGrounded(position, groundY);
            if (m_jumpBuffer > 0.0f && in.allowJumpBuffer && !terrainWet(groundY, waterY))
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
