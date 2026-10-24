#include "Weapons/WeaponLoadout.h"

#include "Gameplay/ThrowableBelt.h"
#include "Gameplay/ThrowableCatalog.h"

#include <string>
#include <string_view>

namespace Dark
{
    namespace
    {

        std::string firstCarried(const ThrowableBelt& belt)
        {
            for (const ThrowableDef& item : throwableCatalog().items())
            {
                if (belt.count(item.id) > 0)
                    return item.id;
            }
            return {};
        }

        std::string nextCarried(const ThrowableBelt& belt, std::string_view after)
        {
            const std::vector<ThrowableDef>& items = throwableCatalog().items();
            const int                        n     = static_cast<int>(items.size());
            if (n <= 0)
                return {};
            int start = 0;
            for (int i = 0; i < n; ++i)
            {
                if (items[static_cast<size_t>(i)].id == after)
                {
                    start = i + 1;
                    break;
                }
            }
            for (int step = 0; step < n; ++step)
            {
                const ThrowableDef& item = items[static_cast<size_t>((start + step) % n)];
                if (belt.count(item.id) > 0)
                    return item.id;
            }
            return {};
        }

    } // namespace

    WeaponLoadout::WeaponLoadout() = default;

    void WeaponLoadout::setHitListener(WeaponHitFn fn, void* user)
    {
        m_melee.setHitListener(fn, user);
        m_projectile.setHitListener(fn, user);
    }

    Weapon& WeaponLoadout::active()
    {
        // Slot 2 is not a rifle. Callers check activeKind() before fire().
        if (m_slot == 1)
            return m_projectile;
        return m_melee;
    }

    const Weapon& WeaponLoadout::active() const
    {
        if (m_slot == 1)
            return m_projectile;
        return m_melee;
    }

    bool WeaponLoadout::selectSlot(int slot)
    {
        if (slot > 2)
            return false;
        const int next = slot < 0 ? 0 : slot;
        if (next == m_slot)
            return false;
        m_slot = next;
        return true;
    }

    bool WeaponLoadout::selectSlot(int slot, ThrowableBelt& belt)
    {
        if (slot != 2)
            return selectSlot(slot);

        if (!belt.any())
        {
            if (m_slot != 2)
                return false;
            belt.setHand("");
            return selectSlot(0);
        }

        const std::string held(belt.hand());
        const bool        heldOk = !held.empty() && belt.count(held) > 0;
        if (m_slot != 2)
        {
            if (!heldOk)
            {
                const std::string first = firstCarried(belt);
                if (first.empty() || !belt.setHand(first))
                    return false;
            }
            return selectSlot(2);
        }

        const std::string next = nextCarried(belt, heldOk ? std::string_view{ held } : std::string_view{});
        if (next.empty() || next == held)
            return false;
        return belt.setHand(next);
    }

    bool WeaponLoadout::spendThrowable(ThrowableBelt& belt)
    {
        const std::string held(belt.hand());
        if (held.empty() || belt.count(held) <= 0)
            return false;
        const int left = belt.consume(held);
        if (left < 0)
            return false;
        if (left > 0)
            return true;

        const std::string next = nextCarried(belt, held);
        if (!next.empty())
        {
            belt.setHand(next);
            return true;
        }

        belt.setHand("");
        selectSlot(0);
        return true;
    }

    bool WeaponLoadout::fire(const WeaponFireRequest& req, const WeaponWorldQuery& world)
    {
        if (activeKind() == WeaponKind::Throwable)
            return false;
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
