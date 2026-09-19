#pragma once

#include "Combat/DamageEvent.h"
#include "Combat/HitSet.h"
#include "Combat/JumpAttackDef.h"
#include "ECS/Entity.h"
#include "Math/Vector3f.h"
#include "Weapons/Weapon.h"

#include <cstddef>
#include <cstdint>

namespace Dark::Combat
{

    enum class JumpAttackPhase : uint8_t
    {
        Idle = 0,
        Telegraph,
        Leap,
        Connected,
        Pound,
        Recover
    };

    enum class JumpAttackCancel : uint8_t
    {
        NoPound = 0,
        ForceIdle
    };

    struct JumpAttackBegin
    {
        Entity         attacker{};
        Math::Vector3f position{};
        Math::Vector3f lookFlat{ 0.0f, 0.0f, 1.0f };
        Math::Vector3f velocity{};
        Entity         intendedTarget{};
        Math::Vector3f intendedTargetPos{};
        float          heightAboveGround = 0.0f;
        float          airTime           = 0.0f;
        uint8_t        team              = 0;
    };

    class JumpAttack
    {
    public:
        explicit JumpAttack(const JumpAttackDef& def = {});
        void                 setDef(const JumpAttackDef& def);
        const JumpAttackDef& def() const;

        JumpAttackPhase      phase() const;
        bool                 busy() const;
        bool                 inAirCommit() const;
        Entity               connectedTarget() const;
        float                connectWindowLeft() const;
        float                cooldownLeft() const;
        const Math::Vector3f& velocity() const;
        size_t               hitCount() const;

        bool begin(const JumpAttackBegin& req);
        void onLanded(const Math::Vector3f& landPos);
        void onSplashed();
        void cancel(JumpAttackCancel reason);
        void tick(float dt);

        bool tryConnect(const WeaponWorldQuery& world, const Math::Vector3f& attackerPos, const Math::Vector3f& lookFlat, DamageEvent& out);

        int tryPound(const WeaponWorldQuery& world, const Math::Vector3f& landPos, DamageEvent* out, int outCap);

        void applyAirSteering(Math::Vector3f& velocity, const Math::Vector3f& pos, const Math::Vector3f& lookFlat, const Math::Vector3f* targetPos, bool hasTarget, float dt);

        void applyConnectSnap(Math::Vector3f& position, const Math::Vector3f& targetPos, float dt);

        void tickAutonomous(Math::Vector3f& position, float dt, float (*heightAt)(void*, float, float), void* heightUser, float waterY, const Math::Vector3f* targetPos, bool hasTarget, bool& landed,
                            bool& splashed);

    private:
        void enterLeap();
        void enterRecover();
        void clearLeapState();
        void fillConnectEvent(DamageEvent& out, Entity target, const Math::Vector3f& attackerPos, const Math::Vector3f& targetPos) const;
        void fillPoundEvent(DamageEvent& out, Entity target, const Math::Vector3f& landPos, const Math::Vector3f& targetPos) const;

        JumpAttackDef  m_def{};
        JumpAttackPhase m_phase = JumpAttackPhase::Idle;
        float          m_cooldown           = 0.0f;
        float          m_telegraphLeft      = 0.0f;
        float          m_connectWindowLeft  = 0.0f;
        float          m_snapLeft           = 0.0f;
        float          m_recoverLeft        = 0.0f;
        Math::Vector3f m_lookFlat{ 0.0f, 0.0f, 1.0f };
        Math::Vector3f m_velocity{};
        Math::Vector3f m_landPos{};
        Entity         m_attacker{};
        Entity         m_intendedTarget{};
        Entity         m_connectedTarget{};
        HitSet         m_hits{};
        bool           m_needTakeoff = false;
        bool           m_didConnect  = false;
    };

} // namespace Dark::Combat
