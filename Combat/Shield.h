#pragma once

#include "ECS/Entity.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"

namespace Dark
{
    class World;
}

namespace Dark::Combat
{

    struct DefenseComponent;
    struct PlayerChargeSettings;

    // Held guard on the off hand. The weapon stays equipped.
    // The flashlight and the shield share that hand: only one is active.
    struct ShieldSettings
    {
        float raiseSeconds   = 0.2f;  // hip to guard; blocking starts when this finishes. 0 snaps
        float moveSpeedScale = 0.55f; // speed multiplier while fully raised (1 = full, 0 = stopped)
        float blockArcDeg    = 120.0f; // total frontal degrees that stop a hit
    };

    enum class ShieldPhase : uint8_t
    {
        Down,
        Raising,
        Up,
        Lowering,
    };

    class Shield
    {
    public:
        void setSettings(const ShieldSettings& settings) { m_settings = settings; }
        ShieldSettings&       settings() { return m_settings; }
        const ShieldSettings& settings() const { return m_settings; }

        // wantUp is held. Releasing starts the lower over raiseSeconds.
        void tick(float dt, bool wantUp);

        ShieldPhase phase() const { return m_phase; }
        float       alpha() const { return m_alpha; } // 0 lowered, 1 fully raised
        bool        blocking() const { return m_phase == ShieldPhase::Up; }
        // 1 while lowered, moveSpeedScale while up, blended while the shield is moving.
        float speedScale() const;

        void reset();

    private:
        ShieldSettings m_settings{};
        ShieldPhase    m_phase = ShieldPhase::Down;
        float          m_alpha = 0.0f;
    };

    struct OffhandState
    {
        bool   lightOn      = true;
        bool   lightPending = false; // turn the light on once the shield has finished lowering
        Shield shield{};

        // wantShield wins over toggleLight on the same frame.
        void tick(float dt, bool wantShield, bool toggleLight);
        void reset();
    };

    // Local pose in the player frame: +Y up, +Z forward, +X right. Feet at the origin.
    // The guard sits on the player's left so a shoulder camera still sees down the aim.
    struct ShieldLocalPose
    {
        Math::Vector3f   position{};
        Math::Quaternion rotation{};
    };

    ShieldLocalPose shieldLocalPose(float alpha);

    // Raised shield negates hits in the arc. Stamina is not spent; the guard holds while it is up.
    void syncShieldDefense(DefenseComponent& defense, const Shield& shield, float facingYawRad);

    // Opens a frontal parry window. Charged is the release of a held block and slams on a successful parry.
    void openShieldParry(DefenseComponent& defense, bool charged, const PlayerChargeSettings& settings);

    // Gives the player a defense and a full block (no chip) for the raised shield.
    void equipPlayerShield(World& world, Entity player);

} // namespace Dark::Combat
