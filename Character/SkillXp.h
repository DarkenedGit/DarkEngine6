#pragma once

#include "Character/PlayerMotor.h"
#include "Character/SkillComponent.h"
#include "ECS/World.h"
#include "Math/Vector3f.h"

namespace Dark
{

    struct MotorXpSample
    {
        PlayerMoveState state        = PlayerMoveState::Grounded;
        bool            sprint       = false;
        bool            crouch       = false;
        bool            dodged       = false;
        bool            jumped       = false;
        bool            doubleJump   = false;
        bool            jumpBusy     = false;
        float           planarMetres = 0.0f;
    };

    inline float locomotionMetres(Math::Vector3f netXZ, Math::Vector3f hitSlideXZ)
    {
        netXZ.y      = 0.0f;
        hitSlideXZ.y = 0.0f;
        if (netXZ.Magnitude() <= 1.0e-4f)
            return 0.0f;
        if (hitSlideXZ.Dot(netXZ) > 0.0f && hitSlideXZ.MagnitudeSqrd() >= netXZ.MagnitudeSqrd() - 1.0e-6f)
            return 0.0f;
        const Math::Vector3f loco{ netXZ.x - hitSlideXZ.x, 0.0f, netXZ.z - hitSlideXZ.z };
        const float          metres = loco.Magnitude();
        return metres > 0.0f ? metres : 0.0f;
    }

    // False when masked, disabled, non-positive, non-finite, or already max level.
    bool grantSkillXp(SkillComponent& comp, SkillId id, float amount);

    void tickSkill(SkillComponent& comp, float dt);
    void noteMotorXp(SkillComponent& comp, const MotorXpSample& sample);
    void noteShotXp(SkillComponent& comp, bool projectileFired);
    void noteNpcSenseXp(SkillComponent& comp, bool geometricSees, bool heardPrey, float dt);
    void notePlayerSenseXp(SkillComponent& comp, bool seeArmed, bool lightOn, bool foreignSpatialVoice, float dt);
    void noteNpcRunXp(SkillComponent& comp, bool qualifyingMove, float planarMetres);

    bool applySkillProfile(World& world, Entity e, const char* profileId);

} // namespace Dark
