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
        Dodge,
        Crouch,
        WallRun,
        Hang,
    };

    enum class MoveCardinal : uint8_t
    {
        None = 0,
        Forward,
        Back,
        Left,
        Right,
    };

    struct PlayerMotorSettings
    {
        float walkSpeed           = 3.6f;
        float crouchSpeed         = 1.6f; // slower than walkSpeed
        float sprintSpeed         = 7.0f;
        float swimSpeed           = 5.0f;
        float airSpeed            = 1.2f;  // extra speed you can add along wish while airborne
        float airAccel            = 12.0f; // units/s^2 of air steering
        float acceleration        = 0.0f;  // m/s^2 toward walk, crouch, and sprint on the ground. 0 snaps. Dodge and swim ignore it.
        float gravity             = 24.0f;
        float jumpSpeed           = 12.0f;
        float doubleJumpSpeed     = 16.0f; // higher than jumpSpeed
        float doubleJumpWindow    = 0.28f; // second keystroke must arrive within this of first jump
        float doubleJumpMinDelay  = 0.05f; // must be a distinct second press
        float coyoteTime          = 0.10f;
        float jumpBuffer          = 0.12f;
        float dodgeTapWindow      = 0.15f; // second press of the same direction must land within this
        float dodgeDuration       = 0.50f;
        float dodgeSpeed          = 10.0f; // planar burst, faster than sprint
        float dodgeAnimSpeed      = 2.0f;  // locomotion clip playback while dodging
        float stepDown            = 0.55f;
        float groundOffset        = 0.5f;
        float swimOffset          = 0.35f;
        float enterWaterDepth     = 0.25f;
        float leaveWaterHeight    = 0.35f;
        float wallReach           = 0.7f;
        float wallJumpUp          = 9.0f;
        float wallJumpOut         = 6.0f;
        float wallJumpCooldown    = 0.3f;
        float wallRunSpeed        = 4.5f;
        float handReach           = 1.9f;
        float staminaRegen        = 0.35f;
        float wallRunDrain        = 0.45f;
        float hangDrain           = 0.08f;
        float wallJumpCost        = 0.12f;
    };

    struct PlayerMotorInput
    {
        Math::Vector3f wish{ Math::Vect3f::ZERO };
        bool           sprint           = false;
        bool           crouch           = false; // held; grounded only, and it wins over sprint
        bool           jumpPressed      = false; // edge this tick
        bool           allowDoubleJump  = true;
        bool           allowJumpBuffer  = true; // land/coyote/grounded consume of m_jumpBuffer
        float          airControlScale  = 1.0f; // multiplies airSpeed/airAccel in applyAirControl
        float          speedScale       = 1.0f; // multiplies walk/sprint/swim/airSpeed/airAccel; clamped [0, 1]
        float          runScale         = 1.0f; // walk + sprint only, clamped [1, 1.20]
        float          swimScale        = 1.0f; // swimSpeed only, clamped [1, 1.25]
        float          jumpScale        = 1.0f; // jumpSpeed + doubleJumpSpeed only, clamped [1, 1.15]
        bool           allowDodge       = true;
        MoveCardinal   dodgeTap         = MoveCardinal::None; // direction key edge this frame
        Math::Vector3f dodgeTapWish{ Math::Vect3f::ZERO };    // world XZ of that edge
    };

    struct PlayerMotorResult
    {
        bool jumped       = false;
        bool doubleJumped = false;
        bool landed       = false;  // airborne -> ground
        bool splashed     = false;  // entered water
        bool dodged       = false;  // entered Dodge this tick
        bool climbed      = false;
    };

    struct PlayerGroundQuery
    {
        float (*heightAt)(void* user, float x, float z) = nullptr;
        void* user                                      = nullptr;
        float waterY                                    = 0.0f;
        bool (*wallAt)(void* user, const Math::Vector3f& from, const Math::Vector3f& dir, float maxDist, float& outDist) = nullptr;
        bool (*ledgeAt)(void* user, float x, float z, float fromY, float& outTopY)                                   = nullptr;
        void* wallUser                                                                                              = nullptr;
    };

    // Possessed-body locomotion: ground, swim, jump, and a timed double jump.
    class PlayerMotor
    {
    public:
        explicit PlayerMotor(PlayerMotorSettings settings = {});

        const PlayerMotorSettings& settings() const { return m_settings; }
        PlayerMotorSettings&       settings() { return m_settings; }
        PlayerMoveState            state() const { return m_state; }
        const Math::Vector3f&      velocity() const { return m_velocity; }
        bool                       didFirstJump() const { return m_didFirstJump; }
        bool                       didDoubleJump() const { return m_didDoubleJump; }
        float                      airTime() const { return m_airTime; }
        float                      coyoteLeft() const { return m_coyote; }
        float                      dodgeTimeLeft() const { return m_dodgeLeft; }
        const Math::Vector3f&      dodgeWish() const { return m_dodgeWish; }
        float                      stamina() const { return m_stamina; }

        void reset();
        void clearJumpBuffer();
        void setHorizontalVelocity(float x, float z);
        void restoreProgress(PlayerMoveState state, const Math::Vector3f& velocity, float airTime, float coyote, bool didFirstJump, bool didDoubleJump, float dodgeLeft, const Math::Vector3f& dodgeWish)
        {
            m_state         = state;
            m_velocity      = velocity;
            m_airTime       = airTime;
            m_coyote        = coyote;
            m_didFirstJump  = didFirstJump;
            m_didDoubleJump = didDoubleJump;
            m_dodgeLeft     = dodgeLeft;
            m_dodgeWish     = dodgeWish;
        }

        PlayerMotorResult tick(Math::Vector3f& position, const PlayerMotorInput& in, float dt, const PlayerGroundQuery& ground);

    private:
        float sampleGround(const PlayerGroundQuery& ground, float x, float z) const;
        bool  terrainWet(float groundY, float waterY) const;
        bool  terrainDry(float groundY, float waterY) const;
        void  moveHorizontal(Math::Vector3f& position, Math::Vector3f wish, float speed, float dt, float acceleration);
        void  applyAirControl(const Math::Vector3f& wish, float dt, float airControlScale);
        void  enterGrounded(Math::Vector3f& position, float groundY);
        void  enterSwim(Math::Vector3f& position, float waterY);
        void  beginJump(float jumpScale, PlayerMotorResult& result);
        void  beginFalling();
        void  noteDodgeTap(const PlayerMotorInput& in, PlayerMotorResult& result);
        bool  tryDoubleJump(bool jumpPressed, bool allowDoubleJump, float jumpScale, PlayerMotorResult& result);
        bool  findWall(const PlayerGroundQuery& ground, const Math::Vector3f& position, const Math::Vector3f& dir, float reach, float& dist) const;
        bool  findLedge(const PlayerGroundQuery& ground, const Math::Vector3f& position, const Math::Vector3f& dir, float wallDist, float& topY) const;
        bool  nearestWall(const PlayerGroundQuery& ground, const Math::Vector3f& position, const Math::Vector3f& wish, Math::Vector3f& dir) const;
        void  wallJump(const Math::Vector3f& awayFromWall, PlayerMotorResult& result);
        void  enterWallRun(const Math::Vector3f& dir);
        void  climbOnto(Math::Vector3f& position, float topY, PlayerMotorResult& result);
        void  tickWall(Math::Vector3f& position, const PlayerMotorInput& in, float dt, const PlayerGroundQuery& ground, PlayerMotorResult& result);

        PlayerMotorSettings m_settings;
        PlayerMoveState     m_state          = PlayerMoveState::Grounded;
        Math::Vector3f      m_velocity       = Math::Vector3f{ 0.0f, 0.0f, 0.0f };
        float               m_airTime        = 0.0f;
        float               m_coyote         = 0.0f;
        float               m_jumpBuffer     = 0.0f;
        bool                m_didFirstJump   = false;
        bool                m_didDoubleJump  = false;
        bool                m_pendingDouble  = false;
        MoveCardinal        m_lastTap        = MoveCardinal::None;
        float               m_tapAge         = 1.0f;
        float               m_dodgeLeft      = 0.0f;
        Math::Vector3f      m_dodgeWish{ 0.0f, 0.0f, 0.0f };
        float               m_stamina        = 1.0f;
        float               m_wallCooldown   = 0.0f;
        float               m_ledgeY         = 0.0f;
        Math::Vector3f      m_wallDir{ 0.0f, 0.0f, 0.0f };
    };

    // Planar speed in m/s. Non-positive acceleration snaps to desired so a missing tune keeps the old step.
    inline float approachSpeed(float current, float desired, float acceleration, float dt)
    {
        if (desired < 0.0f)
            desired = 0.0f;
        if (!(acceleration > 0.0f) || !(dt > 0.0f))
            return desired;
        const float delta = desired - current;
        const float step  = acceleration * dt;
        if (delta > step)
            return current + step;
        if (delta < -step)
            return current - step;
        return desired;
    }

    inline MoveCardinal moveCardinalFromEdges(bool forward, bool back, bool left, bool right)
    {
        const int count = (forward ? 1 : 0) + (back ? 1 : 0) + (left ? 1 : 0) + (right ? 1 : 0);
        if (count != 1)
            return MoveCardinal::None;
        if (forward)
            return MoveCardinal::Forward;
        if (back)
            return MoveCardinal::Back;
        if (left)
            return MoveCardinal::Left;
        return MoveCardinal::Right;
    }

    inline Math::Vector3f moveCardinalWish(MoveCardinal dir, const Math::Vector3f& forward, const Math::Vector3f& right)
    {
        switch (dir)
        {
        case MoveCardinal::Forward: return Math::Vector3f{ forward.x, 0.0f, forward.z };
        case MoveCardinal::Back:    return Math::Vector3f{ -forward.x, 0.0f, -forward.z };
        case MoveCardinal::Right:   return Math::Vector3f{ right.x, 0.0f, right.z };
        case MoveCardinal::Left:    return Math::Vector3f{ -right.x, 0.0f, -right.z };
        case MoveCardinal::None:    break;
        }
        return Math::Vector3f{ 0.0f, 0.0f, 0.0f };
    }

} // namespace Dark
