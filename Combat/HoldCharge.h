#pragma once

#include <cstdint>

namespace Dark::Combat
{

    // Hold past windowSeconds to charge. Release before that is a tap; release after plays the charged action.
    struct HoldChargeSettings
    {
        float windowSeconds = 0.40f;
    };

    enum class HoldChargePhase : uint8_t
    {
        Idle,
        Holding,
        Charged,
    };

    struct HoldChargeEvent
    {
        bool releasedQuick    = false;
        bool releasedCharged  = false;
        bool becameCharged    = false;
    };

    class HoldCharge
    {
    public:
        void setSettings(const HoldChargeSettings& settings) { m_settings = settings; }
        HoldChargeSettings&       settings() { return m_settings; }
        const HoldChargeSettings& settings() const { return m_settings; }

        // held is the button this frame. A forced cancel should call reset() instead of ticking held=false,
        // so an interrupt does not count as a release.
        void tick(float dt, bool held, HoldChargeEvent& out);

        HoldChargePhase phase() const { return m_phase; }
        float           heldSeconds() const { return m_held; }
        bool            isCharged() const { return m_phase == HoldChargePhase::Charged; }

        void reset();

    private:
        HoldChargeSettings m_settings{};
        HoldChargePhase    m_phase = HoldChargePhase::Idle;
        float              m_held  = 0.0f;
    };

    // Shared player tuning. Attack and block each have their own hold window.
    struct PlayerChargeSettings
    {
        float attackWindowSeconds       = 0.40f;
        float attackDamageScale         = 1.85f;
        float blockWindowSeconds        = 0.45f;
        float parryWindowSeconds        = 0.18f; // opens when the shield finishes rising
        float chargedParryWindowSeconds = 0.22f; // opens when a charged block is released
        float parryStunSeconds          = 0.90f;
        float chargedParryStunSeconds   = 1.80f; // knockdown slam; longer than parryStunSeconds
    };

    struct PlayerChargeInput
    {
        float dt              = 0.0f;
        bool  attackDown      = false;
        bool  canChargeAttack = false;
        bool  holdShield      = false;
        bool  canHoldShield   = false;
        bool  chargedParryUp  = false; // charged parry window still open from an earlier release
    };

    struct PlayerChargeStep
    {
        bool  fireQuick          = false;
        bool  fireCharged        = false;
        bool  wantShield         = false;
        bool  openChargedParry   = false;
        float shieldChargeAlpha = 0.0f;
    };

    // Attack release is reported here and fired by the host. A charged block release asks for a parry window.
    // Interrupts (can* false) drop the hold without firing.
    void stepPlayerCharge(HoldCharge& attack,
                          HoldCharge& block,
                          const PlayerChargeSettings& settings,
                          const PlayerChargeInput& in,
                          PlayerChargeStep& out);

} // namespace Dark::Combat
