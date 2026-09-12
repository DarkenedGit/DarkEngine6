#pragma once

#include "Weapons/MeleeWeapon.h"
#include "Weapons/ProjectileWeapon.h"

namespace Dark
{

    // Two-slot player loadout: 1 = melee, 2 = projectile.
    class WeaponLoadout
    {
    public:
        WeaponLoadout();

        WeaponLoadout(const WeaponLoadout&)            = delete;
        WeaponLoadout& operator=(const WeaponLoadout&) = delete;

        void setHitListener(WeaponHitFn fn, void* user);

        MeleeWeapon&             melee() { return m_melee; }
        const MeleeWeapon&       melee() const { return m_melee; }
        ProjectileWeapon&        projectile() { return m_projectile; }
        const ProjectileWeapon&  projectile() const { return m_projectile; }

        Weapon&       active();
        const Weapon& active() const;
        WeaponKind    activeKind() const { return m_slot == 0 ? WeaponKind::Melee : WeaponKind::Projectile; }
        int           slot() const { return m_slot; } // 0 = melee (key 1), 1 = projectile (key 2)

        // slot is 0 or 1. Returns true if the active weapon changed.
        bool selectSlot(int slot);
        bool selectMelee() { return selectSlot(0); }
        bool selectProjectile() { return selectSlot(1); }

        bool fire(const WeaponFireRequest& req, const WeaponWorldQuery& world);
        void tick(float dt, const WeaponWorldQuery& world);
        void clear();

    private:
        MeleeWeapon      m_melee;
        ProjectileWeapon m_projectile;
        int              m_slot = 0;
    };

} // namespace Dark
