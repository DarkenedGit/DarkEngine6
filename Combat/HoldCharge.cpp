#include "Combat/HoldCharge.h"

namespace Dark::Combat
{

    void HoldCharge::tick(float dt, bool held, HoldChargeEvent& out)
    {
        out = {};
        if (dt < 0.0f)
            dt = 0.0f;

        if (!held)
        {
            if (m_phase == HoldChargePhase::Charged)
                out.releasedCharged = true;
            else if (m_phase == HoldChargePhase::Holding)
                out.releasedQuick = true;
            m_phase = HoldChargePhase::Idle;
            m_held  = 0.0f;
            return;
        }

        if (m_phase == HoldChargePhase::Idle)
            m_phase = HoldChargePhase::Holding;
        m_held += dt;

        float window = m_settings.windowSeconds;
        if (window < 0.0f)
            window = 0.0f;
        if (m_phase == HoldChargePhase::Holding && m_held >= window)
        {
            m_phase          = HoldChargePhase::Charged;
            out.becameCharged = true;
        }
    }

    void HoldCharge::reset()
    {
        m_phase = HoldChargePhase::Idle;
        m_held  = 0.0f;
    }

    void stepPlayerCharge(HoldCharge& attack,
                          HoldCharge& block,
                          const PlayerChargeSettings& settings,
                          const PlayerChargeInput& in,
                          PlayerChargeStep& out)
    {
        out = {};
        attack.settings().windowSeconds = settings.attackWindowSeconds;
        block.settings().windowSeconds  = settings.blockWindowSeconds;

        if (!in.canChargeAttack)
            attack.reset();
        else
        {
            HoldChargeEvent ev{};
            attack.tick(in.dt, in.attackDown, ev);
            out.fireQuick    = ev.releasedQuick;
            out.fireCharged  = ev.releasedCharged;
        }

        HoldChargeEvent blockEv{};
        if (!in.canHoldShield)
            block.reset();
        else
            block.tick(in.dt, in.holdShield, blockEv);

        out.openChargedParry   = blockEv.releasedCharged;
        out.wantShield          = in.holdShield || blockEv.releasedCharged || in.chargedParryUp;
        out.shieldChargeAlpha  = block.isCharged() ? 1.0f : 0.0f;
    }

} // namespace Dark::Combat
