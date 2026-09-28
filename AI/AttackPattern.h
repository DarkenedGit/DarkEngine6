#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Dark::AI
{
    enum class AttackKind : unsigned char
    {
        Melee = 0,
        Jump,
    };

    enum class AttackAfterChain : unsigned char
    {
        Pause = 0,
        Reposition,
    };

    struct AttackMove
    {
        std::string id;
        AttackKind  kind      = AttackKind::Melee;
        float       minRange  = 0.0f;
        float       maxRange  = 2.2f;
        float       windup    = 0.40f;
        float       damage    = 12.0f;
        float       stunSeconds = 0.28f;
        float       poise     = 10.0f;
    };

    struct AttackPattern
    {
        std::string      id;
        float            gapSeconds          = 1.25f;
        int              chainCount          = 2;
        AttackAfterChain afterChain          = AttackAfterChain::Pause;
        float            pauseSeconds        = 2.5f;
        float            repositionSeconds   = 1.4f;
        float            repositionDistance  = 5.0f;
        float            packGapSeconds      = 1.2f;
        std::vector<AttackMove> attacks;
        bool             loaded = false;
    };

    struct AttackClock
    {
        float gap            = 0.0f;
        float pause          = 0.0f;
        float reposition     = 0.0f;
        int   chain          = 0;
        int   lastAttack     = -1;
        bool  preferOther    = false;
    };

    bool parseAttackPattern(std::string_view jsonText, AttackPattern& out);
    bool loadAttackPatternFile(const std::filesystem::path& path, AttackPattern& out);
    AttackPattern loadAttackPattern(std::string_view virtualPath);

    void tickAttackClock(AttackClock& clock, float dt);
    bool attackClockReady(const AttackClock& clock, float packGap);
    // Counts the attack. When the chain fills, arms a pause or a reposition and clears the chain.
    void noteAttackStarted(const AttackPattern& pattern, AttackClock& clock, float& packGap);

    // Index into pattern.attacks, or -1. preferOther skips lastAttack when another move fits.
    int pickAttack(const AttackPattern& pattern, float distance, bool preferOther, int lastAttack);
}
