#pragma once

#include "Math/Vector3f.h"

#include <cstdint>

namespace Dark
{
    enum class PlayerMoveState : uint8_t
    {
        Grounded = 0,
        Jumping,
        Falling,
        Swimming,
    };

    struct PlayerMotorSettings
    {
        float walkSpeed           = 8.0f;
        float sprintSpeed         = 16.0f;
        float swimSpeed           = 5.0f;
        float airSpeed            = 2.4f;  // extra speed you can add along wish while airborne
        float airAccel            = 12.0f; // units/s^2 of air steering
        float gravity             = 24.0f;
        float jumpSpeed           = 8.0f;
        float doubleJumpSpeed     = 12.0f; // higher than jumpSpeed
        float doubleJumpWindow    = 0.28f; // second keystroke must arrive within this of first jump
        float doubleJumpMinDelay  = 0.05f; // must be a distinct second press
        float coyoteTime          = 0.10f;
        float jumpBuffer          = 0.12f;
        float stepDown            = 0.55f;
        float groundOffset        = 0.5f;
        float swimOffset          = 0.35f;
        float enterWaterDepth     = 0.25f;
        float leaveWaterHeight    = 0.35f;
    };

    struct PlayerMotorInput
    {
        Math::Vector3f wish{ 0.0f, 0.0f, 0.0f };
        bool           sprint      = false;
        bool           jumpPressed = false; // edge this tick
    };

    struct PlayerMotorResult
    {
        bool jumped       = false;
        bool doubleJumped = false;
        bool landed       = false;  // airborne -> ground
        bool splashed     = false;  // entered water
    };

    struct PlayerGroundQuery
    {
        float (*heightAt)(void* user, float x, float z) = nullptr;
        void* user                                      = nullptr;
        float waterY                                    = 0.0f;
    };

    // Possessed-body locomotion: ground, swim, jump, and a timed double jump.
    class PlayerMotor
    {
    public:
        explicit PlayerMotor(PlayerMotorSettings settings = {});

        const PlayerMotorSettings& settings() const { return m_settings; }
        PlayerMoveState            state() const { return m_state; }
        const Math::Vector3f&      velocity() const { return m_velocity; }
        bool                       didFirstJump() const { return m_didFirstJump; }
        bool                       didDoubleJump() const { return m_didDoubleJump; }
        float                      airTime() const { return m_airTime; }

        void reset();
        void setHorizontalVelocity(float x, float z);

        PlayerMotorResult tick(Math::Vector3f& position, const PlayerMotorInput& in, float dt, const PlayerGroundQuery& ground);

    private:
        float sampleGround(const PlayerGroundQuery& ground, float x, float z) const;
        bool  terrainWet(float groundY, float waterY) const;
        bool  terrainDry(float groundY, float waterY) const;
        void  moveHorizontal(Math::Vector3f& position, Math::Vector3f wish, float speed, float dt);
        void  applyAirControl(const Math::Vector3f& wish, float dt);
        void  enterGrounded(Math::Vector3f& position, float groundY);
        void  enterSwim(Math::Vector3f& position, float waterY);
        void  beginJump(PlayerMotorResult& result);
        void  beginFalling();
        bool  tryDoubleJump(bool jumpPressed, PlayerMotorResult& result);

        PlayerMotorSettings m_settings;
        PlayerMoveState     m_state          = PlayerMoveState::Grounded;
        Math::Vector3f      m_velocity       = Math::Vector3f{ 0.0f, 0.0f, 0.0f };
        float               m_airTime        = 0.0f;
        float               m_coyote         = 0.0f;
        float               m_jumpBuffer     = 0.0f;
        bool                m_didFirstJump   = false;
        bool                m_didDoubleJump  = false;
        bool                m_pendingDouble  = false;
    };

} // namespace Dark
