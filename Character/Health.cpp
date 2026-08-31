#include "Character/Health.h"

#include "Math/MathHelper.h"

namespace Dark
{
    Health::Health(HealthSettings settings)
        : m_settings(settings)
    {
        if (m_settings.maxHp < 1.0f)
            m_settings.maxHp = 1.0f;
        if (m_settings.regenPerSec < 0.0f)
            m_settings.regenPerSec = 0.0f;
        if (m_settings.regenDelay < 0.0f)
            m_settings.regenDelay = 0.0f;
        m_hp          = m_settings.maxHp;
        m_sinceDamage = m_settings.regenDelay;
    }

    void Health::reset()
    {
        m_hp          = m_settings.maxHp;
        m_sinceDamage = m_settings.regenDelay;
    }

    void Health::revive(float hp)
    {
        if (hp < 0.0f)
            hp = m_settings.maxHp;
        m_hp          = Math::Clamp(hp, 0.0f, m_settings.maxHp);
        m_sinceDamage = m_settings.regenDelay;
    }

    bool Health::applyDamage(float amount)
    {
        if (amount <= 0.0f || m_hp <= 0.0f)
            return false;
        m_hp -= amount;
        m_sinceDamage = 0.0f;
        if (m_hp > 0.0f)
            return false;
        m_hp = 0.0f;
        return true;
    }

    void Health::heal(float amount)
    {
        if (amount <= 0.0f || m_hp <= 0.0f)
            return;
        m_hp += amount;
        if (m_hp > m_settings.maxHp)
            m_hp = m_settings.maxHp;
    }

    void Health::tick(float dt)
    {
        if (dt < 0.0f)
            dt = 0.0f;
        if (m_hp <= 0.0f)
            return;
        m_sinceDamage += dt;
        if (m_sinceDamage < m_settings.regenDelay || m_hp >= m_settings.maxHp)
            return;
        m_hp += m_settings.regenPerSec * dt;
        if (m_hp > m_settings.maxHp)
            m_hp = m_settings.maxHp;
    }

    float Health::ratio() const
    {
        if (m_settings.maxHp <= 1.0e-6f)
            return 0.0f;
        const float r = m_hp / m_settings.maxHp;
        if (r < 0.0f)
            return 0.0f;
        if (r > 1.0f)
            return 1.0f;
        return r;
    }

} // namespace Dark
