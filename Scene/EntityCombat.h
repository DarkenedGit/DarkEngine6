#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Dark
{

    // Combat numbers for one entity type. A successful parse keeps any field the json omits.
    // Callers seed with entityCombatDefault so a missing file keeps today's feel.
    struct EntityCombat
    {
        int         version = 1;
        std::string type;

        float healthMax        = 48.0f;
        float healthRegen      = 5.0f;
        float healthRegenDelay = 4.0f;

        float poiseMax        = 100.0f;
        float poiseRegen      = 25.0f;
        float poiseRegenDelay = 1.5f;

        float duelStandoff  = 2.25f;
        float duelEngage    = 4.0f;
        float duelRing      = 3.2f;
        float duelPauseMin  = 1.5f;
        float duelPauseMax  = 3.0f;

        float bodyAlert      = 15.0f;
        float missingSeconds = 30.0f;
        float missingRange   = 40.0f;

        float patrolWait    = 1.5f;
        float meleeReachPad = 0.35f;
        float eyeHeight     = 0.5f;
        float groundOffset  = 1.0f;

        float blockChance  = 0.35f;
        float guardMin     = 0.8f;
        float guardMax     = 1.0f;
        int   poiseHits    = 3;
        float guardWindow  = 1.5f;
        float guardRecover = 0.7f;

        float wanderRadius = 18.0f;
        float wanderMin    = 4.0f;
        float fleeMin      = 8.0f;
        float fleeMax      = 22.0f;
        float fleeLateral  = 8.0f;

        float jumpCooldown        = 3.5f;
        float jumpGroundOffset    = 1.0f;
        float jumpConnectDamage   = 22.0f;
        float jumpConnectPoise    = 28.0f;
        float jumpPoundDamage     = 14.0f;
        float jumpPoundPoise      = 16.0f;
        float jumpLeapVertical    = 10.0f;
        float jumpLeapForwardMax  = 12.0f;
        float jumpGravity         = 24.0f;
        float jumpHoming          = 1.5f;
        float jumpKnockdown       = 0.8f;
        float jumpKnockdownForce  = 1.0f;

        float contactStandoff = 2.25f;
        float contactDps      = 12.0f;
        float respawnSeconds  = 8.0f;
        int   team            = 2;

        float parryStun                 = 0.90f;
        float chargedParryStun          = 1.80f;
        float parryKnockback            = 0.80f;
        float chargedParryKnockback     = 2.4f;
        float parryKnockSeconds         = 0.12f;
        float chargedParryKnockSeconds  = 0.22f;
        float ccRoot                    = 0.8f;
        float ccStun                    = 1.2f;
        float ccKnockdown               = 1.4f;
        float shockHitch                = 0.25f;

        float hitStopLight   = 0.07f;
        float hitStopHeavy   = 0.12f;
        float hitStopBlock   = 0.06f;
        float hitStopParry   = 0.10f;
        float shakeBlock     = 0.06f;
        float shakeParry     = 0.09f;
        float shakeHit       = 0.08f;
        float blockRecoil    = 0.55f;
        float faceSeconds    = 0.15f;
        float parryInterrupt = 1.2f;

        float comboWindow = 0.9f;
        float swingLock   = 0.45f;
        float swingSlowRecoil = 0.4f;
        float swingSlowCombo  = 0.45f;
        float swingSlowSingle = 0.8f;
        float carrySlow       = 0.6f;
        float footstepWalk    = 0.35f;
        float footstepSwim    = 0.55f;

        float drownDps         = 8.0f;
        float takedownRange    = 2.5f;
        float takedownHeight   = 1.2f;
        float takedownBackDot  = -0.2f;
        float meleeVertical    = 1.5f;
        float swordCooldown    = 1.25f;
        float swordHitDelay    = 0.12f;
        float shieldRaise      = 0.2f;
        float shieldMoveScale  = 0.55f;
        float shieldArc        = 120.0f;
        float weaponRange      = 90.0f;
        float muzzleOffset     = 2.2f;

        float repathBackoff = 0.5f;
    };

    EntityCombat entityCombatDefault(std::string_view typeName);

    std::string entityCombatVirtualPath(std::string_view typeName);

    // nlohmann::json::parse(..., false). On failure `inOut` is unchanged.
    bool parseEntityCombat(std::string_view jsonText, EntityCombat& inOut);
    bool loadEntityCombatFile(const std::filesystem::path& path, EntityCombat& inOut);
    bool loadEntityCombatType(std::string_view typeName, EntityCombat& inOut);

    // Seeds the default, then overlays the content file. Later calls return the same record.
    const EntityCombat& entityCombatFor(std::string_view typeName);

} // namespace Dark
