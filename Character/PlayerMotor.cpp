#include "Character/PlayerMotor.h"

#include "Math/MathHelper.h"

namespace Dark
{
    using Math::Vector3f;

    PlayerMotor::PlayerMotor(PlayerMotorSettings settings)
        : m_settings(settings)
        , m_velocity{ 0.0f, 0.0f, 0.0f }
    {
    }

    void PlayerMotor::reset()
    {
        m_state         = PlayerMoveState::Grounded;
        m_velocity      = Vector3f{ 0.0f, 0.0f, 0.0f };
        m_airTime       = 0.0f;
        m_coyote        = 0.0f;
        m_jumpBuffer    = 0.0f;
        m_didFirstJump  = false;
        m_didDoubleJump = false;
        m_pendingDouble = false;
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

    void PlayerMotor::moveHorizontal(Vector3f& position, Vector3f wish, float speed, float dt)
    {
        wish.y           = 0.0f;
        const float mag  = wish.Magnitude();
        if (mag > 1.0f)
            wish *= (1.0f / mag);
        if (mag < 1.0e-8f)
        {
            m_velocity.x = 0.0f;
            m_velocity.z = 0.0f;
            return;
        }
        m_velocity.x = wish.x * speed;
        m_velocity.z = wish.z * speed;
        position.x += m_velocity.x * dt;
        position.z += m_velocity.z * dt;
    }

    void PlayerMotor::applyAirControl(const Vector3f& wish, float dt)
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

        const float maxAir  = m_settings.airSpeed * mag;
        const float current = m_velocity.x * dir.x + m_velocity.z * dir.z;
        const float add     = maxAir - current;
        if (add <= 0.0f)
            return;

        float accel = m_settings.airAccel * dt;
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

    void PlayerMotor::beginJump(PlayerMotorResult& result)
    {
        m_state         = PlayerMoveState::Jumping;
        m_velocity.y    = m_settings.jumpSpeed;
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

    bool PlayerMotor::tryDoubleJump(bool jumpPressed, PlayerMotorResult& result)
    {
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

        m_velocity.y    = m_settings.doubleJumpSpeed;
        m_didDoubleJump = true;
        m_pendingDouble = false;
        m_state         = PlayerMoveState::Jumping;
        result.jumped   = true;
        result.doubleJumped = true;
        return true;
    }

    PlayerMotorResult PlayerMotor::tick(Vector3f& position, const PlayerMotorInput& in, float dt, const PlayerGroundQuery& ground)
    {
        PlayerMotorResult result{};
        if (dt < 0.0f)
            dt = 0.0f;

        if (m_jumpBuffer > 0.0f)
            m_jumpBuffer = Math::Max(0.0f, m_jumpBuffer - dt);
        if (in.jumpPressed)
            m_jumpBuffer = m_settings.jumpBuffer;

        const float waterY  = ground.waterY;
        float       groundY = sampleGround(ground, position.x, position.z);

        if (m_state == PlayerMoveState::Swimming)
        {
            if (terrainDry(groundY, waterY))
                enterGrounded(position, groundY);
            else
            {
                moveHorizontal(position, in.wish, m_settings.swimSpeed, dt);
                position.y   = waterY + m_settings.swimOffset;
                m_velocity.y = 0.0f;
                m_jumpBuffer = 0.0f;
                return result;
            }
        }

        if (m_state == PlayerMoveState::Grounded)
        {
            if (terrainWet(groundY, waterY))
            {
                enterSwim(position, waterY);
                result.splashed = true;
                moveHorizontal(position, in.wish, m_settings.swimSpeed, dt);
                return result;
            }

            const float speed = in.sprint ? m_settings.sprintSpeed : m_settings.walkSpeed;
            moveHorizontal(position, in.wish, speed, dt);
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
                if (m_jumpBuffer > 0.0f)
                {
                    m_jumpBuffer = 0.0f;
                    beginJump(result);
                    m_velocity.y -= m_settings.gravity * dt;
                    position.y += m_velocity.y * dt;
                    m_airTime += dt;
                }
                return result;
            }
        }

        applyAirControl(in.wish, dt);
        position.x += m_velocity.x * dt;
        position.z += m_velocity.z * dt;
        m_velocity.y -= m_settings.gravity * dt;
        position.y += m_velocity.y * dt;
        m_airTime += dt;
        if (m_coyote > 0.0f)
            m_coyote = Math::Max(0.0f, m_coyote - dt);

        if (!m_didFirstJump && m_jumpBuffer > 0.0f && m_coyote > 0.0f)
        {
            m_jumpBuffer = 0.0f;
            beginJump(result);
        }
        else
            tryDoubleJump(in.jumpPressed, result);

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
            if (m_jumpBuffer > 0.0f && !terrainWet(groundY, waterY))
            {
                m_jumpBuffer = 0.0f;
                beginJump(result);
                m_velocity.y -= m_settings.gravity * dt;
                position.y += m_velocity.y * dt;
                m_airTime += dt;
            }
        }

        return result;
    }

} // namespace Dark
