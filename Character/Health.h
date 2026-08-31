#pragma once

namespace Dark
{
    struct HealthSettings
    {
        float maxHp       = 100.0f;
        float regenPerSec = 8.0f;
        float regenDelay  = 3.5f;
    };

    // HP, damage, death, and delayed self-heal. Dead units do not regen.
    class Health
    {
    public:
        Health(HealthSettings settings = {});

        void reset();
        void revive(float hp = -1.0f);

        // Returns true if this hit caused death.
        bool applyDamage(float amount);
        void heal(float amount);
        void tick(float dt);

        float hp() const { return m_hp; }
        float maxHp() const { return m_settings.maxHp; }
        float ratio() const;
        float timeSinceDamage() const { return m_sinceDamage; }
        bool  alive() const { return m_hp > 0.0f; }
        bool  dead() const { return m_hp <= 0.0f; }

        const HealthSettings& settings() const { return m_settings; }

    private:
        HealthSettings m_settings;
        float          m_hp          = 100.0f;
        float          m_sinceDamage = 0.0f;
    };

} // namespace Dark
