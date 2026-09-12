#pragma once

#include "Math/Vector3f.h"

namespace Dark
{
    struct HitReactionSettings
    {
        float stunSeconds        = 0.30f; // 0 = can still steer while pushed
        float knockbackDistance  = 1.5f;  // metres along the incoming hit; 0 = no push
        float knockbackSeconds   = 0.16f; // 0 = apply the whole distance on the next tick
        bool  horizontalOnly     = true;  // ignore hit Y so grounded units stay on the floor
    };

    // Stun lock + knockback slide from a weapon hit (or any incoming direction).
    class HitReaction
    {
    public:
        HitReaction(HitReactionSettings settings = {});

        void                       setSettings(const HitReactionSettings& settings);
        const HitReactionSettings& settings() const { return m_settings; }
        HitReactionSettings&       settings() { return m_settings; }

        // Incoming attack direction (attacker toward victim). Replaces any in-flight reaction.
        void apply(const Math::Vector3f& hitDirection);
        void reset();

        // Counts down stun and returns this frame's knockback displacement.
        Math::Vector3f tick(float dt);

        bool  stunned() const { return m_stunLeft > 0.0f; }
        bool  knockingBack() const { return m_knockLeft > 0.0f; }
        float stunRemaining() const { return m_stunLeft; }
        float knockbackRemaining() const { return m_knockLeft; }

    private:
        HitReactionSettings m_settings;
        float               m_stunLeft      = 0.0f;
        float               m_knockLeft     = 0.0f;
        float               m_knockTimeLeft = 0.0f;
        Math::Vector3f      m_knockDir{ 0.0f, 0.0f, 1.0f };
    };

} // namespace Dark
