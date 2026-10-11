#pragma once

#include "Weapons/Weapon.h"

namespace Dark
{

    struct MeleeWeaponDesc
    {
        const char* name     = "Melee";
        float       damage   = 16.0f;
        float       range    = 2.7f;
        float       minDot   = 0.25f;
        float       cooldown = 0.45f;
        float       hitDelay = 0.0f;
        bool        singleTarget = false;
    };

    class MeleeWeapon : public Weapon
    {
    public:
        MeleeWeapon();
        explicit MeleeWeapon(const MeleeWeaponDesc& desc);

        void                   setDesc(const MeleeWeaponDesc& desc);
        const MeleeWeaponDesc& desc() const { return m_desc; }

        WeaponKind  kind() const override { return WeaponKind::Melee; }
        const char* name() const override { return m_desc.name ? m_desc.name : "Melee"; }
        float       damage() const override { return m_desc.damage; }
        bool        canFire() const override { return m_cooldown <= 0.0f; }

        bool fire(const WeaponFireRequest& req, const WeaponWorldQuery& world) override;
        void tick(float dt, const WeaponWorldQuery& world) override;
        void clear() override;

        // Best target in reach for this swing (closest to the look direction), or -1.
        int pickTarget(const WeaponFireRequest& req, const WeaponWorldQuery& world) const;

    private:
        void strike(const WeaponFireRequest& req, const WeaponWorldQuery& world);

        MeleeWeaponDesc   m_desc{};
        WeaponFireRequest m_pending{};
        float             m_pendingLeft = -1.0f;
    };

} // namespace Dark
