#pragma once

#include "Collision/HitResult.h"
#include "Math/AABox3f.h"
#include "Math/Ray3f.h"
#include "Math/Vector3f.h"

#include <cstdint>

namespace Dark
{

    enum class WeaponKind : uint8_t
    {
        Melee = 0,
        Projectile = 1,
    };

    struct WeaponHit
    {
        Math::Vector3f point{};
        Math::Vector3f normal{ 0.0f, 1.0f, 0.0f };
        Math::Vector3f direction{ 0.0f, 0.0f, 1.0f }; // incoming attack, attacker -> victim
        float          damage       = 0.0f;
        int            targetIndex  = -1;
        bool           hitTarget    = false;
        WeaponKind     weapon       = WeaponKind::Melee;
    };

    struct WeaponWorldQuery
    {
        Collision::RayHit3D (*raycastTerrain)(void* user, const Math::Ray3f& ray, float maxDistance) = nullptr;
        float (*heightAt)(void* user, float x, float z)                                              = nullptr;
        void* terrainUser                                                                            = nullptr;

        int (*targetCount)(void* user)                 = nullptr;
        bool (*targetAlive)(void* user, int index)     = nullptr;
        Math::Vector3f (*targetCenter)(void* user, int index) = nullptr;
        Math::Vector3f targetHalfExtents{ 1.0f, 1.0f, 1.0f };
        void*          targetUser = nullptr;

        float maxRange = 80.0f;
    };

    struct WeaponFireRequest
    {
        Math::Vector3f origin{};
        Math::Vector3f direction{};
        Math::Vector3f ownerPos{};
    };

    using WeaponHitFn = void (*)(void* user, const WeaponHit& hit);

    // Shared world tests used by melee and projectile weapons.
    int  weaponTargetCount(const WeaponWorldQuery& world);
    bool weaponTargetAlive(const WeaponWorldQuery& world, int index);
    bool weaponTargetCenter(const WeaponWorldQuery& world, int index, Math::Vector3f& out);
    Math::AABox3f weaponTargetBox(const WeaponWorldQuery& world, int index);
    Collision::RayHit3D weaponRaycastTerrain(const WeaponWorldQuery& world, const Math::Ray3f& ray, float maxDistance);
    float weaponHeightAt(const WeaponWorldQuery& world, float x, float z);
    bool weaponRaycastClosest(const WeaponWorldQuery& world, const Math::Ray3f& ray, float maxDistance, WeaponHit& out);
    bool weaponSweepClosest(const WeaponWorldQuery& world, const Math::Vector3f& start, const Math::Vector3f& delta, float radius, WeaponHit& out);

    class Weapon
    {
    public:
        virtual ~Weapon() = default;

        virtual WeaponKind  kind() const     = 0;
        virtual const char* name() const     = 0;
        virtual float       damage() const   = 0;
        virtual bool        canFire() const  = 0;

        // Returns true if a swing / shot started. Hits are reported through the listener.
        virtual bool fire(const WeaponFireRequest& req, const WeaponWorldQuery& world) = 0;
        virtual void tick(float dt, const WeaponWorldQuery& world)                     = 0;
        virtual void clear()                                                           = 0;

        void setHitListener(WeaponHitFn fn, void* user)
        {
            m_hitFn   = fn;
            m_hitUser = user;
        }

    protected:
        void emitHit(const WeaponHit& hit)
        {
            if (m_hitFn)
                m_hitFn(m_hitUser, hit);
        }

        WeaponHitFn m_hitFn   = nullptr;
        void*       m_hitUser = nullptr;
        float       m_cooldown = 0.0f;
    };

} // namespace Dark
