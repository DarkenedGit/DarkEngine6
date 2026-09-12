#include "Character/HitReaction.h"

#include "Math/MathHelper.h"

namespace Dark
{
    using Math::Vector3f;

    HitReaction::HitReaction(HitReactionSettings settings)
        : m_settings(settings)
        , m_knockDir{ 0.0f, 0.0f, 1.0f }
    {
        if (m_settings.stunSeconds < 0.0f)
            m_settings.stunSeconds = 0.0f;
        if (m_settings.knockbackDistance < 0.0f)
            m_settings.knockbackDistance = 0.0f;
        if (m_settings.knockbackSeconds < 0.0f)
            m_settings.knockbackSeconds = 0.0f;
    }

    void HitReaction::setSettings(const HitReactionSettings& settings)
    {
        m_settings = settings;
        if (m_settings.stunSeconds < 0.0f)
            m_settings.stunSeconds = 0.0f;
        if (m_settings.knockbackDistance < 0.0f)
            m_settings.knockbackDistance = 0.0f;
        if (m_settings.knockbackSeconds < 0.0f)
            m_settings.knockbackSeconds = 0.0f;
    }

    void HitReaction::apply(const Vector3f& hitDirection)
    {
        Vector3f dir = hitDirection;
        if (m_settings.horizontalOnly)
            dir.y = 0.0f;

        m_stunLeft = m_settings.stunSeconds;

        if (m_settings.knockbackDistance <= 0.0f || dir.MagnitudeSqrd() < 1.0e-8f)
        {
            m_knockLeft     = 0.0f;
            m_knockTimeLeft = 0.0f;
            return;
        }

        dir.Normalize();
        m_knockDir      = dir;
        m_knockLeft     = m_settings.knockbackDistance;
        m_knockTimeLeft = m_settings.knockbackSeconds;
    }

    void HitReaction::reset()
    {
        m_stunLeft      = 0.0f;
        m_knockLeft     = 0.0f;
        m_knockTimeLeft = 0.0f;
    }

    Vector3f HitReaction::tick(float dt)
    {
        if (dt < 0.0f)
            dt = 0.0f;
        if (m_stunLeft > 0.0f)
            m_stunLeft = Math::Max(0.0f, m_stunLeft - dt);
        if (m_knockLeft <= 0.0f || dt <= 0.0f)
            return Vector3f{ 0.0f, 0.0f, 0.0f };

        float step = m_knockLeft;
        if (m_knockTimeLeft > 1.0e-6f)
        {
            step = m_knockLeft * (dt / m_knockTimeLeft);
            if (step > m_knockLeft)
                step = m_knockLeft;
            m_knockTimeLeft = Math::Max(0.0f, m_knockTimeLeft - dt);
        }
        else
            m_knockTimeLeft = 0.0f;

        m_knockLeft -= step;
        if (m_knockLeft < 0.0f)
            m_knockLeft = 0.0f;
        return m_knockDir * step;
    }

} // namespace Dark
