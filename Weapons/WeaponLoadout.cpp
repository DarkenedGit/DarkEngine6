#include "Weapons/WeaponLoadout.h"

namespace Dark
{
    WeaponLoadout::WeaponLoadout() = default;

    void WeaponLoadout::setHitListener(WeaponHitFn fn, void* user)
    {
        m_melee.setHitListener(fn, user);
        m_projectile.setHitListener(fn, user);
    }

    Weapon& WeaponLoadout::active()
    {
        if (m_slot == 0)
            return m_melee;
        return m_projectile;
    }

    const Weapon& WeaponLoadout::active() const
    {
        if (m_slot == 0)
            return m_melee;
        return m_projectile;
    }

    bool WeaponLoadout::selectSlot(int slot)
    {
        const int clamped = slot <= 0 ? 0 : 1;
        if (clamped == m_slot)
            return false;
        m_slot = clamped;
        return true;
    }

    bool WeaponLoadout::fire(const WeaponFireRequest& req, const WeaponWorldQuery& world)
    {
        return active().fire(req, world);
    }

    void WeaponLoadout::tick(float dt, const WeaponWorldQuery& world)
    {
        m_melee.tick(dt, world);
        m_projectile.tick(dt, world);
    }

    void WeaponLoadout::clear()
    {
        m_melee.clear();
        m_projectile.clear();
    }

} // namespace Dark
