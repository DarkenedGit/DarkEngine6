#pragma once

#include "Weapons/MeleeWeapon.h"
#include "Weapons/ProjectileWeapon.h"

namespace Dark
{

    class ThrowableBelt;

    // Player loadout: 0 melee, 1 projectile, 2 throwable.
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
        WeaponKind    activeKind() const
        {
            if (m_slot == 1)
                return WeaponKind::Projectile;
            if (m_slot == 2)
                return WeaponKind::Throwable;
            return WeaponKind::Melee;
        }
        int slot() const { return m_slot; } // 0 melee, 1 projectile, 2 throwable

        // 0, 1, or 2. Other values do not change the slot. True when the slot changed.
        bool selectSlot(int slot);
        // Player press. Slot 2 is refused while every count is zero, and a press that finds
        // slot 2 already active cycles to the next carried type. The first press does not.
        bool selectSlot(int slot, ThrowableBelt& belt);
        bool selectMelee() { return selectSlot(0); }
        bool selectProjectile() { return selectSlot(1); }

        // Spends one of the type in hand. The last of a type brings the next carried type to hand.
        // The last of every type selects the sword and clears the hand.
        bool spendThrowable(ThrowableBelt& belt);

        bool fire(const WeaponFireRequest& req, const WeaponWorldQuery& world);
        void tick(float dt, const WeaponWorldQuery& world);
        void clear();

    private:
        MeleeWeapon      m_melee;
        ProjectileWeapon m_projectile;
        int              m_slot = 0;
    };

} // namespace Dark
